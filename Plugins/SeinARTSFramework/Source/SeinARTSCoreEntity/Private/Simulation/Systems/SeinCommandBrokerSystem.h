/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandBrokerSystem.h
 * @brief   Tick system for CommandBroker entities (DESIGN §5).
 *          PostTick phase.
 *
 *          Per broker, per tick:
 *            1. Strip dead members and update the live-member centroid.
 *            2. Retire every executing order whose effective members are done.
 *            3. Rebuild capabilities if dirty, then dispatch each queued order
 *               whose members are unlocked. Overlapping orders remain FIFO;
 *               disjoint subset orders may execute concurrently.
 *            4. Cull an empty ephemeral broker via DestroyEntity.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Core/SeinSystemPriority.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinCommandBrokerData.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinAbilityComponent.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"  // ReassignSlots (idle re-seek pairing)
#include "Collision/SeinCollisionSpatialHash.h"        // idle re-seek traffic gather
#include "Components/SeinMovementComponent.h"          // idle re-seek settle checks
#include "Components/SeinExtentsComponent.h"           // idle re-seek corridor radii
#include "Components/SeinExtentsHelpers.h"             // GetColliderBoundingRadius
#include "Components/SeinNavigationComponent.h"        // fallback footprint radius
#include "Components/SeinContainmentMemberData.h"      // loose-home-return: skip contained units
#include "Settings/PluginSettings.h"                   // bIdleReseek + threshold
#include "Tags/SeinARTSGameplayTags.h"                 // ground-move context for internal re-form orders
#include "Abilities/SeinAbility.h"
#include "Input/SeinCommand.h"
#include "Input/SeinCommandSchemaRegistry.h"
#include "Serialization/SeinDeterministicValueDigest.h"

/** Ability-execution dispatch helper shared with ProcessCommands (inline first-order
 *  dispatch) so both paths go through one code path. */
namespace SeinCommandBrokerDispatch
{
	/** Refactored 2026-05-07: broker dispatch now ENQUEUES per-member
	 *  ActivateAbility commands instead of calling Ability->ActivateAbility
	 *  directly. The commands go through ProcessCommands' full activation
	 *  gate next tick (cooldown, blocked tags, required tags, range,
	 *  AutoMoveThen, can-activate, cost) — the broker is a player-unit
	 *  intermediate that issues per-member commands, NOT a bypass for
	 *  ability validation. Per-member commands also appear in the command
	 *  log alongside the originating BrokerOrder, matching the "broker
	 *  resolves who-does-what, abilities self-validate" mental model.
	 *
	 *  Timing: broker tick is PostTick. Enqueued commands process in the
	 *  next sim tick's CommandProcessing phase. The broker's `bIsExecuting`
	 *  flag stays true across this 1-tick gap; the completion check (which
	 *  runs after ProcessCommands and AbilityExecution next tick) sees the
	 *  freshly-activated abilities and waits for them to end as expected.
	 *
	 *  Cost semantics: the gate runs cost-deduct per member. Free abilities
	 *  (Move, Attack) cost 0×N = 0. Single-member dispatches (smoke grenade
	 *  thrown by leader; resolver returns one MemberDispatch) charge once.
	 *  Designers wanting "one click, N members, single cost" use the
	 *  TagAlongAbility pattern at the resolver level (one member casts
	 *  paid, others tag along free). */
	static FSeinCommand BuildMemberAbilityCommand(USeinWorldSubsystem& World,
		const FSeinBrokerMemberDispatch& MD,
		FSeinPlayerID DerivedResourcePayer)
	{
		const FSeinPlayerID Owner = World.GetEntityOwner(MD.Member);
		FSeinCommand Cmd = FSeinCommand::MakeAbilityCommand(
			Owner, MD.Member, MD.AbilityTag, MD.TargetEntity, MD.TargetLocation);

		// Carry-through targeter-captured points so the ability's runtime
		// TargeterPoints array is populated when ProcessCommands activates it.
		// Empty for typical right-click flows; non-empty for targeter-UI
		// flows that already captured multi-point intent at click time.
		Cmd.TargeterPoints = MD.TargeterPoints;
		Cmd.DerivedResourcePayer = DerivedResourcePayer;
		Cmd.IssuerKind = ESeinCommandIssuerKind::DeterministicSystem;
		return Cmd;
	}

	/** Rebuild the broker's capability map: ability tag → list of entities
	 *  that hold an instance of that tag.
	 *
	 *  Walks BOTH the broker-owning entity (the squad itself, for squads) AND
	 *  every member. Per-squad design intent: a squad presents a single
	 *  deduped ability set — squad-owned + member-owned — to the player.
	 *  Dedup rule: if the squad and one-or-more members all hold an instance
	 *  of the same tag, the squad-owned handle is the SOLE entry in the
	 *  bucket. Member entries for that tag are suppressed. Rationale:
	 *  squad-owned abilities have squad-scope semantics (one cooldown,
	 *  squad-level effect) and the squad's instance is authoritative for
	 *  cooldown / cost / state. Members granted the same tag are dead weight
	 *  in that case (designer should pick one location to grant the
	 *  ability), but we don't error — we just prefer the squad's instance.
	 *
	 *  Non-squad brokers (selection-spawned ephemeral brokers, etc.) pass
	 *  through the squad-owner walk as a no-op: abstract broker entities
	 *  don't carry FSeinAbilityComponent, so the GetComponent lookup
	 *  returns null and we drop straight into the member walk — same
	 *  behavior as the pre-squad implementation. */
	static void RebuildCapabilityMap(USeinWorldSubsystem& World, FSeinEntityHandle BrokerHandle, FSeinCommandBrokerData& Broker)
	{
		Broker.CapabilityMap.Reset();

		// Pass 1: squad-owned abilities (broker-carrier entity's own AC).
		// Each tag the carrier holds claims its bucket with the carrier
		// handle as the sole entry. Tags claimed here block members from
		// adding themselves to the same bucket in pass 2 — squad-owned wins
		// the dedup.
		TSet<FGameplayTag> SquadOwnedTags;
		if (const FSeinAbilityComponent* OwnerAC = World.GetComponent<FSeinAbilityComponent>(BrokerHandle))
		{
			for (int32 ID : OwnerAC->AbilityInstanceIDs)
			{
				const USeinAbility* Ab = World.GetAbilityInstance(ID);
				if (Ab && Ab->AbilityTag.IsValid())
				{
					Broker.CapabilityMap.FindOrAdd(Ab->AbilityTag).Members.AddUnique(BrokerHandle);
					SquadOwnedTags.Add(Ab->AbilityTag);
				}
			}
		}

		// Pass 2: member abilities. Skip tags already claimed by the squad
		// (those buckets stay sole-entry = the squad handle).
		for (const FSeinEntityHandle& M : Broker.Members)
		{
			const FSeinAbilityComponent* AC = World.GetComponent<FSeinAbilityComponent>(M);
			if (!AC) continue;
			for (int32 ID : AC->AbilityInstanceIDs)
			{
				const USeinAbility* Ab = World.GetAbilityInstance(ID);
				if (Ab && Ab->AbilityTag.IsValid() && !SquadOwnedTags.Contains(Ab->AbilityTag))
				{
					Broker.CapabilityMap.FindOrAdd(Ab->AbilityTag).Members.AddUnique(M);
				}
			}
		}

		Broker.bCapabilityMapDirty = false;
	}

	/** Build the live, unique member subset addressed by an order. */
	static TArray<FSeinEntityHandle> BuildEffectiveMembers(const USeinWorldSubsystem& World,
		const FSeinCommandBrokerData& Broker,
		const FSeinBrokerQueuedOrder& Order)
	{
		const TArray<FSeinEntityHandle>& Candidates = Order.TargetMembers.IsEmpty()
			? Broker.Members : Order.TargetMembers;
		TArray<FSeinEntityHandle> Out;
		Out.Reserve(Candidates.Num());
		for (const FSeinEntityHandle& H : Candidates)
		{
			if (World.IsEntityAlive(H) && Broker.Members.Contains(H))
			{
				Out.AddUnique(H);
			}
		}
		return Out;
	}

	/** Member-side state that must still describe the same broker relationship
	 *  after a pluggable resolver returns. The resolver is allowed to grow any
	 *  pool/storage; a membership or owner change instead invalidates the plan. */
	struct FMemberPrecondition
	{
		FSeinEntityHandle Handle;
		FSeinPlayerID Owner;
		FSeinBrokerMembershipData Membership;
		bool bWasAlive = false;
		bool bHadMembership = false;
	};

	/** Stable identity of the complete broker state against which ResolveDispatch
	 *  computed. Resolver-authored layout is returned in the plan, so every live
	 *  broker field participates in the stale-input check. Resolver UObject fields
	 *  are deliberately not protocol state: any cursor that affects future output
	 *  belongs in hashed broker/component data; UObject mutation is limited to
	 *  incidental cache/debug state and does not invalidate an otherwise stable plan. */
	struct FDispatchPrecondition
	{
		FGuid BrokerDigest;
		FSeinPlayerID BrokerOwner;
		int32 ResolverID = INDEX_NONE;
		TWeakObjectPtr<USeinCommandBrokerResolver> Resolver;
		TArray<FMemberPrecondition> Members;
	};

	static bool ComputeBrokerStateDigest(
		const FSeinCommandBrokerData& Broker,
		FGuid& OutDigest)
	{
		FSeinDeterministicValueDigestOptions Options;
#if !WITH_METADATA
		// The concrete broker graph is frozen production data. Cooked UField
		// metadata is stripped, so mirror the runtime digest admission policy.
		Options.bTrustCookedTypesWithoutMetadata = true;
#endif
		FSeinDeterministicValueDigestError Error;
		const ESeinDeterministicValueDigestResult Result =
			FSeinDeterministicValueDigest::Compute(
				FSeinCommandBrokerData::StaticStruct(), &Broker,
				OutDigest, &Error, Options);
		if (Result == ESeinDeterministicValueDigestResult::Success)
		{
			return true;
		}

		UE_LOG(LogTemp, Error,
			TEXT("Broker dispatch failed to capture canonical precondition at '%s': %s"),
			*Error.FieldPath, *Error.Message);
		return false;
	}

	static bool CapturePrecondition(USeinWorldSubsystem& World,
		FSeinEntityHandle BrokerHandle,
		const FSeinCommandBrokerData& Broker,
		USeinCommandBrokerResolver& Resolver,
		FDispatchPrecondition& Out)
	{
		if (!World.IsEntityAlive(BrokerHandle)
			|| !ComputeBrokerStateDigest(Broker, Out.BrokerDigest))
		{
			return false;
		}

		Out.BrokerOwner = World.GetEntityOwner(BrokerHandle);
		Out.ResolverID = Broker.ResolverID;
		Out.Resolver = &Resolver;
		Out.Members.Reset(Broker.Members.Num());
		for (const FSeinEntityHandle& Member : Broker.Members)
		{
			FMemberPrecondition& Snapshot = Out.Members.AddDefaulted_GetRef();
			Snapshot.Handle = Member;
			Snapshot.bWasAlive = World.IsEntityAlive(Member);
			if (World.GetEntityPool().IsValid(Member))
			{
				Snapshot.Owner = World.GetEntityOwner(Member);
			}
			if (const FSeinBrokerMembershipData* Membership =
				World.GetComponent<FSeinBrokerMembershipData>(Member))
			{
				Snapshot.bHadMembership = true;
				Snapshot.Membership = *Membership;
			}
		}
		return true;
	}

	static bool PreconditionStillHolds(USeinWorldSubsystem& World,
		FSeinEntityHandle BrokerHandle,
		const FDispatchPrecondition& Before)
	{
		if (!World.IsEntityAlive(BrokerHandle)
			|| World.GetEntityOwner(BrokerHandle) != Before.BrokerOwner)
		{
			return false;
		}

		const FSeinCommandBrokerData* Broker =
			World.GetComponent<FSeinCommandBrokerData>(BrokerHandle);
		if (!Broker || Broker->ResolverID != Before.ResolverID
			|| World.GetCommandBrokerResolver(Broker->ResolverID) != Before.Resolver.Get())
		{
			return false;
		}

		FGuid AfterDigest;
		if (!ComputeBrokerStateDigest(*Broker, AfterDigest)
			|| AfterDigest != Before.BrokerDigest)
		{
			return false;
		}

		for (const FMemberPrecondition& Snapshot : Before.Members)
		{
			if (World.IsEntityAlive(Snapshot.Handle) != Snapshot.bWasAlive)
			{
				return false;
			}
			if (World.GetEntityPool().IsValid(Snapshot.Handle)
				&& World.GetEntityOwner(Snapshot.Handle) != Snapshot.Owner)
			{
				return false;
			}

			const FSeinBrokerMembershipData* Membership =
				World.GetComponent<FSeinBrokerMembershipData>(Snapshot.Handle);
			if ((Membership != nullptr) != Snapshot.bHadMembership)
			{
				return false;
			}
			if (Membership
				&& (Membership->CurrentBrokerHandle !=
						Snapshot.Membership.CurrentBrokerHandle
					|| Membership->CohesionGroupId !=
						Snapshot.Membership.CohesionGroupId))
			{
				return false;
			}
		}
		return true;
	}

	/** Validate the complete resolver plan before any live state is committed. */
	static bool ValidateDispatchPlan(USeinWorldSubsystem& World,
		FSeinEntityHandle BrokerHandle,
		const FSeinCommandBrokerData& Broker,
		const FSeinBrokerQueuedOrder& Order,
		const TArray<FSeinEntityHandle>& Effective,
		const FSeinBrokerDispatchPlan& Plan,
		TArray<FSeinCommand>& OutCommands)
	{
		const bool bUnexpectedSettledOutput = !Plan.bApplySettledSlots
			&& (!Plan.SettledSlotPositions.IsEmpty()
				|| !Plan.SettledSlotFacings.IsEmpty()
				|| Plan.bSettledSlotsMemberAligned);
		const bool bInvalidSettledOutput = Plan.bApplySettledSlots
			&& (Plan.SettledSlotPositions.Num() > Broker.Members.Num()
				|| Plan.SettledSlotFacings.Num()
					!= Plan.SettledSlotPositions.Num()
				|| (Plan.bSettledSlotsMemberAligned
					&& Plan.SettledSlotPositions.Num()
						!= Broker.Members.Num()));
		if (bUnexpectedSettledOutput || bInvalidSettledOutput)
		{
			return false;
		}

		// Membership defines the resolver's structural roster. Ownership may
		// legitimately change while an order is queued; the generated command
		// re-enters the frozen authority policy before execution.
		TSet<FSeinEntityHandle> AuthorizedMembers;
		AuthorizedMembers.Reserve(Effective.Num());
		for (const FSeinEntityHandle& Member : Effective)
		{
			const FSeinBrokerMembershipData* Membership =
				World.GetComponent<FSeinBrokerMembershipData>(Member);
			if (!World.IsEntityAlive(Member)
				|| !Membership
				|| Membership->CurrentBrokerHandle != BrokerHandle)
			{
				return false;
			}
			AuthorizedMembers.Add(Member);
		}
		if (AuthorizedMembers.Num() != Effective.Num()
			|| Plan.MemberDispatches.Num() > AuthorizedMembers.Num() + 1)
		{
			return false;
		}

		TSet<FSeinEntityHandle> SeenDispatchers;
		SeenDispatchers.Reserve(Plan.MemberDispatches.Num());
		OutCommands.Reset(Plan.MemberDispatches.Num());
		bool bUsesCarrier = false;
		for (const FSeinBrokerMemberDispatch& Dispatch : Plan.MemberDispatches)
		{
			if (!Dispatch.AbilityTag.IsValid()
				|| !World.IsEntityAlive(Dispatch.Member)
				|| SeenDispatchers.Contains(Dispatch.Member))
			{
				return false;
			}
			SeenDispatchers.Add(Dispatch.Member);

			const bool bCarrier = Dispatch.Member == BrokerHandle;
			if (!AuthorizedMembers.Contains(Dispatch.Member) && !bCarrier)
			{
				return false;
			}

			const FSeinAbilityComponent* AbilityComponent =
				World.GetComponent<FSeinAbilityComponent>(Dispatch.Member);
			if (!AbilityComponent
				|| !AbilityComponent->HasAbilityWithTag(
					World, Dispatch.AbilityTag))
			{
				return false;
			}
			bUsesCarrier |= bCarrier
				&& !AuthorizedMembers.Contains(BrokerHandle);

			FSeinCommand Command = BuildMemberAbilityCommand(
				World, Dispatch, Order.DerivedResourcePayer);
			if (World.ValidateCommandStructure(Command)
				!= ESeinCommandStructureResult::Valid)
			{
				return false;
			}
			OutCommands.Add(MoveTemp(Command));
		}

		return Plan.MemberDispatches.Num()
			<= AuthorizedMembers.Num() + (bUsesCarrier ? 1 : 0);
	}

	/** Dispatch a specific queued order via the broker's resolver. Marks the
	 *  order's per-order `bIsExecuting` + `LastDispatchTick` and enqueues
	 *  per-member ActivateAbility commands. Caller is responsible for the
	 *  pre-dispatch member-locked check — this helper assumes the order's
	 *  effective members are unlocked (i.e. not currently being driven by
	 *  another executing order in the same broker).
	 *
	 *  Returns true only when the resolver plan committed. False also means a
	 *  callback changed the broker precondition; that plan is discarded and the
	 *  still-pending order is retried on a later tick. */
	static bool DispatchOrderAtIndex(USeinWorldSubsystem& World,
		FSeinEntityHandle BrokerHandle,
		int32 OrderIndex)
	{
		FSeinCommandBrokerData* Broker =
			World.GetComponent<FSeinCommandBrokerData>(BrokerHandle);
		if (!World.IsEntityAlive(BrokerHandle) || !Broker
			|| !Broker->OrderQueue.IsValidIndex(OrderIndex)
			|| Broker->Members.Num() == 0)
		{
			return false;
		}

		USeinCommandBrokerResolver* Resolver =
			World.GetCommandBrokerResolver(Broker->ResolverID);
		if (!Resolver) return false;

		if (Broker->bCapabilityMapDirty)
		{
			RebuildCapabilityMap(World, BrokerHandle, *Broker);
		}

		const FSeinBrokerQueuedOrder Order = Broker->OrderQueue[OrderIndex];

		// Build the effective member set. If subset-targeted and the targets
		// are all dead / no longer in the broker, drop the order and bail.
		const TArray<FSeinEntityHandle> Effective =
			BuildEffectiveMembers(World, *Broker, Order);
		if (Effective.Num() == 0)
		{
			Broker->OrderQueue.RemoveAt(OrderIndex);
			return false;
		}

		FSeinBrokerOrderInput Input;
		Input.Context = Order.Context;
		Input.TargetEntity = Order.TargetEntity;
		Input.TargetLocation = Order.TargetLocation;
		Input.FormationEnd = Order.FormationEnd;
		Input.GuidePoints = Order.GuidePoints;
		Input.FormationTag = Order.FormationTag;
		Input.EffectiveMembers = Effective;
		Input.TargeterPoints = Order.TargeterPoints;
		Input.PredeterminedAbilityTag = Order.PredeterminedAbilityTag;
		Input.PreplacedMembers = Order.PreplacedMembers;
		Input.PreplacedPositions = Order.PreplacedPositions;

		FDispatchPrecondition Precondition;
		if (!CapturePrecondition(
				World, BrokerHandle, *Broker, *Resolver, Precondition))
		{
			return false;
		}

		// No entity/component/order reference survives this call. Blueprint and
		// native resolvers may synchronously allocate, add components, or destroy
		// entities; all of those can invalidate storage addresses.
		Broker = nullptr;
		const FSeinBrokerDispatchPlan Plan = Resolver->ResolveDispatch(&World, BrokerHandle, Input);

		if (!PreconditionStillHolds(World, BrokerHandle, Precondition))
		{
			return false;
		}

		TArray<FSeinCommand> ValidatedCommands;
		int32 QueueDepth = 0;
		{
			// Reacquire after callback validation, validate the full plan, then
			// atomically commit layout + the exact snapshotted order. Every reference
			// dies before command enqueue can call outward.
			FSeinCommandBrokerData* CurrentBroker =
				World.GetComponent<FSeinCommandBrokerData>(BrokerHandle);
			if (!CurrentBroker || !CurrentBroker->OrderQueue.IsValidIndex(OrderIndex))
			{
				return false;
			}
			const TArray<FSeinEntityHandle> CurrentEffective =
				BuildEffectiveMembers(
					World, *CurrentBroker, CurrentBroker->OrderQueue[OrderIndex]);
			if (CurrentEffective != Effective
				|| !ValidateDispatchPlan(
					World, BrokerHandle, *CurrentBroker, Order,
					CurrentEffective, Plan, ValidatedCommands))
			{
				return false;
			}
			FSeinBrokerQueuedOrder& CurrentOrder =
				CurrentBroker->OrderQueue[OrderIndex];
			if (Plan.bApplyAnchorFacing)
			{
				CurrentBroker->AnchorFacing = Plan.AnchorFacing;
			}
			if (Plan.bApplySettledSlots)
			{
				CurrentBroker->SettledSlotPositions = Plan.SettledSlotPositions;
				CurrentBroker->SettledSlotFacings = Plan.SettledSlotFacings;
				CurrentBroker->bSettledSlotsMemberAligned =
					Plan.bSettledSlotsMemberAligned;
			}
			CurrentBroker->CurrentOrderContext = Order.Context;
			CurrentBroker->Anchor = Order.TargetLocation;
			CurrentOrder.bIsExecuting = true;
			CurrentOrder.LastDispatchTick = World.GetCurrentTick();
			QueueDepth = CurrentBroker->OrderQueue.Num();
		}

#if !UE_BUILD_SHIPPING
		UE_LOG(LogTemp, Verbose,
			TEXT("BrokerDispatch[%s] order[%d]: %d effective members → %d member dispatches (predetermined=%s, queue-depth=%d)"),
			*BrokerHandle.ToString(),
			OrderIndex,
			Effective.Num(),
			Plan.MemberDispatches.Num(),
			Order.PredeterminedAbilityTag.IsValid() ? *Order.PredeterminedAbilityTag.ToString() : TEXT("<smart>"),
			QueueDepth);
#endif

		for (const FSeinCommand& Command : ValidatedCommands)
		{
			World.EnqueueDerivedCommand(Command);
		}
		return true;
	}
}

/**
 * System: CommandBroker
 * Phase: PostTick | Priority: 40
 */
class FSeinCommandBrokerSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		TArray<FSeinEntityHandle> CullList;
		TArray<FSeinEntityHandle> BrokerHandles;
		TArray<FSeinEntityHandle> LooseReturnList;   // un-brokered units to self-return home (deferred past the pool walk)
		const USeinARTSCoreSettings* BrokerSettings = GetDefault<USeinARTSCoreSettings>();

		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			FSeinCommandBrokerData* Broker = World.GetComponent<FSeinCommandBrokerData>(Handle);
			if (!Broker)
			{
				// LOOSE-HOME-RETURN (re-seek for the un-brokered). A unit that was never ordered has no
				// broker and no SettledSlotPositions, so the broker re-seek below can't reach it. If it is
				// idle, settled, un-contained, and shoved off its seeded HOME, queue a self-return home:
				// acted on AFTER this pool walk (CreateBrokerForMembers spawns an entity — unsafe to do
				// mid-iteration). The return mints the unit's persistent broker via the normal order path
				// (dispatch captures HomePos as its settled slot), so every LATER shove is owned by the
				// broker re-seek below. Same bIdleReseek master switch + displacement threshold as re-seek.
				if (BrokerSettings && BrokerSettings->bIdleReseek)
				{
					const FSeinMovementComponent* Move = World.GetComponent<FSeinMovementComponent>(Handle);
					if (Move && Move->bHomeSeeded && !Move->bHasTarget
						&& Move->Velocity.SizeSquared() <= FFixedPoint::Epsilon)
					{
						// Member of a live broker already? Its broker's re-seek owns it — skip.
						const FSeinBrokerMembershipData* Memb = World.GetComponent<FSeinBrokerMembershipData>(Handle);
						const bool bBrokered = Memb && Memb->CurrentBrokerHandle.IsValid()
							&& World.GetEntityPool().IsValid(Memb->CurrentBrokerHandle);
						// Contained (garrison / transport / attachment)? Its container poses it — skip.
						const FSeinContainmentMemberData* Cont = World.GetComponent<FSeinContainmentMemberData>(Handle);
						const bool bContained = Cont && Cont->CurrentContainer.IsValid();
						// Busy in an ability? Leave it be.
						const FSeinAbilityComponent* AC = World.GetComponent<FSeinAbilityComponent>(Handle);
						const USeinAbility* Active = AC ? AC->GetActiveAbility(World) : nullptr;
						const bool bBusy = Active && Active->bIsActive;
						if (!bBrokered && !bContained && !bBusy)
						{
							FFixedVector Delta = Entity.Transform.GetLocation() - Move->HomePos;
							Delta.Z = FFixedPoint::Zero;
							const FFixedPoint Thresh = BrokerSettings->ReseekDisplacementThreshold;
							if (Delta.SizeSquared() > Thresh * Thresh)
							{
								LooseReturnList.Add(Handle);
							}
						}
					}
				}
				return;
			}
			BrokerHandles.Add(Handle);
		});

		// Resolve designer callbacks only after the pool walk. A resolver may
		// synchronously spawn/destroy entities or grow component storage, so even a
		// seemingly read-only callback is not safe beneath ForEachEntity.
		for (const FSeinEntityHandle& Handle : BrokerHandles)
		{
			if (!World.IsEntityAlive(Handle)) continue;
			FSeinCommandBrokerData* Broker =
				World.GetComponent<FSeinCommandBrokerData>(Handle);
			if (!Broker) continue;

			// 1. Strip dead members (belt-and-suspenders — ProcessDeferredDestroys
			// already evicts on death, but members whose handle was released through
			// a non-destroy path would slip through without this). Also strip dead
			// handles from each queued order's TargetMembers — subset-targeted
			// orders whose every target died fall through to the empty-subset
			// guard in DispatchFrontOrder and get popped silently.
			const int32 NumBefore = Broker->Members.Num();
			Broker->Members.RemoveAll([&](const FSeinEntityHandle& M)
			{
				return !World.GetEntityPool().IsValid(M);
			});
			if (Broker->Members.Num() != NumBefore)
			{
				Broker->bCapabilityMapDirty = true;
			}
			for (FSeinBrokerQueuedOrder& Order : Broker->OrderQueue)
			{
				if (Order.TargetMembers.Num() == 0) continue;
				Order.TargetMembers.RemoveAll([&](const FSeinEntityHandle& M)
				{
					return !World.GetEntityPool().IsValid(M);
				});
			}

			// 2. Update centroid from live members.
			if (Broker->Members.Num() > 0)
			{
				FFixedVector Sum;
				int32 Count = 0;
				for (const FSeinEntityHandle& M : Broker->Members)
				{
					if (const FSeinEntity* E = World.GetEntity(M))
					{
						Sum += E->Transform.GetLocation();
						++Count;
					}
				}
				if (Count > 0)
				{
					Broker->Centroid = Sum / FFixedPoint::FromInt(Count);
				}
			}

			// 3. Per-order completion check. With per-order parallelism, ANY
			// order in the queue can be `bIsExecuting`; iterate them all and
			// pop the completed ones. Reverse iteration so RemoveAt is safe.
			//
			// Gate per order on `CurrentTick > Order.LastDispatchTick` — the
			// per-member ActivateAbility commands enqueued at dispatch don't
			// process until the next CommandProcessing phase, so members'
			// ActiveAbilityID is still INDEX_NONE same-tick and a naive
			// "all idle = done" check would falsely fire.
			//
			// Effective-member check (subset-aware): a subset-targeted order
			// only waits on its target members. Non-target members can be
			// idle, executing a different order, or queued — none of that
			// blocks the current order's completion.
			const int32 CurrentTick = World.GetCurrentTick();
			for (int32 i = Broker->OrderQueue.Num() - 1; i >= 0; --i)
			{
				FSeinBrokerQueuedOrder& Order = Broker->OrderQueue[i];
				if (!Order.bIsExecuting) continue;
				if (CurrentTick <= Order.LastDispatchTick) continue;

				const TArray<FSeinEntityHandle> Effective =
					SeinCommandBrokerDispatch::BuildEffectiveMembers(World, *Broker, Order);

				bool bAllDone = true;
				for (const FSeinEntityHandle& M : Effective)
				{
					const FSeinAbilityComponent* AC = World.GetComponent<FSeinAbilityComponent>(M);
					const USeinAbility* Active = AC ? AC->GetActiveAbility(World) : nullptr;
					if (Active && Active->bIsActive)
					{
						bAllDone = false;
						break;
					}
				}
				if (bAllDone)
				{
					Broker->OrderQueue.RemoveAt(i);
				}
			}

			// 4. Dispatch loop. Walk queue in order; dispatch any non-executing
			// order whose effective members are all unlocked (not currently
			// being driven by another executing order in this broker).
			//
			// LockedMembers is built up incrementally — as we dispatch each
			// eligible order, its members join the locked set so subsequent
			// orders in the same pass see them as blocked. This preserves FIFO
			// for orders that share members (player shift-chained full-broker
			// orders serialize naturally) while letting subset-targeted orders
			// with disjoint member sets run concurrently (the AutoMoveThen-pair
			// parallelism case).
			if (Broker->OrderQueue.Num() > 0 && Broker->Members.Num() > 0)
			{
				// Reused across brokers/ticks to avoid a per-broker set
				// allocation; Reset() clears it while keeping capacity. The
				// broker handle pass is serial, so a
				// shared member set is safe — it's fully cleared before each use.
				LockedMembers.Reset();
				for (const FSeinBrokerQueuedOrder& Order : Broker->OrderQueue)
				{
					if (!Order.bIsExecuting) continue;
					const TArray<FSeinEntityHandle> Eff =
						SeinCommandBrokerDispatch::BuildEffectiveMembers(World, *Broker, Order);
					for (const FSeinEntityHandle& M : Eff) { LockedMembers.Add(M); }
				}

				for (int32 i = 0; ; ++i)
				{
					// A previous resolver may have grown the broker component
					// storage. Reacquire at every iteration before reading it.
					Broker = World.GetComponent<FSeinCommandBrokerData>(Handle);
					if (!World.IsEntityAlive(Handle) || !Broker
						|| !Broker->OrderQueue.IsValidIndex(i))
					{
						break;
					}
					if (Broker->OrderQueue[i].bIsExecuting) continue;

					const TArray<FSeinEntityHandle> Effective =
						SeinCommandBrokerDispatch::BuildEffectiveMembers(World, *Broker, Broker->OrderQueue[i]);
					if (Effective.Num() == 0)
					{
						// Drop dead-target-only orders (members died before
						// dispatch). RemoveAt shifts later indices; decrement
						// `i` so the next iteration revisits the current slot.
						Broker->OrderQueue.RemoveAt(i);
						--i;
						continue;
					}

					bool bAnyLocked = false;
					for (const FSeinEntityHandle& M : Effective)
					{
						if (LockedMembers.Contains(M)) { bAnyLocked = true; break; }
					}
					if (bAnyLocked) continue;

					// Do not retain the outer component pointer across the
					// pluggable callback hidden inside DispatchOrderAtIndex.
					Broker = nullptr;
					if (SeinCommandBrokerDispatch::DispatchOrderAtIndex(World, Handle, i))
					{
						// Lock this order's members for the remainder of this
						// pass so subsequent eligible orders see the conflict.
						for (const FSeinEntityHandle& M : Effective) { LockedMembers.Add(M); }
					}
				}
			}

			// Dispatch can invalidate every component address or kill this broker.
			// Reacquire before the non-callback re-seek/cull work below.
			Broker = World.GetComponent<FSeinCommandBrokerData>(Handle);
			if (!World.IsEntityAlive(Handle) || !Broker) continue;

			// 4.5 IDLE RE-SEEK (opt-in, default off): a formation whose members were shoved off
			// its settled slots re-fills its OWN slots — each soldier INDIVIDUALLY gated, so the
			// re-form reads organic instead of choreographed. The FORMATION owns the slots;
			// which member takes which slot is re-decided via the anti-cross matcher. Per scan
			// (every ~0.5s, and only while no FOREIGN order is queued — the player always wins):
			//   • Pair every unreleased member to an unclaimed slot (bijective, anti-cross,
			//     deterministic). DISPLACED = far from YOUR PAIRED slot — never "near someone's
			//     slot": a blob camped around a ring must keep resolving until every slot is
			//     actually filled by its assigned member.
			//   • Each displaced member releases ON ITS OWN when ALL of: it is individually
			//     settled (ability-idle, no move target, ~zero velocity); its personal jitter
			//     delay from the episode anchor has matured (hash of its handle modulo ~1.5s —
			//     lockstep-identical); and its CORRIDOR IS CLEAR — no moving foreign unit near
			//     the straight member→slot segment (destination end included), so a soldier on
			//     the crowd's trailing edge starts back while the column still transits
			//     elsewhere, and nobody marches into oncoming traffic. Own members in motion
			//     never block a corridor (same-formation traffic is cohesion-skipped and
			//     brush-bys are the collision floor's job).
			//   • Each released soldier gets its OWN one-member internal order (claimed slots /
			//     members are excluded from later pairings); the episode ends (anchor reset +
			//     a 1s quiet period) when nobody is pair-displaced and nothing is in flight.
			// IT3 handoff note: the per-member release gate is where a future transit-dodge
			// behavior plugs in — "dodge active" simply suppresses that member's release.
			// Deaths stale the layout (member/slot counts diverge) → re-seek suspends until the
			// next full ground order recaptures. Squad brokers never capture slots → skip.
			const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
			if (Settings->bIdleReseek
				&& Broker->Members.Num() > 0
				&& Broker->SettledSlotPositions.Num() == Broker->Members.Num()
				&& CurrentTick >= Broker->NextReseekAllowedTick)
			{
				// Scan cadences, both designer-tuned (seconds → whole ticks, deterministic):
				// WATCH (cold) = how often an undisturbed formation checks for displacement;
				// RELEASE (hot) = how often releases are sampled during an active episode —
				// floor of one tick, so the default 0 means "every tick" (each soldier fires
				// the moment its own gates open; coarser values quantize releases into waves).
				const int32 TickRate = (Settings->SimulationTickRate > 1) ? Settings->SimulationTickRate : 1;
				int32 WatchTicks = (Settings->ReseekWatchInterval * FFixedPoint::FromInt(TickRate)).ToInt();
				if (WatchTicks < 1) { WatchTicks = 1; }
				int32 ReleaseTicks = (Settings->ReseekReleaseInterval * FFixedPoint::FromInt(TickRate)).ToInt();
				if (ReleaseTicks < 1) { ReleaseTicks = 1; }
				// Episode-duration cap (B1): a self-feeding re-form limit cycle never reaches the
				// "nobody displaced" end condition below, so bound one episode in wall-seconds.
				// 0 = disabled. Reuses the already-hashed ReseekEpisodeStartTick; adds no sim state.
				int32 MaxEpisodeTicks = (Settings->ReseekMaxEpisodeSeconds * FFixedPoint::FromInt(TickRate)).ToInt();
				if (MaxEpisodeTicks < 0) { MaxEpisodeTicks = 0; }
				// Only an exact 0 disables the cap; a positive-but-sub-tick value rounds up to one
				// tick rather than silently truncating to "off".
				if (MaxEpisodeTicks == 0 && Settings->ReseekMaxEpisodeSeconds > FFixedPoint::Zero) { MaxEpisodeTicks = 1; }
				Broker->NextReseekAllowedTick = CurrentTick + WatchTicks;

				// Foreign-order gate: any queued order that is NOT one of our re-form subsets
				// (internal + pre-placed) suspends re-seek — the player always wins.
				bool bForeignOrder = false;
				for (const FSeinBrokerQueuedOrder& Queued : Broker->OrderQueue)
				{
					if (!(Queued.bIsInternalPrefix && Queued.PreplacedPositions.Num() > 0))
					{
						bForeignOrder = true;
						break;
					}
				}

				// B1: if a hot episode has outlived the cap without converging, declare the crowd
				// good-enough - stop correcting, end the episode, and grant an extended quiet period.
				// Mode-agnostic: bounds any limit cycle regardless of a mode's return dynamics. Sits
				// before the pairing/release work so a capped scan does no further re-forming.
				const bool bEpisodeCapped = (Broker->ReseekEpisodeStartTick != 0
					&& MaxEpisodeTicks > 0
					&& CurrentTick - Broker->ReseekEpisodeStartTick > MaxEpisodeTicks);
				if (bEpisodeCapped)
				{
					Broker->ReseekEpisodeStartTick = 0;
					Broker->NextReseekAllowedTick = CurrentTick + (TickRate * 2);
				}
				else if (!bForeignOrder)
				{
					// In-flight claims: members already released + the slots their re-form
					// orders target. Later pairings cover only the remainder.
					TSet<FSeinEntityHandle> ReleasedMembers;
					TArray<FFixedVector> ClaimedSlots;
					for (const FSeinBrokerQueuedOrder& Queued : Broker->OrderQueue)
					{
						for (const FSeinEntityHandle& RM : Queued.PreplacedMembers) { ReleasedMembers.Add(RM); }
						ClaimedSlots.Append(Queued.PreplacedPositions);
					}

					// Pair the unreleased members to their return slots. Two modes, chosen by the
					// CAPTURING resolver via bSettledSlotsMemberAligned:
					//   ALIGNED (squads)  — slot i belongs to Members i; every member returns to
					//                       ITS OWN authored slot, roster never re-shuffles.
					//   FREE (loose)      — bijective anti-cross re-match over the remainder so
					//                       the return crosses as little as possible.
					// A member already standing on its paired slot reads "on station" below.
					TArray<FSeinEntityHandle> Unreleased;
					TArray<FFixedVector> Paired;
					bool bPairingValid = true;
					if (Broker->bSettledSlotsMemberAligned)
					{
						for (int32 j = 0; j < Broker->Members.Num(); ++j)
						{
							if (ReleasedMembers.Contains(Broker->Members[j])) continue;
							Unreleased.Add(Broker->Members[j]);
							Paired.Add(Broker->SettledSlotPositions[j]);
						}
					}
					else
					{
						for (const FSeinEntityHandle& M : Broker->Members)
						{
							if (!ReleasedMembers.Contains(M)) { Unreleased.Add(M); }
						}
						// Unclaimed slots (exact fixed-point identity — claimed slot values are
						// copies straight out of SettledSlotPositions).
						TArray<FFixedVector> Unclaimed;
						for (const FFixedVector& Slot : Broker->SettledSlotPositions)
						{
							bool bClaimed = false;
							for (const FFixedVector& C : ClaimedSlots)
							{
								if (C.X == Slot.X && C.Y == Slot.Y) { bClaimed = true; break; }
							}
							if (!bClaimed) { Unclaimed.Add(Slot); }
						}
						// Defensive: transient count skew (a member died with its re-form in
						// flight) → skip this scan; dead-strip + completion pop reconverge.
						bPairingValid = (Unreleased.Num() == Unclaimed.Num());
						if (bPairingValid)
						{
							Paired = Unclaimed;
							USeinDefaultCommandBrokerResolver::ReassignSlots(
								&World, Unreleased, Paired, Broker->AnchorFacing,
								/*bLateral*/ true, /*bDepth*/ true);
						}
					}

					if (bPairingValid && Unreleased.Num() > 0)
					{

						// MOVING foreign traffic, gathered ONCE per scan over the combined
						// slots+members bounds (+margin): position + body radius per mover, for
						// the per-member corridor tests below. Parked foreigners are ignored
						// (standing occupancy is not transit); own members never block.
						TArray<FFixedVector> TrafficPos;
						TArray<FFixedPoint> TrafficRadius;
						{
							FFixedVector Min = Broker->SettledSlotPositions[0];
							FFixedVector Max = Min;
							const auto Grow = [&Min, &Max](const FFixedVector& P)
							{
								if (P.X < Min.X) Min.X = P.X;
								if (P.Y < Min.Y) Min.Y = P.Y;
								if (P.X > Max.X) Max.X = P.X;
								if (P.Y > Max.Y) Max.Y = P.Y;
							};
							for (const FFixedVector& Slot : Broker->SettledSlotPositions) { Grow(Slot); }
							for (const FSeinEntityHandle& M : Unreleased)
							{
								if (const FSeinEntity* E = World.GetEntity(M)) { Grow(E->Transform.GetLocation()); }
							}
							const FFixedVector Centre(
								(Min.X + Max.X) / FFixedPoint::Two, (Min.Y + Max.Y) / FFixedPoint::Two,
								Broker->SettledSlotPositions[0].Z);
							FFixedVector HalfSpan(Max.X - Centre.X, Max.Y - Centre.Y, FFixedPoint::Zero);
							const FFixedPoint GatherRadius = HalfSpan.Size() + FFixedPoint::FromInt(400);

							ReseekScratchNeighbors.Reset();
							World.GetCollisionSpatialHash().QueryRadius(
								Centre, GatherRadius, ReseekScratchNeighbors, FSeinEntityHandle());
							for (const FSeinEntityHandle& N : ReseekScratchNeighbors)
							{
								if (Broker->Members.Contains(N)) continue;
								const FSeinMovementComponent* NMove = World.GetComponent<FSeinMovementComponent>(N);
								if (!NMove) continue; // static geometry — not traffic
								// Skip PARKED foreigners (no order + at rest). A dodging idler carries honest
								// non-zero velocity (the idle-dodge decouple), so it correctly reads as traffic here.
								if (!NMove->bHasTarget && NMove->Velocity.SizeSquared() <= FFixedPoint::Epsilon) continue;
								const FSeinEntity* NE = World.GetEntity(N);
								if (!NE) continue;
								FFixedPoint R = FFixedPoint::Zero;
								if (const FSeinExtentsComponent* NExt = World.GetComponent<FSeinExtentsComponent>(N))
								{
									R = SeinExtentsHelpers::GetColliderBoundingRadius(*NExt);
								}
								if (R <= FFixedPoint::Zero)
								{
									if (const FSeinNavigationComponent* NNav = World.GetComponent<FSeinNavigationComponent>(N))
									{
										R = NNav->FallbackFootprintRadius;
									}
								}
								if (R <= FFixedPoint::Zero) { R = FFixedPoint::FromInt(50); }
								TrafficPos.Add(NE->Transform.GetLocation());
								TrafficRadius.Add(R);
							}
						}

						// Planar point→segment distance² (the corridor test workhorse). A
						// degenerate segment (member already at its slot) collapses to a point
						// distance. All fixed-point; deterministic.
						const auto SegDistSq = [](const FFixedVector& P, const FFixedVector& A, const FFixedVector& B) -> FFixedPoint
						{
							const FFixedPoint ABX = B.X - A.X;
							const FFixedPoint ABY = B.Y - A.Y;
							const FFixedPoint LenSq = ABX * ABX + ABY * ABY;
							FFixedPoint T = FFixedPoint::Zero;
							if (LenSq > FFixedPoint::Epsilon)
							{
								T = ((P.X - A.X) * ABX + (P.Y - A.Y) * ABY) / LenSq;
								if (T < FFixedPoint::Zero) T = FFixedPoint::Zero;
								if (T > FFixedPoint::One)  T = FFixedPoint::One;
							}
							const FFixedPoint CX = A.X + ABX * T;
							const FFixedPoint CY = A.Y + ABY * T;
							const FFixedPoint DX = P.X - CX;
							const FFixedPoint DY = P.Y - CY;
							return DX * DX + DY * DY;
						};

						const FFixedPoint Threshold = Settings->ReseekDisplacementThreshold;
						const int32 WindowTicks = ((TickRate * 3) / 2 > 1) ? (TickRate * 3) / 2 : 1;
						bool bAnyDisplaced = false;
						int32 ReleasedThisScan = 0;

						for (int32 i = 0; i < Unreleased.Num(); ++i)
						{
							const FSeinEntityHandle& M = Unreleased[i];
							const FSeinMovementComponent* Move = World.GetComponent<FSeinMovementComponent>(M);
							const FSeinEntity* E = World.GetEntity(M);
							if (!Move || !E) continue;

							// PAIRED displacement — distance to YOUR assigned slot, never "near
							// someone's slot": a blob camped around a ring keeps resolving until
							// every slot is filled by its assigned member.
							const FFixedVector Pos = E->Transform.GetLocation();
							// Structural hysteresis floor (A): the on-station band can never sit at or
							// below the unit's arrival acceptance, or a just-arrived, collision-jostled
							// member reads as "displaced" and the formation re-forms forever. Floor at
							// twice acceptance (arrival tolerance plus an equal jostle band); a configured
							// threshold above the floor wins. Per-member: AcceptanceRadius is hashed sim state.
							FFixedPoint MemberAcceptance = FFixedPoint::Zero;
							if (const FSeinNavigationComponent* MNav = World.GetComponent<FSeinNavigationComponent>(M))
							{
								MemberAcceptance = MNav->AcceptanceRadius;
							}
							// Match the movement layer's arrival fallback: a nav-less unit (or one with a
							// zero acceptance) completes moves within 50 (SeinMoveToAction / the trace
							// system use the same constant). Compute the floor against that real arrival
							// tolerance, not 0 - otherwise those members would slip the floor entirely.
							if (MemberAcceptance <= FFixedPoint::Zero) { MemberAcceptance = FSeinNavigationComponent::DefaultArrivalAcceptance(); }
							FFixedPoint EffThreshold = Threshold;
							const FFixedPoint Floor = MemberAcceptance + MemberAcceptance;
							if (EffThreshold < Floor) { EffThreshold = Floor; }
							const FFixedPoint EffThresholdSq = EffThreshold * EffThreshold;
							const FFixedPoint DX = Pos.X - Paired[i].X;
							const FFixedPoint DY = Pos.Y - Paired[i].Y;
							if (DX * DX + DY * DY <= EffThresholdSq) continue; // on station

							// Idle-dodge no longer suppresses re-seek here. The dodge writes a real velocity, so
							// the settled-predicate just below (velocity ~zero) already holds a dodging member back
							// from re-forming and releases it once the dodge ends and its velocity coasts to zero -
							// dodge and re-seek decoupled through that one honest signal. A dodging member DOES count
							// as displaced (keeps the episode alive), so its return fires within the same episode the
							// moment it clears.
							bAnyDisplaced = true;

							// Individually settled? (Ability-idle, no move target, ~zero velocity.)
							const FSeinAbilityComponent* AC = World.GetComponent<FSeinAbilityComponent>(M);
							const USeinAbility* Active = AC ? AC->GetActiveAbility(World) : nullptr;
							if ((Active && Active->bIsActive)
								|| Move->bHasTarget
								|| Move->Velocity.SizeSquared() > FFixedPoint::Epsilon)
							{
								continue; // displaced but not settled — keeps the episode alive
							}

							// Episode anchors on the first displaced sighting (deterministic order).
							if (Broker->ReseekEpisodeStartTick == 0)
							{
								Broker->ReseekEpisodeStartTick = CurrentTick;
							}
							// Personal jitter matured? (Hash of handle — lockstep-identical.)
							const int32 Jitter = static_cast<int32>(
								GetTypeHash(M) % static_cast<uint32>(WindowTicks));
							if (CurrentTick < Broker->ReseekEpisodeStartTick + Jitter) continue;

							// CORRIDOR CLEAR? Any moving foreign unit inside the member→slot
							// corridor (halfwidth = both bodies + margin; destination end
							// included) blocks THIS member only — a soldier on the crowd's
							// trailing edge starts back while the column still transits elsewhere.
							// (IT3 handoff: a future transit-dodge state also suppresses here.)
							FFixedPoint SelfR = FFixedPoint::Zero;
							if (const FSeinExtentsComponent* SExt = World.GetComponent<FSeinExtentsComponent>(M))
							{
								SelfR = SeinExtentsHelpers::GetColliderBoundingRadius(*SExt);
							}
							if (SelfR <= FFixedPoint::Zero)
							{
								if (const FSeinNavigationComponent* SNav = World.GetComponent<FSeinNavigationComponent>(M))
								{
									SelfR = SNav->FallbackFootprintRadius;
								}
							}
							if (SelfR <= FFixedPoint::Zero) { SelfR = FFixedPoint::FromInt(50); }

							bool bCorridorClear = true;
							for (int32 t = 0; t < TrafficPos.Num(); ++t)
							{
								const FFixedPoint Halfwidth = SelfR + TrafficRadius[t] + FFixedPoint::FromInt(100);
								if (SegDistSq(TrafficPos[t], Pos, Paired[i]) < Halfwidth * Halfwidth)
								{
									bCorridorClear = false;
									break;
								}
							}
							if (!bCorridorClear) continue;

							// RELEASE — this soldier alone, straight to its paired slot. One-
							// member TargetMembers is CRITICAL (empty would fan the order across
							// ALL members and send everyone to the anchor).
							FSeinBrokerQueuedOrder Order;
							Order.TargetMembers.Add(M);
							Order.PreplacedMembers.Add(M);
							Order.PreplacedPositions.Add(Paired[i]);
							Order.Context.AddTag(SeinARTSTags::Command_Context_RightClick);
							Order.Context.AddTag(SeinARTSTags::Command_Context_Target_Ground);
							Order.TargetLocation = Broker->Anchor;
							Order.bIsInternalPrefix = true;
							Broker->OrderQueue.Add(Order);
							++ReleasedThisScan;
						}

						const bool bInFlight = ReleasedMembers.Num() > 0 || ReleasedThisScan > 0;
						if (!bAnyDisplaced && !bInFlight)
						{
							// Episode over. Reset the anchor; grant a quiet period so a finished
							// re-form doesn't immediately rescan.
							if (Broker->ReseekEpisodeStartTick != 0)
							{
								Broker->ReseekEpisodeStartTick = 0;
								Broker->NextReseekAllowedTick = CurrentTick + TickRate;
							}
						}
						else
						{
							// EPISODE HOT → rescan at the RELEASE cadence. The watch cadence
							// quantized releases into visible mini-waves (everyone whose jitter
							// matured / corridor cleared within the same scan window fired
							// together on the scan tick); the release interval samples finer —
							// default every tick, so each soldier releases the exact tick its
							// own gates open. Cost is transient and bounded (only brokers with
							// a live episode, only while it lasts).
							Broker->NextReseekAllowedTick = CurrentTick + ReleaseTicks;
						}
					}
				}
			}

			// 5. Cull if empty. Queue-empty implies no orders executing under
			// the per-order model — no separate broker-level bIsExecuting flag
			// to consult anymore.
			if (Broker->bSelfCullOnEmpty && Broker->Members.Num() == 0 && Broker->OrderQueue.Num() == 0)
			{
				CullList.Add(Handle);
			}
		}

		for (const FSeinEntityHandle& H : CullList)
		{
			World.DestroyEntity(H);
		}

		// Self-issue the loose-home returns collected above (deferred — each mints a broker entity,
		// unsafe during the pool walk). Each is a single-member ground move back to the unit's seeded
		// home; CreateBrokerForMembers builds its persistent broker and the dispatch captures HomePos
		// as the settled slot, so every subsequent shove routes through the broker re-seek. The list
		// is in pool order and broker allocation is deterministic, so the whole pass is deterministic.
		for (const FSeinEntityHandle& H : LooseReturnList)
		{
			const FSeinMovementComponent* Move = World.GetComponent<FSeinMovementComponent>(H);
			if (!Move || !Move->bHomeSeeded) continue;
			// Re-validate un-brokered (a broker could have been minted for it earlier in this loop).
			const FSeinBrokerMembershipData* Memb = World.GetComponent<FSeinBrokerMembershipData>(H);
			if (Memb && Memb->CurrentBrokerHandle.IsValid()
				&& World.GetEntityPool().IsValid(Memb->CurrentBrokerHandle)) continue;

			FSeinBrokerQueuedOrder Order;
			Order.TargetMembers.Add(H);
			Order.PreplacedMembers.Add(H);
			Order.PreplacedPositions.Add(Move->HomePos);
			Order.Context.AddTag(SeinARTSTags::Command_Context_RightClick);
			Order.Context.AddTag(SeinARTSTags::Command_Context_Target_Ground);
			Order.TargetLocation = Move->HomePos;

			TArray<FSeinEntityHandle> Members;
			Members.Add(H);
			World.CreateBrokerForMembers(Members, World.GetEntityOwner(H), Order);
		}
	}

	virtual FSeinSystemDescriptor DescribeSystem() const override
	{
		return FSeinSystemDescriptor::Stateless(
			FName(TEXT("seinarts.core.command_broker")),
			1u,
			ESeinTickPhase::PostTick,
			SeinSystemPriority::CommandBroker);
	}

private:
	/** Scratch set for the per-broker dispatch loop's member-lock tracking,
	 *  reused (Reset() each broker) to avoid a per-broker-per-tick allocation.
	 *  Sim-thread-only scratch — the broker tick runs serially. */
	TSet<FSeinEntityHandle> LockedMembers;

	/** Scratch neighbor buffer for the idle re-seek traffic-clearance query,
	 *  reused (Reset() each scan) for the same no-per-tick-allocation reason.
	 *  Sim-thread-only scratch — the broker tick runs serially. */
	TArray<FSeinEntityHandle> ReseekScratchNeighbors;
};

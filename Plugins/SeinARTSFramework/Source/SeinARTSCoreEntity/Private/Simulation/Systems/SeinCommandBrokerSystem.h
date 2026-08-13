/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinCommandBrokerSystem.h
 * @author       RJ Macklem
 * @created      02 Jun 2026
 * @latest       13 Aug 2026
 * @brief        Dispatches broker orders and coordinates deterministic idle
 *               formation return during the PostTick phase.
 *
 *               Order dispatch, completion, and broker lifetime stay in this
 *               system. The private re-seek kernel owns return pairing,
 *               traffic clearance, release cadence, and loose home return.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Core/SeinSystemPriority.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/ComponentStorage.h"
#include "Components/SeinCommandBrokerData.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinAbilityComponent.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "Abilities/SeinAbility.h"
#include "Input/SeinCommand.h"
#include "Input/SeinCommandSchemaRegistry.h"
#include "Serialization/SeinDeterministicValueDigest.h"
#include "Simulation/Systems/SeinCommandBrokerReseek.h"

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
			World.GetComponentMutable<FSeinCommandBrokerData>(BrokerHandle);
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
				World.GetComponentMutable<FSeinCommandBrokerData>(
					BrokerHandle);
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
		TArray<FSeinEntityHandle> LooseReturnList;

		// Broker work is sparse: walk the exact live component slots rather than
		// probing every entity in the world for a broker component.
		if (const ISeinComponentStorage* BrokerStorage =
			World.GetComponentStorageRaw(FSeinCommandBrokerData::StaticStruct()))
		{
			BrokerStorage->ForEachLiveComponent([&](
				FSeinEntityHandle Handle, const void* /*RawComponent*/)
			{
				if (World.GetEntityPool().IsValid(Handle))
				{
					BrokerHandles.Add(Handle);
				}
			});
		}

		IdleReseek.CollectLooseReturnCandidates(World, LooseReturnList);

		// Resolve designer callbacks only after the pool walk. A resolver may
		// synchronously spawn/destroy entities or grow component storage, so even a
		// seemingly read-only callback is not safe beneath a storage walk.
		for (const FSeinEntityHandle& Handle : BrokerHandles)
		{
			if (!World.IsEntityAlive(Handle)) continue;
			FSeinCommandBrokerData* Broker =
				World.GetComponentMutable<FSeinCommandBrokerData>(Handle);
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
					Broker = World.GetComponentMutable<FSeinCommandBrokerData>(
						Handle);
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
			Broker = World.GetComponentMutable<FSeinCommandBrokerData>(Handle);
			if (!World.IsEntityAlive(Handle) || !Broker) continue;

			IdleReseek.ProcessBroker(World, *Broker, CurrentTick);

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

		IdleReseek.IssueLooseReturns(World, LooseReturnList);
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


	FSeinCommandBrokerReseek IdleReseek;
};

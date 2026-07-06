/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandBrokerSystem.h
 * @brief   Tick system for CommandBroker entities (DESIGN §5).
 *          PostTick phase.
 *
 *          Per broker, per tick:
 *            1. Strip dead members; mark capability map dirty if the list shrinks.
 *            2. Update centroid from live member positions.
 *            3. If executing: poll members; when every member's ability has ended,
 *               clear the executing flag, pop the front order, dispatch the next.
 *            4. If not executing + queue non-empty: rebuild capability map if
 *               dirty + call resolver + issue per-member ActivateAbility internally.
 *            5. If no members and no pending orders: cull via DestroyEntity.
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
#include "Settings/PluginSettings.h"                   // bIdleReseek + threshold
#include "Tags/SeinARTSGameplayTags.h"                 // ground-move context for internal re-form orders
#include "Abilities/SeinAbility.h"
#include "Input/SeinCommand.h"

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
	static void ActivateMemberAbility(USeinWorldSubsystem& World, const FSeinBrokerMemberDispatch& MD)
	{
		if (!World.GetEntityPool().IsValid(MD.Member)) return;

		const FSeinPlayerID Owner = World.GetEntityOwner(MD.Member);
		FSeinCommand Cmd = FSeinCommand::MakeAbilityCommand(
			Owner, MD.Member, MD.AbilityTag, MD.TargetEntity, MD.TargetLocation);

		// Carry-through targeter-captured points so the ability's runtime
		// TargeterPoints array is populated when ProcessCommands activates it.
		// Empty for typical right-click flows; non-empty for targeter-UI
		// flows that already captured multi-point intent at click time.
		Cmd.TargeterPoints = MD.TargeterPoints;

		World.EnqueueCommand(Cmd);
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

	/** Build the effective member set for a queued order — TargetMembers if
	 *  non-empty (subset-targeted), else the broker's full Members. Subset
	 *  entries are also filtered for liveness (dead handles skipped). */
	static TArray<FSeinEntityHandle> BuildEffectiveMembers(const USeinWorldSubsystem& World,
		const FSeinCommandBrokerData& Broker,
		const FSeinBrokerQueuedOrder& Order)
	{
		if (Order.TargetMembers.Num() == 0)
		{
			return Broker.Members;
		}
		TArray<FSeinEntityHandle> Out;
		Out.Reserve(Order.TargetMembers.Num());
		for (const FSeinEntityHandle& H : Order.TargetMembers)
		{
			if (World.GetEntityPool().IsValid(H) && Broker.Members.Contains(H))
			{
				Out.Add(H);
			}
		}
		return Out;
	}

	/** Dispatch a specific queued order via the broker's resolver. Marks the
	 *  order's per-order `bIsExecuting` + `LastDispatchTick` and enqueues
	 *  per-member ActivateAbility commands. Caller is responsible for the
	 *  pre-dispatch member-locked check — this helper assumes the order's
	 *  effective members are unlocked (i.e. not currently being driven by
	 *  another executing order in the same broker).
	 *
	 *  Returns true if the order was dispatched. False on resolver-missing
	 *  or empty-effective-members (the latter pops the order from the queue
	 *  before returning). */
	static bool DispatchOrderAtIndex(USeinWorldSubsystem& World,
		FSeinEntityHandle BrokerHandle,
		FSeinCommandBrokerData& Broker,
		int32 OrderIndex)
	{
		if (!Broker.OrderQueue.IsValidIndex(OrderIndex) || Broker.Members.Num() == 0) return false;
		USeinCommandBrokerResolver* Resolver = World.GetCommandBrokerResolver(Broker.ResolverID);
		if (!Resolver) return false;

		if (Broker.bCapabilityMapDirty)
		{
			RebuildCapabilityMap(World, BrokerHandle, Broker);
		}

		FSeinBrokerQueuedOrder& Order = Broker.OrderQueue[OrderIndex];

		// Build the effective member set. If subset-targeted and the targets
		// are all dead / no longer in the broker, drop the order and bail.
		const TArray<FSeinEntityHandle> Effective = BuildEffectiveMembers(World, Broker, Order);
		if (Effective.Num() == 0)
		{
			Broker.OrderQueue.RemoveAt(OrderIndex);
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

		const FSeinBrokerDispatchPlan Plan = Resolver->ResolveDispatch(&World, BrokerHandle, Input);

		// Most-recently-dispatched context for UI / diagnostics. Anchor is
		// per-broker (not per-order) because the resolver consumes it for
		// formation-relative placement. With per-order parallelism, anchor
		// reflects the latest dispatched order's target.
		Broker.CurrentOrderContext = Order.Context;
		Broker.Anchor = Order.TargetLocation;

		// Per-order execution state. CurrentTick stamp guards the completion
		// check from prematurely popping the order in the same tick it
		// dispatched — see FSeinBrokerQueuedOrder::LastDispatchTick comment.
		Order.bIsExecuting = true;
		Order.LastDispatchTick = World.GetCurrentTick();

#if !UE_BUILD_SHIPPING
		UE_LOG(LogTemp, Verbose,
			TEXT("BrokerDispatch[%s] order[%d]: %d effective members → %d member dispatches (predetermined=%s, queue-depth=%d)"),
			*BrokerHandle.ToString(),
			OrderIndex,
			Effective.Num(),
			Plan.MemberDispatches.Num(),
			Order.PredeterminedAbilityTag.IsValid() ? *Order.PredeterminedAbilityTag.ToString() : TEXT("<smart>"),
			Broker.OrderQueue.Num());
#endif

		for (const FSeinBrokerMemberDispatch& MD : Plan.MemberDispatches)
		{
			ActivateMemberAbility(World, MD);
		}
		return true;
	}
}

/**
 * System: CommandBroker
 * Phase: PostTick | Priority: 40 (before StateHashSystem)
 */
class FSeinCommandBrokerSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		TArray<FSeinEntityHandle> CullList;

		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& /*Entity*/)
		{
			FSeinCommandBrokerData* Broker = World.GetComponent<FSeinCommandBrokerData>(Handle);
			if (!Broker) return;

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
				// broker tick is serial (ForEachEntity is a plain loop), so a
				// shared member set is safe — it's fully cleared before each use.
				LockedMembers.Reset();
				for (const FSeinBrokerQueuedOrder& Order : Broker->OrderQueue)
				{
					if (!Order.bIsExecuting) continue;
					const TArray<FSeinEntityHandle> Eff =
						SeinCommandBrokerDispatch::BuildEffectiveMembers(World, *Broker, Order);
					for (const FSeinEntityHandle& M : Eff) { LockedMembers.Add(M); }
				}

				for (int32 i = 0; i < Broker->OrderQueue.Num(); ++i)
				{
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

					if (SeinCommandBrokerDispatch::DispatchOrderAtIndex(World, Handle, *Broker, i))
					{
						// Lock this order's members for the remainder of this
						// pass so subsequent eligible orders see the conflict.
						for (const FSeinEntityHandle& M : Effective) { LockedMembers.Add(M); }
					}
				}
			}

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

				if (!bForeignOrder)
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
						const FFixedPoint ThresholdSq = Threshold * Threshold;
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
							const FFixedPoint DX = Pos.X - Paired[i].X;
							const FFixedPoint DY = Pos.Y - Paired[i].Y;
							if (DX * DX + DY * DY <= ThresholdSq) continue; // on station

							// DODGE-ACTIVE SUPPRESSION (the IT3 transit-dodge plug-point). A member
							// actively stepping aside for a passing mover — its own avoidance SteerDir
							// is non-zero (idlers only ever get a non-zero SteerDir from the idle-dodge)
							// — is treated exactly like an on-station member: no re-form order, and it
							// does NOT count as displaced. When the mover passes and the dodge clears
							// (SteerDir hard-zeros), this SAME gate next tick sees it displaced+settled
							// and issues the return — so the shipped re-seek owns the walk-back,
							// un-duplicated (no parallel return in TickIdle). One benign wrinkle: a
							// suppressed member not counting toward bAnyDisplaced can let the episode
							// end-reset mid-dodge (re-bases the personal jitter); the return still fires.
							if (Move->AvoidanceOutput.SteerDir.SizeSquared() > FFixedPoint::Epsilon) continue;

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
		});

		for (const FSeinEntityHandle& H : CullList)
		{
			World.DestroyEntity(H);
		}
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PostTick; }
	virtual int32 GetPriority() const override { return SeinSystemPriority::CommandBroker; }
	virtual FName GetSystemName() const override { return TEXT("CommandBroker"); }

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

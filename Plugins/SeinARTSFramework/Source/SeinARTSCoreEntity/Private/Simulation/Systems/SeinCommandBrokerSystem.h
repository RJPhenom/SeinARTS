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
				TSet<FSeinEntityHandle> LockedMembers;
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
};

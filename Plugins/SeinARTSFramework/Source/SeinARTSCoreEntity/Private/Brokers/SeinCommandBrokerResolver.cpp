/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandBrokerResolver.cpp
 * @brief   Default-on-nothing base implementation. Subclasses (BP or C++)
 *          override ResolveDispatch/ResolvePositions.
 */

#include "Brokers/SeinCommandBrokerResolver.h"
#include "Abilities/SeinAbility.h"
#include "Components/SeinAbilityPayload.h"
#include "Components/SeinSquadPayload.h"
#include "Components/SeinSquadMemberPayload.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace SeinDispatchPolicyLocal
{
	/** Sort candidates into dispatch order:
	 *    - Squad broker: slot-declaration order; squad-self (if a candidate)
	 *      sorts LAST so selectors like FirstAvailable prefer member
	 *      dispatchers over the squad entity unless explicitly squad-targeted.
	 *    - Non-squad broker: pass through in caller order.
	 *
	 *  Stable across ticks because slots is a stable authored array. */
	static TArray<FSeinEntityHandle> SortDispatchOrder(
		USeinWorldSubsystem* World,
		FSeinEntityHandle BrokerHandle,
		const TArray<FSeinEntityHandle>& Candidates)
	{
		if (!World) return Candidates;
		const FSeinSquadPayload* Squad = World->GetComponent<FSeinSquadPayload>(BrokerHandle);
		if (!Squad) return Candidates;

		TArray<FSeinEntityHandle> Out;
		Out.Reserve(Candidates.Num());
		for (const FSeinSquadSlot& Slot : Squad->Slots)
		{
			if (!Slot.CurrentOccupant.IsValid()) continue;
			if (Candidates.Contains(Slot.CurrentOccupant))
			{
				Out.AddUnique(Slot.CurrentOccupant);
			}
		}
		// Squad-self last so member dispatchers are preferred by FirstAvailable.
		if (Candidates.Contains(BrokerHandle))
		{
			Out.AddUnique(BrokerHandle);
		}
		// Stragglers (candidates that aren't slot occupants and aren't the
		// broker handle — shouldn't happen on a well-formed squad, but defensive
		// against mid-teardown / orphaned member states).
		for (const FSeinEntityHandle& C : Candidates)
		{
			Out.AddUnique(C);
		}
		return Out;
	}

	/** True iff the candidate's entity-tag state (BaseTags + dynamic GrantedTags
	 *  refcounted via USeinWorldSubsystem::GrantTag) contains `Tag`. Uses the
	 *  central HasTag accessor — no per-component lookup, no slot-tag fallback.
	 *  Tag matching is intentionally entity-level so designers tag the UNIT
	 *  itself (`Unit.Role.Officer`) rather than relying on squad-slot tags,
	 *  which makes the same ability dispatch policy work uniformly in squad
	 *  and selection brokers. */
	static bool CandidateHasTag(USeinWorldSubsystem* World, FSeinEntityHandle Candidate, FGameplayTag Tag)
	{
		return World && Tag.IsValid() && World->HasTag(Candidate, Tag);
	}

	/** Squad-broker leader semantic: the squad's authoritative Leader handle.
	 *  Non-squad brokers don't have a leader concept — the resolver caller
	 *  falls back to FirstAvailable when this returns invalid. */
	static FSeinEntityHandle GetLeaderForBroker(USeinWorldSubsystem* World, FSeinEntityHandle BrokerHandle)
	{
		if (!World) return FSeinEntityHandle();
		if (const FSeinSquadPayload* Squad = World->GetComponent<FSeinSquadPayload>(BrokerHandle))
		{
			return Squad->Leader;
		}
		return FSeinEntityHandle();
	}
}

void USeinCommandBrokerResolver::MarkDeterministicStateDirty(
	USeinWorldSubsystem* World)
{
	if (World)
	{
		World->MarkCommandBrokerResolverRuntimeStateDirty(this);
	}
}

TArray<FSeinEntityHandle> USeinCommandBrokerResolver::ApplyAbilityDispatchPolicy(
	USeinWorldSubsystem* World,
	FSeinEntityHandle BrokerHandle,
	const USeinAbility* Ability,
	const TArray<FSeinEntityHandle>& Candidates)
{
	// Null ability → behave as Mode::All. Matches "no policy = everyone fires"
	// historic semantic for legacy paths that haven't been migrated yet.
	if (!Ability)
	{
		return Candidates;
	}

	const TArray<FSeinEntityHandle> Sorted =
		SeinDispatchPolicyLocal::SortDispatchOrder(World, BrokerHandle, Candidates);

	switch (Ability->DispatchMode)
	{
	case ESeinAbilityDispatchMode::All:
	{
		return Sorted;
	}

	case ESeinAbilityDispatchMode::Single:
	{
		FSeinEntityHandle Chosen = FSeinEntityHandle();

		auto FindByPredicate = [&](TFunction<bool(FSeinEntityHandle)> Pred) -> FSeinEntityHandle
		{
			for (const FSeinEntityHandle& C : Sorted)
			{
				if (Pred(C)) return C;
			}
			return FSeinEntityHandle();
		};

		switch (Ability->DispatchSelector)
		{
		case ESeinAbilityDispatchSelector::Leader:
		{
			const FSeinEntityHandle Leader = SeinDispatchPolicyLocal::GetLeaderForBroker(World, BrokerHandle);
			if (Leader.IsValid() && Sorted.Contains(Leader))
			{
				Chosen = Leader;
			}
			break;
		}
		case ESeinAbilityDispatchSelector::ByTag:
		{
			if (Ability->DispatchPreferredTag.IsValid())
			{
				const FGameplayTag PreferredTag = Ability->DispatchPreferredTag;
				Chosen = FindByPredicate([&](FSeinEntityHandle C)
				{
					return SeinDispatchPolicyLocal::CandidateHasTag(World, C, PreferredTag);
				});
			}
			break;
		}
		case ESeinAbilityDispatchSelector::FirstAvailable:
		{
			if (Sorted.Num() > 0) { Chosen = Sorted[0]; }
			break;
		}
		}

		// Fallback when selector produced no candidate.
		if (!Chosen.IsValid())
		{
			switch (Ability->DispatchFallback)
			{
			case ESeinAbilityDispatchFallback::LeaderFirst:
			{
				const FSeinEntityHandle Leader = SeinDispatchPolicyLocal::GetLeaderForBroker(World, BrokerHandle);
				if (Leader.IsValid() && Sorted.Contains(Leader))
				{
					Chosen = Leader;
				}
				else if (Sorted.Num() > 0)
				{
					Chosen = Sorted[0];
				}
				break;
			}
			case ESeinAbilityDispatchFallback::FirstAvailable:
			{
				if (Sorted.Num() > 0) { Chosen = Sorted[0]; }
				break;
			}
			case ESeinAbilityDispatchFallback::Fail:
			{
				// Intentional empty set.
				break;
			}
			}
		}

		TArray<FSeinEntityHandle> Out;
		if (Chosen.IsValid()) { Out.Add(Chosen); }
		return Out;
	}

	case ESeinAbilityDispatchMode::ByTag:
	{
		TArray<FSeinEntityHandle> Out;
		if (!Ability->DispatchPreferredTag.IsValid()) return Out;
		const FGameplayTag PreferredTag = Ability->DispatchPreferredTag;
		for (const FSeinEntityHandle& C : Sorted)
		{
			if (SeinDispatchPolicyLocal::CandidateHasTag(World, C, PreferredTag))
			{
				Out.Add(C);
			}
		}
		return Out;
	}
	}

	return Sorted;
}

FGameplayTag USeinCommandBrokerResolver::ResolveMemberAbility_Implementation(
	USeinWorldSubsystem* World,
	FSeinEntityHandle Member,
	const FGameplayTagContainer& Context)
{
	if (!World) return FGameplayTag();
	const FSeinAbilityPayload* AC = World->GetComponent<FSeinAbilityPayload>(Member);
	if (!AC) return FGameplayTag();
	return AC->ResolveCommandContext(Context);
}

FSeinBrokerDispatchPlan USeinCommandBrokerResolver::ResolveDispatch_Implementation(
	USeinWorldSubsystem* /*World*/,
	FSeinEntityHandle /*BrokerHandle*/,
	const FSeinBrokerOrderInput& /*Order*/)
{
	// Abstract — subclasses provide a dispatch plan. Returning empty is safe
	// (the system will simply cull the broker when the order completes with
	// zero pending member dispatches).
	return FSeinBrokerDispatchPlan{};
}

TArray<FFixedVector> USeinCommandBrokerResolver::ResolvePositions_Implementation(
	USeinWorldSubsystem* /*World*/,
	const TArray<FSeinEntityHandle>& Members,
	FFixedVector Anchor,
	FFixedQuaternion /*Facing*/)
{
	TArray<FFixedVector> Out;
	Out.Init(Anchor, Members.Num());
	return Out;
}

FSeinFormationLayout USeinCommandBrokerResolver::ResolveFormationLayout_Implementation(
	USeinWorldSubsystem* World,
	const TArray<FSeinEntityHandle>& Members,
	const FSeinOrderTarget& Target,
	bool /*bReassignLateral*/,
	bool /*bReassignDepth*/)
{
	// Abstract base default: keep current facing, place every member at the
	// target anchor. Trivial — useful for very simple subclasses; default + squad
	// resolvers override with real layout logic.
	FSeinFormationLayout Layout;
	Layout.Facing = Target.CurrentFacing;
	Layout.Positions = ResolvePositions(World, Members, Target.Anchor, Target.CurrentFacing);
	return Layout;
}

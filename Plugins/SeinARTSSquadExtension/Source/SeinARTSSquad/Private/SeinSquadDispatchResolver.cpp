/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadDispatchResolver.cpp
 * @brief   Squad-aware broker resolver: leader-first predetermined dispatch.
 *          Slot-offset layout now lives in USeinSlotFormation (this resolver's
 *          DefaultFormationClass), not a ResolvePositions override.
 */

#include "SeinSquadDispatchResolver.h"
#include "Components/SeinSquadComponent.h"
#include "Components/SeinSquadMemberComponent.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinCommandBrokerData.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Types/Entity.h"
#include "Math/MathLib.h"
#include "SeinSlotFormation.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinSquadDispatch, Log, All);

USeinSquadDispatchResolver::USeinSquadDispatchResolver()
{
	// Squads lay out at authored per-slot offsets -> use the slot formation by
	// default. The base ctor already seeded FormationsByTag (e.g. Line), but squads
	// drop the gesture tag in ResolveDispatch, so this default always resolves --
	// squads stay slot-driven, behaving like today.
	DefaultFormationClass = USeinSlotFormation::StaticClass();
}

FSeinBrokerDispatchPlan USeinSquadDispatchResolver::ResolveDispatch_Implementation(
	USeinWorldSubsystem* World,
	FSeinEntityHandle BrokerHandle,
	const FSeinBrokerOrderInput& Order)
{
	FSeinBrokerDispatchPlan Plan;
	if (!World) return Plan;

	const TArray<FSeinEntityHandle>& Effective = Order.EffectiveMembers;
	if (Effective.Num() == 0) return Plan;

	UE_LOG(LogSeinSquadDispatch, Verbose,
		TEXT("ResolveDispatch: broker=%s, order target=%s, %d effective members, predetermined=%s"),
		*BrokerHandle.ToString(),
		*Order.TargetLocation.ToString(),
		Effective.Num(),
		Order.PredeterminedAbilityTag.IsValid() ? *Order.PredeterminedAbilityTag.ToString() : TEXT("<smart>"));

	// Predetermined-ability path: the player triggered a specific ability via
	// an action slot (or another trigger source). Apply the squad's per-tag
	// dispatch policy to pick the dispatcher set from the capability map.
	//
	// Capability-map already reflects the dedup rule (squad-owned wins over
	// member-owned for the same tag) â€” see RebuildCapabilityMap. The squad's
	// AbilityDispatchPolicy then refines "who actually fires it" based on
	// designer intent: All (default), Single + Selector, Subset + Filter.
	if (Order.PredeterminedAbilityTag.IsValid())
	{
		const FSeinCommandBrokerData* BrokerData = World->GetComponent<FSeinCommandBrokerData>(BrokerHandle);

		// Pull candidates from the capability map. Bucket may contain the
		// squad handle (squad-owned ability) and/or member handles
		// (member-owned). Member candidates must be intersected with
		// `Effective` (subset-targeted orders shouldn't dispatch to members
		// outside the subset); the squad handle is always allowed.
		TArray<FSeinEntityHandle> Candidates;
		if (BrokerData)
		{
			if (const FSeinBrokerCapabilityBucket* Bucket = BrokerData->CapabilityMap.Find(Order.PredeterminedAbilityTag))
			{
				for (const FSeinEntityHandle& C : Bucket->Members)
				{
					if (C == BrokerHandle || Effective.Contains(C))
					{
						Candidates.Add(C);
					}
				}
			}
		}

		// Look up the ability instance to read its dispatch policy fields.
		// Try the first capable candidate's AC (all instances of the same
		// ability class share the policy because it's CDO-level). Falls back
		// to null → ApplyAbilityDispatchPolicy degrades to Mode::All.
		const USeinAbility* Ability = nullptr;
		for (const FSeinEntityHandle& C : Candidates)
		{
			if (const FSeinAbilityComponent* AC = World->GetComponent<FSeinAbilityComponent>(C))
			{
				if (USeinAbility* Found = AC->FindAbilityByTag(*World, Order.PredeterminedAbilityTag))
				{
					Ability = Found;
					break;
				}
			}
		}

		const TArray<FSeinEntityHandle> Dispatchers = ApplyAbilityDispatchPolicy(
			World, BrokerHandle, Ability, Candidates);

		UE_LOG(LogSeinSquadDispatch, Verbose,
			TEXT("ResolveDispatch[predetermined=%s]: candidates=%d, ability-policy=%s, dispatchers=%d"),
			*Order.PredeterminedAbilityTag.ToString(),
			Candidates.Num(),
			Ability ? *UEnum::GetValueAsString(Ability->DispatchMode) : TEXT("<no-instance:All>"),
			Dispatchers.Num());

		for (const FSeinEntityHandle& Dispatcher : Dispatchers)
		{
			FSeinBrokerMemberDispatch MD;
			MD.Member = Dispatcher;
			MD.AbilityTag = Order.PredeterminedAbilityTag;
			MD.TargetEntity = Order.TargetEntity;
			MD.TargetLocation = Order.TargetLocation;
			MD.TargeterPoints = Order.TargeterPoints;
			Plan.MemberDispatches.Add(MD);
		}
		return Plan;
	}

	// Right-click smart-command path. Reimplemented here (rather than delegating
	// to Super) because the default resolver dispatches every member to
	// `Order.TargetLocation` for the primary path â€” clumping at the click point
	// â€” and only uses the per-member formation position in its TagAlongAbility
	// fallback. Squads with authored slot offsets need each member to head to
	// THEIR slot's world position, so we route MD.TargetLocation through our
	// ResolvePositions output.
	//
	// Exception: when Order.TargetEntity is valid (entity-targeted command â€”
	// attack a unit, repair a building, etc.), every member dispatches against
	// the same entity. Their own ability handles "get in range" â€” formation
	// positions don't apply because the target moves.
	//
	// Formation orientation: the formation always rotates so its forward axis aligns with the
	// direction from current centroid -> target (ComputeFormationFacing). The per-squad slot RE-MATCH
	// toggles (Reassign Slots Lateral / Depth) live on FSeinSquadComponent and are passed into
	// ResolveFormationLayout below -- the SAME entry point preview consumers call, so commit + preview
	// never drift. Both default OFF: authored slot roles stay pinned unless the designer opts in.
	FSeinCommandBrokerData* BrokerData = World->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
	const FSeinSquadComponent* SquadData = World->GetComponent<FSeinSquadComponent>(BrokerHandle);
	const FSeinEntity* SquadEntity = World->GetEntity(BrokerHandle);
	const FFixedVector CurrentCentroid = BrokerData ? BrokerData->Centroid
		: (SquadEntity ? SquadEntity->Transform.GetLocation() : FFixedVector::ZeroVector);
	const FFixedQuaternion CurrentFacing = BrokerData ? BrokerData->AnchorFacing : FFixedQuaternion::Identity;
	const bool bReassignLateral = SquadData ? SquadData->bReassignSlotsLateral : false;
	const bool bReassignDepth   = SquadData ? SquadData->bReassignSlotsDepth   : false;

	// A squad is ONE element of the parent formation: the parent (ComputeMultiBrokerAnchors) already spent
	// the gesture SPACING the squad anchors and handed this squad its anchor (Order.TargetLocation) +
	// element facing (CurrentFacing). Lay the members out in the squad's OWN compact shape via the shared
	// inner-layout constructor, which by construction carries NO gesture guide/tag — a guide here would
	// re-expand each squad to fill the whole drag, overlapping them into one. EMPTY FormationClass = the
	// slot formation (this resolver's DefaultFormationClass). SAME constructor the preview uses → preview
	// === commit. (TargetEntity isn't needed for layout; the per-member dispatch reads Order.TargetEntity.)
	const FSeinOrderTarget Target = USeinFormation::MakeInnerLayoutTarget(
		Order.TargetLocation, CurrentCentroid, CurrentFacing,
		SquadData ? SquadData->FormationClass : TSoftClassPtr<USeinFormation>());
	const FSeinFormationLayout Layout = ResolveFormationLayout(
		World, Effective, Target, bReassignLateral, bReassignDepth);
	const FFixedQuaternion FormationFacing = Layout.Facing;
	const TArray<FFixedVector>& Positions = Layout.Positions;

	// Persist the formation facing on the broker so the next move's
	// "current facing" lookup reflects this dispatch.
	if (BrokerData)
	{
		BrokerData->AnchorFacing = FormationFacing;
	}

	const bool bEntityTargeted = Order.TargetEntity.IsValid();

	Plan.MemberDispatches.Reserve(Effective.Num());

	for (int32 i = 0; i < Effective.Num(); ++i)
	{
		const FSeinEntityHandle Member = Effective[i];
		const FSeinAbilityComponent* AC = World->GetComponent<FSeinAbilityComponent>(Member);
		if (!AC) continue;

		const FFixedVector SlotGoal = Positions.IsValidIndex(i) ? Positions[i] : Order.TargetLocation;

		const FGameplayTag ResolvedTag = ResolveMemberAbility(World, Member, Order.Context);

		UE_LOG(LogSeinSquadDispatch, Verbose,
			TEXT("  member[%d] %s â†’ resolved=%s, target=%s, location=%s"),
			i, *Member.ToString(),
			ResolvedTag.IsValid() ? *ResolvedTag.ToString() : TEXT("<none>"),
			bEntityTargeted ? TEXT("entity") : TEXT("slot"),
			*(bEntityTargeted ? Order.TargetLocation : SlotGoal).ToString());

		if (ResolvedTag.IsValid() && AC->HasAbilityWithTag(*World, ResolvedTag))
		{
			FSeinBrokerMemberDispatch MD;
			MD.Member = Member;
			MD.AbilityTag = ResolvedTag;
			MD.TargetEntity = Order.TargetEntity;
			MD.TargetLocation = bEntityTargeted ? Order.TargetLocation : SlotGoal;
			Plan.MemberDispatches.Add(MD);
			continue;
		}

		// Tag-along fallback â€” non-combatants tagging along with an attack order
		// always target their formation slot, even on entity-targeted commands.
		if (TagAlongAbility.IsValid() && AC->HasAbilityWithTag(*World, TagAlongAbility))
		{
			FSeinBrokerMemberDispatch MD;
			MD.Member = Member;
			MD.AbilityTag = TagAlongAbility;
			MD.TargetLocation = SlotGoal;
			Plan.MemberDispatches.Add(MD);
			continue;
		}
		// else: no capable ability; member stays idle for this order.
	}

	return Plan;
}


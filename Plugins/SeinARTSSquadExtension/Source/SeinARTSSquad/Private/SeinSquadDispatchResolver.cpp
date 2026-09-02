/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadDispatchResolver.cpp
 * @brief   Squad-aware broker resolver: leader-first predetermined dispatch.
 *          Slot-offset layout now lives in USeinSlotFormation (this resolver's
 *          DefaultFormationClass), not a ResolvePositions override.
 */

#include "SeinSquadDispatchResolver.h"
#include "Components/SeinSquadPayload.h"
#include "Components/SeinSquadMemberPayload.h"
#include "Components/SeinAbilityPayload.h"
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
			if (const FSeinAbilityPayload* AC = World->GetComponent<FSeinAbilityPayload>(C))
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
	// toggles (Reassign Slots Lateral / Depth) live on FSeinSquadPayload and are passed into
	// ResolveFormationLayout below -- the SAME entry point preview consumers call, so commit + preview
	// never drift. Both default OFF: authored slot roles stay pinned unless the designer opts in.
	FSeinCommandBrokerData* BrokerData = World->GetComponentMutable<FSeinCommandBrokerData>(BrokerHandle);
	const FSeinSquadPayload* SquadData = World->GetComponent<FSeinSquadPayload>(BrokerHandle);
	const FSeinEntity* SquadEntity = World->GetEntity(BrokerHandle);
	const bool bHadBrokerData = BrokerData != nullptr;
	const FSeinPlayerID BrokerOwner = bHadBrokerData
		? World->GetEntityOwner(BrokerHandle) : FSeinPlayerID::Neutral();
	const int32 BrokerResolverID = bHadBrokerData ? BrokerData->ResolverID : INDEX_NONE;
	const TArray<FSeinEntityHandle> BrokerMembers = bHadBrokerData
		? BrokerData->Members : TArray<FSeinEntityHandle>();
	const FFixedVector CurrentCentroid = BrokerData ? BrokerData->Centroid
		: (SquadEntity ? SquadEntity->Transform.GetLocation() : FFixedVector::ZeroVector);
	const FFixedQuaternion CurrentFacing = BrokerData ? BrokerData->AnchorFacing : FFixedQuaternion::Identity;
	const bool bReassignLateral = SquadData ? SquadData->bReassignSlotsLateral : false;
	const bool bReassignDepth   = SquadData ? SquadData->bReassignSlotsDepth   : false;

	const bool bEntityTargeted = Order.TargetEntity.IsValid();

	// PRE-PLACED FIDELITY PATH (idle re-form + any internal machinery that dispatches
	// members to exact points): when the order carries member→position pairs, route each
	// listed member STRAIGHT to its paired world position and skip the inner layout solve
	// entirely. Re-solving here would be actively destructive for a SCATTERED squad: the
	// facing recomputes from centroid→anchor (a scattered centroid sits off to the side, so
	// every dispatch rotates the slot offsets to a fresh wrong orientation) and the
	// AnchorFacing write below would overwrite the squad's standing facing with that junk —
	// corrupting the very layout a re-form is trying to return to. A pre-placed dispatch
	// returns to the layout AS GIVEN: no re-solve, no facing recompute, no state writes.
	TArray<FFixedVector> Positions;
	FFixedQuaternion FormationFacing = CurrentFacing;
	if (Order.DestinationArtifact.Num() > 0)
	{
		Positions.Reserve(Effective.Num());
		for (const FSeinEntityHandle& Member : Effective)
		{
			const FSeinFrozenDestination* Frozen =
				Order.DestinationArtifact.FindByPredicate(
					[Member](const FSeinFrozenDestination& Entry)
					{
						return Entry.Member == Member;
					});
			Positions.Add(Frozen ? Frozen->WorldPosition : Order.TargetLocation);
		}
		if (BrokerData && !bEntityTargeted && Positions.Num() > 0
			&& Effective.Num() == BrokerMembers.Num())
		{
			Plan.bApplySettledSlots = true;
			Plan.SettledSlotPositions = Positions;
			Plan.SettledSlotFacings.Init(CurrentFacing, Positions.Num());
			Plan.bSettledSlotsMemberAligned = true;
		}
	}
	else if (Order.PreplacedPositions.Num() > 0)
	{
		Positions.Reserve(Effective.Num());
		for (const FSeinEntityHandle& Member : Effective)
		{
			const int32 Pidx = Order.PreplacedMembers.IndexOfByKey(Member);
			Positions.Add(Order.PreplacedPositions.IsValidIndex(Pidx)
				? Order.PreplacedPositions[Pidx] : Order.TargetLocation);
		}
	}
	else
	{
		// A squad is ONE element of the parent formation: the parent (ComputeMultiBrokerAnchors)
		// already spent the gesture SPACING the squad anchors and handed this squad its anchor
		// (Order.TargetLocation) + element facing (CurrentFacing). Lay the members out in the
		// squad's OWN compact shape via the shared inner-layout constructor, which by
		// construction carries NO gesture guide/tag — a guide here would re-expand each squad to
		// fill the whole drag, overlapping them into one. EMPTY FormationClass = the slot
		// formation (this resolver's DefaultFormationClass). SAME constructor the preview uses →
		// preview === commit. (TargetEntity isn't needed for layout; the per-member dispatch
		// reads Order.TargetEntity.)
		const FSeinOrderTarget Target = USeinFormation::MakeInnerLayoutTarget(
			Order.TargetLocation, CurrentCentroid, CurrentFacing,
			SquadData ? SquadData->FormationClass : TSoftClassPtr<USeinFormation>());

		// Formation layout reaches designer-pluggable formation and post-process
		// hooks. Release component references before they can grow storage.
		BrokerData = nullptr;
		SquadData = nullptr;
		SquadEntity = nullptr;
		const FSeinFormationLayout Layout = ResolveFormationLayout(
			World, Effective, Target, bReassignLateral, bReassignDepth);

		if (bHadBrokerData)
		{
			BrokerData = World->GetComponentMutable<FSeinCommandBrokerData>(
				BrokerHandle);
			if (!World->IsEntityAlive(BrokerHandle) || !BrokerData
				|| World->GetEntityOwner(BrokerHandle) != BrokerOwner
				|| BrokerData->ResolverID != BrokerResolverID
				|| BrokerData->Centroid != CurrentCentroid
				|| BrokerData->Members != BrokerMembers)
			{
				// The broker system's outer precondition rejects this stale plan.
				return Plan;
			}
		}
		FormationFacing = Layout.Facing;
		Positions = Layout.Positions;

		if (BrokerData)
		{
			Plan.bApplyAnchorFacing = true;
			Plan.AnchorFacing = FormationFacing;
		}

		// Return the resolved layout for transactional broker-system commit.
		// Mirrors the default resolver's capture, with one squad-specific difference:
		// MEMBER-ALIGNED. Squads have AUTHORED slot roles (the re-match toggles default OFF, so
		// slot i belongs to member i of this dispatch), and a re-form must send each member back
		// to ITS OWN slot rather than re-shuffling the roster. Full-squad GROUND orders only:
		// entity-targeted dispatches carry no slots, and subset/pre-placed orders must not
		// clobber the squad's standing layout with a partial or re-routed one.
		if (BrokerData && !bEntityTargeted && Positions.Num() > 0
			&& Effective.Num() == BrokerMembers.Num())
		{
			TArray<FFixedQuaternion> SlotFacings = Layout.Facings;
			while (SlotFacings.Num() < Positions.Num()) { SlotFacings.Add(FormationFacing); }
			SlotFacings.SetNum(Positions.Num());
			Plan.bApplySettledSlots = true;
			Plan.SettledSlotPositions = Positions;
			Plan.SettledSlotFacings = MoveTemp(SlotFacings);
			Plan.bSettledSlotsMemberAligned = true;
		}
	}

	Plan.MemberDispatches.Reserve(Effective.Num());

	for (int32 i = 0; i < Effective.Num(); ++i)
	{
		const FSeinEntityHandle Member = Effective[i];
		const FFixedVector SlotGoal = Positions.IsValidIndex(i) ? Positions[i] : Order.TargetLocation;
		if (!World->GetComponent<FSeinAbilityPayload>(Member)) continue;

		const FGameplayTag ResolvedTag = ResolveMemberAbility(World, Member, Order.Context);

		// ResolveMemberAbility is Blueprint-pluggable and may reallocate or
		// replace component storage. Reacquire only after the callback returns.
		const FSeinAbilityPayload* AC = World->GetComponent<FSeinAbilityPayload>(Member);
		if (!AC) continue;

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


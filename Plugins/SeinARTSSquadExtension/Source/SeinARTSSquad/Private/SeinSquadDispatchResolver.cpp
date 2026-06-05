/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadDispatchResolver.cpp
 * @brief   Squad-aware broker resolver: leader-first predetermined dispatch
 *          + slot-offset formation positions.
 */

#include "SeinSquadDispatchResolver.h"
#include "Components/SeinSquadComponent.h"
#include "Components/SeinSquadMemberComponent.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinCommandBrokerData.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Types/Entity.h"
#include "Math/MathLib.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinSquadDispatch, Log, All);

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
	// Formation orientation: by default, the formation rotates so its forward
	// axis aligns with the direction from current centroid â†’ target. With
	// the squad's `bInvertSlotOrderWhenMovingBackward` flag enabled and the
	// move heading roughly opposite the squad's current facing, we KEEP the
	// current facing â€” the squad walks backwards and slot offsets stay in
	// their current world orientation. CoH-style natural-feel. Toggle is on
	// FSeinSquadComponent (per-squad designer toggle), NOT the resolver. Logic
	// lives in ResolveFormationLayout â€” same entry point preview consumers
	// call, so commit + preview never drift.
	FSeinCommandBrokerData* BrokerData = World->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
	const FSeinSquadComponent* SquadData = World->GetComponent<FSeinSquadComponent>(BrokerHandle);
	const FSeinEntity* SquadEntity = World->GetEntity(BrokerHandle);
	const FFixedVector CurrentCentroid = BrokerData ? BrokerData->Centroid
		: (SquadEntity ? SquadEntity->Transform.GetLocation() : FFixedVector::ZeroVector);
	const FFixedQuaternion CurrentFacing = BrokerData ? BrokerData->AnchorFacing : FFixedQuaternion::Identity;
	const bool bInvertWhenBackward = SquadData ? SquadData->bInvertSlotOrderWhenMovingBackward : false;

	const FSeinFormationLayout Layout = ResolveFormationLayout(
		World, Effective, CurrentCentroid, CurrentFacing,
		Order.TargetLocation, bInvertWhenBackward);
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

TArray<FFixedVector> USeinSquadDispatchResolver::ResolvePositions_Implementation(
	USeinWorldSubsystem* World,
	const TArray<FSeinEntityHandle>& Members,
	FFixedVector Anchor,
	FFixedQuaternion Facing)
{
	TArray<FFixedVector> Out;
	Out.Reserve(Members.Num());

	if (!World)
	{
		// Fallback to parent's grid layout if no world (defensive â€” shouldn't happen).
		return Super::ResolvePositions_Implementation(World, Members, Anchor, Facing);
	}

	// Per member: look up its slot's OffsetTransform via SquadEntity â†’ FSeinSquadComponent.
	// Members without a resolvable slot fall back to the parent's grid position
	// for that index (so a squad mid-tear-down still produces coherent output).
	//
	// Two diagnostic counters tracked alongside `bAnyFallback`:
	// `bAnyAuthoredOffset` flips true the moment we read a non-zero LocalOffset
	// from any slot, indicating the designer DID author offsets. If after the
	// loop `bAnyAuthoredOffset` is false AND every member resolved its slot
	// successfully, we know the data path is correct but every authored
	// offset happens to be zero â€” clearly an unauthored squad. In that case
	// we override Out wholesale with the parent grid layout so unauthored
	// squads get a sensible default formation instead of converging on the
	// anchor. Authored squads (any non-zero offset) always pass through.
	bool bAnyFallback = false;
	bool bAnyAuthoredOffset = false;
	int32 SlotLookupFailures = 0;
	TArray<int32> FallbackIndices;

	for (int32 i = 0; i < Members.Num(); ++i)
	{
		const FSeinEntityHandle Member = Members[i];
		const FSeinSquadMemberComponent* MemberData = World->GetComponent<FSeinSquadMemberComponent>(Member);
		if (!MemberData || !MemberData->SquadEntity.IsValid())
		{
			Out.Add(Anchor);            // placeholder â€” replaced below if grid fallback fires
			FallbackIndices.Add(i);
			bAnyFallback = true;
			++SlotLookupFailures;
			UE_LOG(LogSeinSquadDispatch, Verbose,
				TEXT("ResolvePositions: member %s has no SquadMemberData / invalid SquadEntity â€” using grid fallback"),
				*Member.ToString());
			continue;
		}

		const FSeinSquadComponent* Squad = World->GetComponent<FSeinSquadComponent>(MemberData->SquadEntity);
		if (!Squad)
		{
			Out.Add(Anchor);
			FallbackIndices.Add(i);
			bAnyFallback = true;
			++SlotLookupFailures;
			UE_LOG(LogSeinSquadDispatch, Verbose,
				TEXT("ResolvePositions: member %s points at squad %s but squad has no FSeinSquadComponent â€” using grid fallback"),
				*Member.ToString(), *MemberData->SquadEntity.ToString());
			continue;
		}

		// Prefer SlotIndex (canonical identity, always unique per array
		// position) over tag-based lookup. SlotTag may be shared across
		// multiple slots (e.g. five rifleman slots all tagged
		// `Squad.Slot.Rifleman`), in which case `IndexOfSlotByTag` returns
		// the FIRST match â€” collapsing every member to slot 0's offset.
		// Falls back to tag lookup for legacy data (SlotIndex INDEX_NONE).
		int32 SlotIdx = MemberData->SlotIndex;
		if (SlotIdx == INDEX_NONE || !Squad->Slots.IsValidIndex(SlotIdx))
		{
			SlotIdx = Squad->IndexOfSlotByTag(MemberData->SlotTag);
		}
		if (SlotIdx == INDEX_NONE)
		{
			Out.Add(Anchor);
			FallbackIndices.Add(i);
			bAnyFallback = true;
			++SlotLookupFailures;
			UE_LOG(LogSeinSquadDispatch, Verbose,
				TEXT("ResolvePositions: member %s no valid slot (SlotIndex=%d, SlotTag='%s') in squad %s â€” using grid fallback"),
				*Member.ToString(),
				MemberData->SlotIndex,
				*MemberData->SlotTag.ToString(),
				*MemberData->SquadEntity.ToString());
			continue;
		}

		const FFixedVector LocalOffset = Squad->Slots[SlotIdx].OffsetTransform.GetLocation();
		if (!LocalOffset.IsNearlyZero())
		{
			bAnyAuthoredOffset = true;
		}
		const FFixedVector WorldOffset = Facing.RotateVector(LocalOffset);
		FFixedVector SlotPos = Anchor + WorldOffset;

		// Project slot to passable nav cell â€” same rationale as the default
		// resolver. Without this, squad formations spreading off raised
		// platforms or past nav volume edges land on impassable terrain.
		if (World->NavProjectResolver.IsBound())
		{
			FFixedVector Projected;
			if (World->NavProjectResolver.Execute(SlotPos, Projected))
			{
				SlotPos = Projected;
			}
			else
			{
				SlotPos = Anchor;
			}
		}

		Out.Add(SlotPos);
	}

	// Fallback case 1: per-member slot resolution failed for some members
	// (squad mid-teardown, member missing component, tag mismatch). Splice
	// in the parent's grid positions for those specific indices.
	if (bAnyFallback)
	{
		const TArray<FFixedVector> Grid = Super::ResolvePositions_Implementation(World, Members, Anchor, Facing);
		for (int32 Idx : FallbackIndices)
		{
			if (Grid.IsValidIndex(Idx)) { Out[Idx] = Grid[Idx]; }
		}
	}

	// Fallback case 2: every member resolved its slot successfully BUT every
	// authored offset is identity (zero). This is the "designer made a squad
	// but didn't author per-slot transforms" case â€” without this fallback,
	// every member would converge on the anchor (because every offset is
	// zero). Replace the whole output with the parent grid so unauthored
	// squads spread sensibly. Authored squads (any non-zero offset) skip
	// this branch and use their authored data.
	if (!bAnyAuthoredOffset && SlotLookupFailures == 0 && Members.Num() > 1)
	{
		UE_LOG(LogSeinSquadDispatch, Verbose,
			TEXT("ResolvePositions: all %d slot offsets are identity â€” falling back to grid layout. "
			     "Author per-slot OffsetTransform on FSeinSquadComponent::Slots to get formation-specific layout."),
			Members.Num());
		Out = Super::ResolvePositions_Implementation(World, Members, Anchor, Facing);
	}

	return Out;
}

FSeinFormationLayout USeinSquadDispatchResolver::ResolveFormationLayout_Implementation(
	USeinWorldSubsystem* World,
	const TArray<FSeinEntityHandle>& Members,
	FFixedVector CurrentCentroid,
	FFixedQuaternion CurrentFacing,
	FFixedVector TargetLocation,
	bool bInvertWhenBackward)
{
	// Facing computation lives on the parent â€” single source of truth shared
	// between commit + preview paths.
	const FSeinFormationFacing FacingResult = ComputeFormationFacing(
		CurrentCentroid, CurrentFacing, TargetLocation, bInvertWhenBackward);

	FSeinFormationLayout Layout;
	Layout.Facing = FacingResult.Facing;
	Layout.bAntiCrossReorder = FacingResult.bAntiCrossReorder;
	Layout.Positions = ResolvePositions(World, Members, TargetLocation, FacingResult.Facing);

	// Slot-mirror for backward walk â€” only meaningful when authored slot offsets
	// are asymmetric (the squad has a real "front row vs back row"). The
	// default-grid fallback inside ResolvePositions is symmetric, so mirroring
	// is a no-op for the unauthored case; we apply unconditionally for the
	// backward-walk path to keep this branch simple.
	// Now uses the SHARED, deterministic base implementation (ReassignSlotsByAxisProjection); the
	// non-squad path runs the same match unconditionally, while the squad keeps it gated on the
	// authored-role opt-in so a hard reverse re-ranks instead of crossing.
	if (FacingResult.bAntiCrossReorder)
	{
		ReassignSlotsByAxisProjection(World, Members, Layout.Positions, FacingResult.Facing);
	}

	// Hook subclasses (cover-aware squad resolver, etc.) to mutate positions
	// after geometry + slot-mirror. Empty default impl on the base â€” no-op
	// for non-overriding subclasses. Runs AFTER the mirror so cover-snap sees
	// the final geometric positions.
	PostProcessPositions(World, Members, Layout.Positions, TargetLocation);

	return Layout;
}

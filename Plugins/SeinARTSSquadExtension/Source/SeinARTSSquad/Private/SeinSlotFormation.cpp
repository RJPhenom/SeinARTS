/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSlotFormation.cpp
 * @brief   Per-slot authored-offset layout (ported from the squad dispatch
 *          resolver's ResolvePositions override).
 */

#include "SeinSlotFormation.h"
#include "Components/SeinSquadPayload.h"
#include "Components/SeinSquadMemberPayload.h"
#include "Simulation/SeinWorldSubsystem.h"

FSeinFormationLayout USeinSlotFormation::BuildFormation_Implementation(
	USeinWorldSubsystem* World,
	const TArray<FSeinEntityHandle>& Members,
	const FSeinOrderTarget& Target)
{
	FSeinFormationLayout Layout;
	const int32 N = Members.Num();

	// Facing: a right-click-drag ORIENTS the squad. It faces the drag perpendicular by
	// fixed handedness (USeinFormation::DragFacingDir); the drag DIRECTION is the sole
	// authority, so the squad's own position/centroid does NOT influence it and every
	// squad in a multi-squad drag faces alike. No drag: keep the move-target facing.
	// Facing is handed DOWN by the parent formation (the squad is ONE element in it): the parent
	// already resolved this squad's facing for its slot — radial in a ring, drag-perpendicular in a
	// box, move-direction on a plain click — and delivered it via Target.CurrentFacing. Use it directly
	// and rotate the squad's whole authored body to match. (Pre-B this formation self-computed the drag
	// perpendicular; that decision now lives one level up so squads orient to the FORMATION, not just
	// the raw drag.)
	Layout.Facing = Target.CurrentFacing;

	// Emit footprint radii for preview dot sizing. Slot POSITIONS stay the designer's
	// authored offsets — the spacing is authored, not footprint-derived.
	GatherFootprintRadii(World, Members, Layout.Radii);

	if (N == 0 || !World)
	{
		Layout.Positions.Init(Target.Anchor, N);
		return Layout;
	}

	const FFixedVector Anchor = Target.Anchor;
	const FFixedQuaternion Facing = Layout.Facing;
	Layout.Positions.Reserve(N);

	// Per member: resolve its slot via SquadEntity -> FSeinSquadPayload, take the
	// authored OffsetTransform (preferring the canonical SlotIndex; SlotTag fallback
	// for legacy data — a SHARED tag would collapse members onto slot 0), rotate by
	// facing, translate by the anchor, nav-project. Members whose slot can't be
	// resolved get the anchor (a blob placeholder).
	bool bAnyAuthoredOffset = false;
	int32 SlotLookupFailures = 0;
	for (int32 i = 0; i < N; ++i)
	{
		const FSeinEntityHandle Member = Members[i];
		const FSeinSquadMemberPayload* MemberData = World->GetComponent<FSeinSquadMemberPayload>(Member);
		if (!MemberData || !MemberData->SquadEntity.IsValid())
		{
			Layout.Positions.Add(Anchor);
			++SlotLookupFailures;
			continue;
		}

		const FSeinSquadPayload* Squad = World->GetComponent<FSeinSquadPayload>(MemberData->SquadEntity);
		if (!Squad)
		{
			Layout.Positions.Add(Anchor);
			++SlotLookupFailures;
			continue;
		}

		int32 SlotIdx = MemberData->SlotIndex;
		if (SlotIdx == INDEX_NONE || !Squad->Slots.IsValidIndex(SlotIdx))
		{
			SlotIdx = Squad->IndexOfSlotByTag(MemberData->SlotTag);
		}
		if (SlotIdx == INDEX_NONE)
		{
			Layout.Positions.Add(Anchor);
			++SlotLookupFailures;
			continue;
		}

		const FFixedVector LocalOffset = Squad->Slots[SlotIdx].OffsetTransform.GetLocation();
		if (!LocalOffset.IsNearlyZero())
		{
			bAnyAuthoredOffset = true;
		}
		const FFixedVector WorldOffset = Facing.RotateVector(LocalOffset);
		Layout.Positions.Add(ProjectToNavigable(World, Anchor + WorldOffset, Anchor));
	}

	// Unauthored squad: every member resolved a slot but every offset is identity.
	// Blob at the anchor (matches pre-refactor: the squad fell back to the base
	// resolver's blob). Author per-slot OffsetTransforms on FSeinSquadPayload::Slots
	// for a real layout.
	if (!bAnyAuthoredOffset && SlotLookupFailures == 0 && N > 1)
	{
		Layout.Positions.Init(Anchor, N);
	}

	return Layout;
}

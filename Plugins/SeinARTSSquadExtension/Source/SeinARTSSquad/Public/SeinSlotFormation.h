/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSlotFormation.h
 * @brief   Formation that places members at their squad's authored per-slot
 *          OffsetTransforms (FSeinSquadComponent::Slots), rotated by the formation
 *          facing and nav-projected.
 *
 *          The squad layout model, expressed as a USeinFormation so squads are
 *          just another formation consumer (ported out of the squad dispatch
 *          resolver's old ResolvePositions override). The squad dispatch resolver
 *          selects this as its DefaultFormationClass. Members whose slot can't be
 *          resolved, or an entirely unauthored squad (all-identity offsets), fall
 *          back to a blob at the anchor (matches the pre-refactor behaviour).
 */

#pragma once

#include "CoreMinimal.h"
#include "Formations/SeinFormation.h"
#include "SeinSlotFormation.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Slot Formation"))
class SEINARTSSQUAD_API USeinSlotFormation : public USeinFormation
{
	GENERATED_BODY()

public:
	virtual FSeinFormationLayout BuildFormation_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target) override;

	/** The slot formation IS the authored-slot-offset layout — so a squad using it shows its per-slot
	 *  OffsetTransform authoring (the editor's Details customization keys off this). */
	virtual bool UsesAuthoredSlotOffsets_Implementation() const override { return true; }
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinGridFormation.h
 * @brief   Grid formation — a uniform square-ish grid centered on the anchor,
 *          rotated to face the move direction, each slot nav-projected.
 *
 *          What the removed `bFormationSpreadEnabled = true` used to produce; now
 *          an opt-in formation a project selects on the broker resolver. The
 *          anti-cross slot re-match still runs afterward in the resolver.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Formations/SeinFormation.h"
#include "SeinGridFormation.generated.h"

/**
 * Arranges a selection into a tidy rectangular block when you order them to move. Units fill a
 * square-ish grid of evenly spaced slots, centered on where you clicked and rotated so the block
 * faces the direction of travel.
 *
 * This is a GRID formation shape: it lays out one slot per member on a uniform lattice sized to be
 * as close to square as the member count allows (roughly equal rows and columns), spaces slots by
 * Inter Unit Spacing (in UE world units / centimeters), and rotates the whole grid to face the move
 * direction. Every slot is projected onto walkable navigation so nobody is placed inside a wall or
 * off the grid. This reproduces the layout the retired automatic formation-spread once produced, but
 * as an opt-in shape a project selects on the command broker's resolver. After this shape emits its
 * slots, the resolver still runs its anti-cross slot re-match pass so units claim the nearest slot
 * and their paths do not needlessly cross.
 */
UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Grid Formation"))
class SEINARTSCOREENTITY_API USeinGridFormation : public USeinFormation
{
	GENERATED_BODY()

public:
	virtual bool IsStatelessExecutionAdmitted(FString& OutError) const override
	{
		return AdmitStatelessNativeAnchor(StaticClass(), OutError);
	}

	/** World-space spacing between units in the grid. Scale in UE world units (cm).
	 *  150 ≈ one infantryman's personal-space radius. (Moved here from the default
	 *  broker resolver's InterUnitSpacing.) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Inter Unit Spacing"))
	FFixedPoint InterUnitSpacing = FFixedPoint::FromInt(150);

	virtual FSeinFormationLayout BuildFormation_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target) override;
};

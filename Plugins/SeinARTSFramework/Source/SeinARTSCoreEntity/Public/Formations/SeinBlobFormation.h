/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBlobFormation.h
 * @brief   Blob formation — every member shares the one destination anchor.
 *
 *          The framework default (preserves the historic single-destination move:
 *          the AoE/SC2/CoH model where the hard collision floor packs units into a
 *          no-overlap cluster on arrival). This is what the removed
 *          `bFormationSpreadEnabled = false` used to produce. Inherits the base
 *          blob layout unchanged — no overrides needed.
 */

#pragma once

#include "CoreMinimal.h"
#include "Formations/SeinFormation.h"
#include "SeinBlobFormation.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Blob Formation"))
class SEINARTSCOREENTITY_API USeinBlobFormation : public USeinFormation
{
	GENERATED_BODY()

	// Intentionally empty: the base USeinFormation default layout IS the blob
	// (every member → Target.Anchor). Exists as a concrete, designer-pickable class.
};

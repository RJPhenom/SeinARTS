/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLevelDataAsset.h
 * @brief   Abstract polymorphic baked asset for the unified level-data substrate (CP1.1).
 *
 *          Concrete substrates produce their own subclass (e.g. USeinLevelDataDefaultAsset)
 *          holding the channel-extensible per-cell data — shared height + in-bounds mask +
 *          per-layer channel blocks tagged by layer + resolution (planning/Decisions.md D15).
 *          A minimal polymorphic anchor. ASeinLevelVolume holds a soft reference to one;
 *          the substrate loads from it. Keeping the base minimal lets the format stay
 *          impl-specific + extensible.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SeinLevelDataAsset.generated.h"

UCLASS(Abstract, BlueprintType)
class SEINARTSLEVELDATA_API USeinLevelDataAsset : public UDataAsset
{
	GENERATED_BODY()
};

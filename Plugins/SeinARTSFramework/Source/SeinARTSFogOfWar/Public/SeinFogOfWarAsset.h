/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarAsset.h
 * @brief   Abstract base for baked fog-of-war grid data. Each USeinFogOfWar
 *          subclass owns a matching USeinFogOfWarAsset subclass that holds
 *          its serialized bake output (grid dims, per-cell ground + blocker
 *          heights, per-layer static blocker masks, etc.).
 *
 *          ASeinFogOfWarVolume stores a polymorphic `TObjectPtr<USeinFogOfWarAsset>`
 *          reference; the active fog-of-war class casts to its own concrete
 *          type on load.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SeinFogOfWarAsset.generated.h"

UCLASS(Abstract, BlueprintType, meta = (DisplayName = "Sein Fog Of War Asset"))
class SEINARTSFOGOFWAR_API USeinFogOfWarAsset : public UDataAsset
{
	GENERATED_BODY()
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationAsset.h
 * @brief   Abstract base for baked navigation data. Each USeinNavigation subclass
 *          owns a matching USeinNavigationAsset subclass that holds its serialized
 *          representation (grid cells, waypoint graph, nav-mesh triangles, etc.).
 *
 *          ASeinNavVolume stores a polymorphic `TObjectPtr<USeinNavigationAsset>`
 *          reference; the active nav class casts it to its own concrete type on
 *          load.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SeinNavigationAsset.generated.h"

UCLASS(Abstract, BlueprintType, meta = (DisplayName = "Sein Navigation Asset"))
class SEINARTSNAVIGATION_API USeinNavigationAsset : public UDataAsset
{
	GENERATED_BODY()
};

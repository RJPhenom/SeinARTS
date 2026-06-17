/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLayerConfig.h
 * @brief   Base for a custom bake-layer's PER-VOLUME config, edited inline on
 *          ASeinLevelVolume.
 *
 *          A third-party bake-layer system (an ISeinLevelLayerProvider — like the
 *          shipped Nav / Fog providers, but custom: a threat/influence map, a
 *          sound-propagation map, a custom nav/vision variant) subclasses this,
 *          registers the subclass via ASeinLevelVolume::RegisterLayerConfigClass
 *          at module startup, and reads it at bake via ASeinLevelVolume::GetLayerConfig.
 *          This is the NO-FORK path for PER-VOLUME layer settings; GLOBAL config
 *          still belongs in USeinARTSCoreSettings.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinLayerConfig.generated.h"

UCLASS(Abstract, EditInlineNew, Blueprintable, CollapseCategories, meta = (DisplayName = "Sein Layer Config"))
class SEINARTSLEVELDATA_API USeinLayerConfig : public UObject
{
	GENERATED_BODY()
};

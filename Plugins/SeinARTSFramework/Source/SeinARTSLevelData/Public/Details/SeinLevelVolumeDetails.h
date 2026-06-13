/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLevelVolumeDetails.h
 * @brief   Details panel for ASeinLevelVolume.
 *
 *          Builds the "Bake" sub-group under the shared "SeinARTS" category
 *          (alongside Navigation / Fog Of War): the "Bake Level Data" button
 *          just above the BakedAsset output. The button can't be a plain
 *          CallInEditor UFUNCTION because that forces its category through
 *          EditCategory, which pulls an "A|B" name out of the sub-category
 *          nesting and renders it as a detached top-level "SeinARTS|Bake"
 *          header — so the volume declares BakeLevelData as a plain method and
 *          this customization draws the button inside the nested group.
 */

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

#include "IDetailCustomization.h"

class IDetailLayoutBuilder;

class FSeinLevelVolumeDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};

#endif // WITH_EDITOR

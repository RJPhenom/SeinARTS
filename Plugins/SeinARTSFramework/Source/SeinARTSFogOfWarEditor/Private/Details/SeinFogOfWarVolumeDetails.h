/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarVolumeDetails.h
 * @brief   Detail customization for ASeinFogOfWarVolume — adds the "Bake Fog
 *          Of War" button to the volume's details panel and forwards to the
 *          active USeinFogOfWar subclass via `CustomizeVolumeDetails` so
 *          impls can extend the panel with their own rows.
 *
 *          Lives in SeinARTSFogOfWarEditor (the editor companion to the
 *          SeinARTSFogOfWar Runtime module) so the editor-side registration
 *          happens at PostEngineInit phase — after SeinARTSEditor + the
 *          PropertyEditor module are both ready.
 */

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class FSeinFogOfWarVolumeDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	TWeakObjectPtr<class UWorld> CachedWorld;

	FReply OnBakeClicked();
	bool IsBakeEnabled() const;
};

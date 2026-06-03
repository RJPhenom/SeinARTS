/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavVolumeDetails.h
 * @brief   Detail customization for ASeinNavVolume — adds the "Bake Navigation"
 *          button to the volume's details panel and forwards to the active
 *          USeinNavigation subclass via `CustomizeVolumeDetails` so impls can
 *          extend the panel with their own rows.
 *
 *          Lives in SeinARTSNavigation (private/editor) so the system stays
 *          self-contained — a team replacing the nav module without using the
 *          framework's editor module still gets the bake button registered
 *          automatically when the nav module loads.
 */

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class FSeinNavVolumeDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	TWeakObjectPtr<class UWorld> CachedWorld;

	FReply OnRebuildClicked();
	bool IsRebuildEnabled() const;
};

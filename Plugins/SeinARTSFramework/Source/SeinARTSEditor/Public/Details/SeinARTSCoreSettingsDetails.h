/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinARTSCoreSettingsDetails.h
 * @brief:   IDetailCustomization for USeinARTSCoreSettings. Injects the two
 *           regenerate buttons under the Tag Semantics category:
 *             - "Regenerate Auto-Generated Tags" — non-destructive
 *             - "Force Regenerate All Tags"      — destructive, confirmation dialog
 */

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class FSeinARTSCoreSettingsDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinARTSCoreSettingsDetails.h
 * @brief:   IDetailCustomization for USeinARTSCoreSettings. Owns generated
 *           simulation-content and tag-semantics actions.
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

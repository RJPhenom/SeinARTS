/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionChannelDetails.h
 * @brief   Property-type customization for FSeinCollisionChannelDefinition (each
 *          row of USeinARTSCoreSettings::CollisionChannels). Renders the channel
 *          as a flat Name | Default Response | Debug Color row instead of the
 *          default expandable "Index [N]" struct, so the registry reads as a
 *          compact table. Add/remove stays on the array's built-in controls.
 */

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class FSeinCollisionChannelDetails : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> PropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> PropertyHandle,
		IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;
};

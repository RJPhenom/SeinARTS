/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionResponseDetails.h
 * @brief   Property-type customization for FSeinCollisionResponseContainer (the
 *          collision response matrix on FSeinExtentsPayload). Renders the
 *          Unreal-style per-channel Ignore / Overlap / Block selector — one row
 *          per channel in USeinARTSCoreSettings::CollisionChannels —
 *          instead of the raw "array of {Channel, Response}" UI.
 *
 *          Reads/writes the sparse override array: a cell set to the channel's
 *          DefaultResponse removes the override (no entry == default); any other
 *          value writes/updates one. Matches Unreal's FCollisionResponse model.
 */

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class FSeinCollisionResponseDetails : public IPropertyTypeCustomization
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

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFixedPointDetails.h
 * @brief   Property type customization for FFixedPoint. Shows the value as a
 *          single decimal float input — designer-friendly. Underlying storage
 *          is still 32.32 fixed-point (deterministic); the float field is just
 *          a presentation layer that converts via ToFloat / FromFloat on the
 *          edit boundary. For programmatic precision use the
 *          MakeFixedPointFromParts / BreakFixedPointToParts BPFL.
 */

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;

class FSeinFixedPointDetails : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	//~ IPropertyTypeCustomization
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	TSharedPtr<IPropertyHandle> ValueHandle;

	int64 GetRawValue() const;
	void  SetRawValue(int64 NewVal) const;

	TOptional<float> GetFloatValue() const;
	void OnFloatCommitted(float NewValue, ETextCommit::Type CommitType);
};

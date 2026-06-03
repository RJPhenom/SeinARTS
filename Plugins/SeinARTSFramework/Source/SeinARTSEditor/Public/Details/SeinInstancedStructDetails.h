/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinInstancedStructDetails.h
 * @brief   Custom property-type customization for FInstancedStruct that allows
 *          UserDefinedStructs to appear in pickers filtered by BaseStruct metadata.
 *
 *          UE's default FInstancedStructDetails uses SInstancedStructPicker,
 *          which hardcodes `bAllowUserDefinedStructs = (BaseStruct == null)`.
 *          The moment you set BaseStruct, every UDS is excluded — even if its
 *          super struct is a valid child of BaseStruct.
 *
 *          This subclass keeps UE's child-row rendering intact and only
 *          replaces the header's picker with one that always sets
 *          `bAllowUserDefinedStructs = true` so designer-authored UDSes
 *          (including Sein UDS components created by USeinSimComponentFactory)
 *          appear alongside the native C++ structs in the picker.
 */

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;
class IPropertyUtilities;
class SComboButton;
class UScriptStruct;

class FSeinInstancedStructDetails : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	//~ IPropertyTypeCustomization
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	TSharedPtr<IPropertyHandle> StructProperty;
	TSharedPtr<IPropertyUtilities> PropUtils;
	TSharedPtr<SComboButton> ComboButton;

	TSharedRef<SWidget> GenerateStructPicker();
	void OnStructPicked(const UScriptStruct* InStruct);

	const UScriptStruct* GetBaseStructFromMeta() const;
	const UScriptStruct* GetCurrentScriptStruct() const;
	FText GetDisplayValueString() const;
	FText GetTooltipText() const;
	const struct FSlateBrush* GetDisplayValueIcon() const;
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFixedPointDetails.cpp
 * @brief   Single decimal-float field for FFixedPoint properties. The
 *          underlying storage is still 32.32 fixed-point — only the editor
 *          presentation is via float.
 */

#include "Details/SeinFixedPointDetails.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SNumericEntryBox.h"

#define LOCTEXT_NAMESPACE "SeinFixedPointDetails"

namespace SeinFixedPointDetails_Private
{
	// Round-trip conversion factor — must match FFixedPoint::FromFloat /
	// FFixedPoint::ToFloat. Hard-coded as a double literal so the customization
	// stays self-contained.
	constexpr double kFixedPointScale = 4294967296.0; // 2^32
}

TSharedRef<IPropertyTypeCustomization> FSeinFixedPointDetails::MakeInstance()
{
	return MakeShared<FSeinFixedPointDetails>();
}

void FSeinFixedPointDetails::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	ValueHandle = PropertyHandle->GetChildHandle(TEXT("Value"));

	// Honor ClampMin / ClampMax / UIMin / UIMax meta tags if the property
	// declares them, so the float field clamps the same way an int32 property
	// would. Missing metadata = unbounded.
	TOptional<float> MinValue;
	TOptional<float> MaxValue;
	TOptional<float> UIMin;
	TOptional<float> UIMax;
	if (PropertyHandle->HasMetaData(TEXT("ClampMin")))
	{
		MinValue = FCString::Atof(*PropertyHandle->GetMetaData(TEXT("ClampMin")));
	}
	if (PropertyHandle->HasMetaData(TEXT("ClampMax")))
	{
		MaxValue = FCString::Atof(*PropertyHandle->GetMetaData(TEXT("ClampMax")));
	}
	if (PropertyHandle->HasMetaData(TEXT("UIMin")))
	{
		UIMin = FCString::Atof(*PropertyHandle->GetMetaData(TEXT("UIMin")));
	}
	if (PropertyHandle->HasMetaData(TEXT("UIMax")))
	{
		UIMax = FCString::Atof(*PropertyHandle->GetMetaData(TEXT("UIMax")));
	}

	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(120.f)
	.MaxDesiredWidth(220.f)
	[
		SNew(SNumericEntryBox<float>)
			.AllowSpin(true)
			.Value(this, &FSeinFixedPointDetails::GetFloatValue)
			.OnValueCommitted(this, &FSeinFixedPointDetails::OnFloatCommitted)
			.MinValue(MinValue)
			.MaxValue(MaxValue)
			.MinSliderValue(UIMin.IsSet() ? UIMin : MinValue)
			.MaxSliderValue(UIMax.IsSet() ? UIMax : MaxValue)
			.ToolTipText(LOCTEXT("FixedPointTooltip",
				"Decimal display of the underlying 32.32 fixed-point value. "
				"Editing converts via FromFloat — for bit-exact authoring, "
				"use Make Fixed Point From Parts in a Blueprint."))
	];
}

void FSeinFixedPointDetails::CustomizeChildren(
	TSharedRef<IPropertyHandle> /*PropertyHandle*/,
	IDetailChildrenBuilder& /*ChildBuilder*/,
	IPropertyTypeCustomizationUtils& /*CustomizationUtils*/)
{
	// No child rows — the Value field is fully represented in the header.
}

int64 FSeinFixedPointDetails::GetRawValue() const
{
	int64 Result = 0;
	if (ValueHandle.IsValid())
	{
		ValueHandle->GetValue(Result);
	}
	return Result;
}

void FSeinFixedPointDetails::SetRawValue(int64 NewVal) const
{
	if (ValueHandle.IsValid())
	{
		ValueHandle->SetValue(NewVal);
	}
}

TOptional<float> FSeinFixedPointDetails::GetFloatValue() const
{
	const int64 Raw = GetRawValue();
	return static_cast<float>(static_cast<double>(Raw) / SeinFixedPointDetails_Private::kFixedPointScale);
}

void FSeinFixedPointDetails::OnFloatCommitted(float NewValue, ETextCommit::Type /*CommitType*/)
{
	// FromFloat semantics: raw = (int64)(value * 2^32). Use double for the
	// multiply so we don't lose precision for values like 0.1 that have
	// inexact float representations.
	const int64 Raw = static_cast<int64>(static_cast<double>(NewValue) * SeinFixedPointDetails_Private::kFixedPointScale);
	SetRawValue(Raw);
}

#undef LOCTEXT_NAMESPACE

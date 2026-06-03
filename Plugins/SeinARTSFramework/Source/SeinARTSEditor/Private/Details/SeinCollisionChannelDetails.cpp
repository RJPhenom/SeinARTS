/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionChannelDetails.cpp
 *
 * Lays a collision-channel registry row out flat: the Name field in the name
 * column, then the Default Response dropdown + Debug Color swatch in the value
 * column. No expandable "Index [N]" struct, no per-channel enable flag (channels
 * are always live; participation is gated per-entity by bCollisionEnabled). Each
 * field reuses its standard engine value widget, so undo/transactions/multi-edit
 * all work for free; CustomizeChildren is intentionally empty (flat row).
 */

#include "Details/SeinCollisionChannelDetails.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "SeinCollisionChannelDetails"

TSharedRef<IPropertyTypeCustomization> FSeinCollisionChannelDetails::MakeInstance()
{
	return MakeShared<FSeinCollisionChannelDetails>();
}

void FSeinCollisionChannelDetails::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& /*CustomizationUtils*/)
{
	const TSharedPtr<IPropertyHandle> NameHandle     = PropertyHandle->GetChildHandle(TEXT("Name"));
	const TSharedPtr<IPropertyHandle> ResponseHandle = PropertyHandle->GetChildHandle(TEXT("DefaultResponse"));
	const TSharedPtr<IPropertyHandle> ColorHandle    = PropertyHandle->GetChildHandle(TEXT("DebugColor"));

	// Standard engine value widgets (text / enum-combo / color swatch). The
	// `false` suppresses the per-widget reset-to-default arrow to keep rows tight.
	const TSharedRef<SWidget> NameWidget = NameHandle.IsValid()
		? NameHandle->CreatePropertyValueWidget(false) : SNullWidget::NullWidget;
	const TSharedRef<SWidget> ResponseWidget = ResponseHandle.IsValid()
		? ResponseHandle->CreatePropertyValueWidget(false) : SNullWidget::NullWidget;
	const TSharedRef<SWidget> ColorWidget = ColorHandle.IsValid()
		? ColorHandle->CreatePropertyValueWidget(false) : SNullWidget::NullWidget;

	HeaderRow
	.NameContent()
	[
		NameWidget
	]
	.ValueContent()
	.MinDesiredWidth(260.f)
	.MaxDesiredWidth(360.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 8.f, 0.f)
		[
			ResponseWidget
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(64.f)
			[
				ColorWidget
			]
		]
	];
}

void FSeinCollisionChannelDetails::CustomizeChildren(
	TSharedRef<IPropertyHandle> /*PropertyHandle*/,
	IDetailChildrenBuilder& /*ChildBuilder*/,
	IPropertyTypeCustomizationUtils& /*CustomizationUtils*/)
{
	// Flat row — no expandable children.
}

#undef LOCTEXT_NAMESPACE

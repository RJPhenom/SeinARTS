/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionObjectTypeDetails.cpp
 *
 * Renders FSeinCollisionObjectType as a single dropdown: a combo button whose
 * label is the current channel name, and whose menu lists None + every channel
 * declared in USeinARTSCoreSettings::CollisionChannels. Selecting an entry
 * writes the name to the wrapped `Channel` FName handle (so undo / transactions
 * / multi-edit work). Re-read from settings on every menu open, so renaming or
 * adding a channel updates the dropdown without an editor restart.
 */

#include "Details/SeinCollisionObjectTypeDetails.h"

#include "Settings/PluginSettings.h"
#include "Data/SeinCollisionChannelDefinition.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SComboButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "SeinCollisionObjectTypeDetails"

namespace SeinCollisionObjectTypeDetailsLocal
{
	static FText CurrentLabel(TSharedPtr<IPropertyHandle> ChannelHandle)
	{
		FName Value = NAME_None;
		if (ChannelHandle.IsValid())
		{
			ChannelHandle->GetValue(Value);
		}
		return Value.IsNone() ? LOCTEXT("None", "None") : FText::FromName(Value);
	}

	static TSharedRef<SWidget> BuildMenu(TSharedPtr<IPropertyHandle> ChannelHandle)
	{
		FMenuBuilder MenuBuilder(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr);

		auto AddEntry = [&MenuBuilder, ChannelHandle](const FText& Label, FName Value)
		{
			MenuBuilder.AddMenuEntry(
				Label,
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([ChannelHandle, Value]()
					{
						if (ChannelHandle.IsValid()) ChannelHandle->SetValue(Value);
					}),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([ChannelHandle, Value]()
					{
						FName Current = NAME_None;
						if (ChannelHandle.IsValid()) ChannelHandle->GetValue(Current);
						return Current == Value;
					})
				),
				NAME_None,
				EUserInterfaceActionType::RadioButton);
		};

		AddEntry(LOCTEXT("None", "None"), NAME_None);
		if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
		{
			for (const FSeinCollisionChannelDefinition& Def : Settings->GetAllCollisionChannels())
			{
				if (Def.Name.IsNone()) continue;
				AddEntry(FText::FromName(Def.Name), Def.Name);
			}
		}

		return MenuBuilder.MakeWidget();
	}
}

TSharedRef<IPropertyTypeCustomization> FSeinCollisionObjectTypeDetails::MakeInstance()
{
	return MakeShared<FSeinCollisionObjectTypeDetails>();
}

void FSeinCollisionObjectTypeDetails::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& /*CustomizationUtils*/)
{
	const TSharedPtr<IPropertyHandle> ChannelHandle = PropertyHandle->GetChildHandle(TEXT("Channel"));

	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(180.f)
	.MaxDesiredWidth(320.f)
	[
		SNew(SComboButton)
		.ContentPadding(FMargin(6.f, 2.f))
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text_Lambda([ChannelHandle]() { return SeinCollisionObjectTypeDetailsLocal::CurrentLabel(ChannelHandle); })
		]
		.OnGetMenuContent_Lambda([ChannelHandle]() { return SeinCollisionObjectTypeDetailsLocal::BuildMenu(ChannelHandle); })
	];
}

void FSeinCollisionObjectTypeDetails::CustomizeChildren(
	TSharedRef<IPropertyHandle> /*PropertyHandle*/,
	IDetailChildrenBuilder& /*ChildBuilder*/,
	IPropertyTypeCustomizationUtils& /*CustomizationUtils*/)
{
	// Single-value dropdown — no expandable children.
}

#undef LOCTEXT_NAMESPACE

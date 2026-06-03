/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionResponseDetails.cpp
 *
 * Renders FSeinCollisionResponseContainer as a per-channel response matrix:
 * a header row of Ignore/Overlap/Block column labels, then one row per
 * collision channel (from plugin settings) with three mutually-exclusive
 * selectors. The container stores only OVERRIDES — a cell at the channel's
 * DefaultResponse carries no entry — so writes add/update/remove entries to
 * keep that invariant (same model as Unreal's FCollisionResponse).
 */

#include "Details/SeinCollisionResponseDetails.h"

#include "Settings/PluginSettings.h"
#include "Data/SeinCollisionChannelDefinition.h"
#include "Collision/SeinCollisionTypes.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"

#define LOCTEXT_NAMESPACE "SeinCollisionResponseDetails"

namespace SeinCollisionResponseDetailsLocal
{
	/** Per-column fixed width so the header labels line up over the row selectors. */
	static constexpr float ColumnWidth = 64.f;

	/** A channel's DefaultResponse from settings (Block if not found). */
	static ESeinCollisionResponse GetChannelDefault(FName Channel)
	{
		if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
		{
			for (const FSeinCollisionChannelDefinition& Def : Settings->CollisionChannels)
			{
				if (Def.Name == Channel)
				{
					return Def.DefaultResponse;
				}
			}
		}
		return ESeinCollisionResponse::Block;
	}

	/** Current response for `Channel` (first edited object), default if unset. */
	static ESeinCollisionResponse ReadResponse(TSharedPtr<IPropertyHandle> Handle, FName Channel)
	{
		const ESeinCollisionResponse Default = GetChannelDefault(Channel);
		if (!Handle.IsValid()) return Default;

		TArray<void*> RawData;
		Handle->AccessRawData(RawData);
		if (RawData.Num() == 0 || RawData[0] == nullptr) return Default;

		const FSeinCollisionResponseContainer* Container =
			static_cast<const FSeinCollisionResponseContainer*>(RawData[0]);
		return Container->GetResponseForChannel(Channel, Default);
	}

	/** Set `Channel`'s response across all edited objects, keeping the override
	 *  array sparse (a value equal to the channel default removes the entry). */
	static void WriteResponse(TSharedPtr<IPropertyHandle> Handle, FName Channel, ESeinCollisionResponse NewResponse)
	{
		if (!Handle.IsValid()) return;
		const ESeinCollisionResponse Default = GetChannelDefault(Channel);

		TArray<void*> RawData;
		Handle->AccessRawData(RawData);
		if (RawData.Num() == 0) return;

		Handle->NotifyPreChange();
		for (void* Data : RawData)
		{
			if (!Data) continue;
			FSeinCollisionResponseContainer* Container = static_cast<FSeinCollisionResponseContainer*>(Data);

			int32 FoundIndex = INDEX_NONE;
			for (int32 i = 0; i < Container->Overrides.Num(); ++i)
			{
				if (Container->Overrides[i].Channel == Channel) { FoundIndex = i; break; }
			}

			if (NewResponse == Default)
			{
				if (FoundIndex != INDEX_NONE) Container->Overrides.RemoveAt(FoundIndex);
			}
			else if (FoundIndex != INDEX_NONE)
			{
				Container->Overrides[FoundIndex].Response = NewResponse;
			}
			else
			{
				FSeinCollisionResponseOverride Override;
				Override.Channel = Channel;
				Override.Response = NewResponse;
				Container->Overrides.Add(Override);
			}
		}
		Handle->NotifyPostChange(EPropertyChangeType::ValueSet);
	}

	/** One selector cell (a checkbox acting as a radio: checked iff the channel's
	 *  current response equals this column; clicking it sets that response). */
	static TSharedRef<SWidget> MakeResponseCell(TSharedPtr<IPropertyHandle> Handle, FName Channel, ESeinCollisionResponse Column)
	{
		return SNew(SBox)
			.WidthOverride(ColumnWidth)
			.HAlign(HAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([Handle, Channel, Column]()
				{
					return ReadResponse(Handle, Channel) == Column ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([Handle, Channel, Column](ECheckBoxState State)
				{
					// Only the "check" transition selects; clicking the already-active
					// cell (an uncheck) is ignored — a channel always has a response.
					if (State == ECheckBoxState::Checked)
					{
						WriteResponse(Handle, Channel, Column);
					}
				})
			];
	}

	/** A fixed-width column header label. */
	static TSharedRef<SWidget> MakeColumnLabel(const FText& Label)
	{
		return SNew(SBox)
			.WidthOverride(ColumnWidth)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock).Text(Label)
			];
	}
}

TSharedRef<IPropertyTypeCustomization> FSeinCollisionResponseDetails::MakeInstance()
{
	return MakeShared<FSeinCollisionResponseDetails>();
}

void FSeinCollisionResponseDetails::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& /*CustomizationUtils*/)
{
	// Show the field name ("Collision Responses"); the matrix lives in children.
	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	];
}

void FSeinCollisionResponseDetails::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& /*CustomizationUtils*/)
{
	using namespace SeinCollisionResponseDetailsLocal;

	TSharedPtr<IPropertyHandle> Handle = PropertyHandle;

	// Column-header row: Ignore | Overlap | Block.
	ChildBuilder.AddCustomRow(LOCTEXT("ResponseHeaderFilter", "Collision Response"))
	.NameContent()
	[
		SNew(STextBlock).Text(LOCTEXT("ResponseHeaderName", "Response"))
	]
	.ValueContent()
	.MinDesiredWidth(ColumnWidth * 3.f)
	.MaxDesiredWidth(ColumnWidth * 3.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth() [ MakeColumnLabel(LOCTEXT("ColIgnore",  "Ignore"))  ]
		+ SHorizontalBox::Slot().AutoWidth() [ MakeColumnLabel(LOCTEXT("ColOverlap", "Overlap")) ]
		+ SHorizontalBox::Slot().AutoWidth() [ MakeColumnLabel(LOCTEXT("ColBlock",   "Block"))   ]
	];

	// One row per channel in settings.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	bool bAnyChannel = false;
	if (Settings)
	{
		for (const FSeinCollisionChannelDefinition& Def : Settings->CollisionChannels)
		{
			if (Def.Name.IsNone()) continue;
			bAnyChannel = true;

			const FName ChannelName = Def.Name;
			ChildBuilder.AddCustomRow(FText::FromName(ChannelName))
			.NameContent()
			[
				SNew(STextBlock).Text(FText::FromName(ChannelName))
			]
			.ValueContent()
			.MinDesiredWidth(ColumnWidth * 3.f)
			.MaxDesiredWidth(ColumnWidth * 3.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth() [ MakeResponseCell(Handle, ChannelName, ESeinCollisionResponse::Ignore)  ]
				+ SHorizontalBox::Slot().AutoWidth() [ MakeResponseCell(Handle, ChannelName, ESeinCollisionResponse::Overlap) ]
				+ SHorizontalBox::Slot().AutoWidth() [ MakeResponseCell(Handle, ChannelName, ESeinCollisionResponse::Block)   ]
			];
		}
	}

	if (!bAnyChannel)
	{
		ChildBuilder.AddCustomRow(LOCTEXT("NoChannelsFilter", "Collision Channels"))
		.WholeRowContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoChannels", "No collision channels enabled — configure them in Project Settings > Plugins > SeinARTS > Collision."))
			.AutoWrapText(true)
		];
	}
}

#undef LOCTEXT_NAMESPACE

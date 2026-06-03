/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLayerMaskUI.cpp
 */

#include "Details/SeinLayerMaskUI.h"

#include "Settings/PluginSettings.h"
#include "Data/SeinNavLayerDefinition.h"
#include "Data/SeinVisionLayerDefinition.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"

#define LOCTEXT_NAMESPACE "SeinLayerMaskUI"

namespace SeinLayerMaskUI
{
	FText GetNavLayerLabel(int32 BitIdx)
	{
		if (BitIdx == 0) return LOCTEXT("NavDefault", "Default");

		const int32 SlotIdx = BitIdx - 1;
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		if (Settings && Settings->NavLayers.IsValidIndex(SlotIdx))
		{
			const FSeinNavLayerDefinition& Def = Settings->NavLayers[SlotIdx];
			if (Def.bEnabled && !Def.LayerName.IsNone())
			{
				return FText::FromName(Def.LayerName);
			}
		}
		return LOCTEXT("Unused", "(unused)");
	}

	FText GetFogLayerLabel(int32 BitIdx)
	{
		if (BitIdx == 0) return LOCTEXT("FogExplored", "Explored");
		if (BitIdx == 1) return LOCTEXT("FogVisible", "Visible");

		const int32 SlotIdx = BitIdx - 2;
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		if (Settings && Settings->VisionLayers.IsValidIndex(SlotIdx))
		{
			const FSeinVisionLayerDefinition& Def = Settings->VisionLayers[SlotIdx];
			if (Def.bEnabled && !Def.LayerName.IsNone())
			{
				return FText::FromName(Def.LayerName);
			}
		}
		return LOCTEXT("Unused", "(unused)");
	}

	namespace
	{
		FText GetSummaryText(TSharedPtr<IPropertyHandle> MaskHandle, TFunction<FText(int32)> GetBitLabel)
		{
			if (!MaskHandle.IsValid()) return FText::GetEmpty();
			uint8 Value = 0;
			MaskHandle->GetValue(Value);
			if (Value == 0) return LOCTEXT("NoFlags", "(none)");

			TArray<FString> Names;
			for (int32 Bit = 0; Bit < 8; ++Bit)
			{
				if ((Value & (1u << Bit)) != 0)
				{
					Names.Add(GetBitLabel(Bit).ToString());
				}
			}
			return FText::FromString(FString::Join(Names, TEXT(", ")));
		}

		TSharedRef<SWidget> BuildBitmaskMenu(TSharedPtr<IPropertyHandle> MaskHandle, TFunction<FText(int32)> GetBitLabel)
		{
			TSharedRef<SVerticalBox> VBox = SNew(SVerticalBox);
			for (int32 Bit = 0; Bit < 8; ++Bit)
			{
				VBox->AddSlot()
				.AutoHeight()
				.Padding(8.f, 3.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([MaskHandle, Bit]()
						{
							if (!MaskHandle.IsValid()) return ECheckBoxState::Unchecked;
							uint8 V = 0;
							MaskHandle->GetValue(V);
							return ((V & (1u << Bit)) != 0) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([MaskHandle, Bit](ECheckBoxState State)
						{
							if (!MaskHandle.IsValid()) return;
							uint8 V = 0;
							MaskHandle->GetValue(V);
							const uint8 BitMask = static_cast<uint8>(1u << Bit);
							const uint8 NewV = (State == ECheckBoxState::Checked)
								? (V | BitMask)
								: (V & ~BitMask);
							MaskHandle->SetValue(NewV);
						})
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text_Lambda([Bit, GetBitLabel]() { return GetBitLabel(Bit); })
					]
				];
			}
			return VBox;
		}
	}

	void AddLayerMaskRow(
		TSharedRef<IPropertyHandle> MaskHandle,
		IDetailChildrenBuilder& ChildBuilder,
		TFunction<FText(int32)> GetBitLabel,
		const FText& RowFilterText)
	{
		ChildBuilder.AddCustomRow(RowFilterText)
		.NameContent()
		[
			MaskHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(220.f)
		.MaxDesiredWidth(420.f)
		[
			SNew(SComboButton)
			.ContentPadding(FMargin(6.f, 2.f))
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text_Lambda([MaskHandle, GetBitLabel]() { return GetSummaryText(MaskHandle, GetBitLabel); })
			]
			.OnGetMenuContent_Lambda([MaskHandle, GetBitLabel]()
			{
				return BuildBitmaskMenu(MaskHandle, GetBitLabel);
			})
		];
	}
}

#undef LOCTEXT_NAMESPACE

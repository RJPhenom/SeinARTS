/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementModeDetails.cpp
 */

#include "Details/SeinMovementModeDetails.h"
#include "Util/SeinMovementTuningExport.h"

#include "Engine/Blueprint.h"
#include "StructUtils/UserDefinedStruct.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

#define LOCTEXT_NAMESPACE "SeinMovementModeDetails"

TSharedRef<IDetailCustomization> FSeinMovementModeDetails::MakeInstance()
{
	return MakeShared<FSeinMovementModeDetails>();
}

void FSeinMovementModeDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Class Defaults shows the CDO; recover the owning Blueprint from its class.
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	UBlueprint* BP = nullptr;
	for (const TWeakObjectPtr<UObject>& Obj : Objects)
	{
		UObject* O = Obj.Get();
		UClass* Cls = O ? O->GetClass() : nullptr;
		if (Cls)
		{
			if (UBlueprint* Found = Cast<UBlueprint>(Cls->ClassGeneratedBy))
			{
				BP = Found;
				break;
			}
		}
	}

	// Only BP-authored movement modes get the button. C++ modes (no ClassGeneratedBy)
	// and non-movement BPs fall through with no added UI.
	if (!SeinMovementTuning::IsMovementModeBlueprint(BP))
	{
		return;
	}

	WeakBlueprint = BP;

	IDetailCategoryBuilder& Cat = DetailBuilder.EditCategory(
		TEXT("Tuning"),
		LOCTEXT("TuningCategory", "Tuning"),
		ECategoryPriority::Important);

	Cat.AddCustomRow(LOCTEXT("GenTuningRow", "Generate Tuning Data Structure"))
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(LOCTEXT("SyncTuningHelp",
				"Mirror this mode's Instance-Editable, deterministic variables (FixedPoint, int, bool, "
				"enum, ...) into its tuning data struct (<Name>TuningData) and link it, so it auto-fills "
				"a unit's Movement Class Data. Run after adding, removing, or retyping variables."))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.ToolTipText(LOCTEXT("SyncTuningTip", "Generate the tuning data struct from this mode's Instance-Editable, deterministic variables."))
			.OnClicked(this, &FSeinMovementModeDetails::OnSyncClicked)
			.Content()
			[
				SNew(STextBlock).Text(LOCTEXT("GenTuningBtn", "Generate Tuning Data Structure"))
			]
		]
	];
}

FReply FSeinMovementModeDetails::OnSyncClicked()
{
	UBlueprint* BP = WeakBlueprint.Get();
	if (!BP)
	{
		return FReply::Handled();
	}

	UUserDefinedStruct* UDS = SeinMovementTuning::SyncTuningStructForBlueprint(BP);

	// Always tell the designer what happened — a sync must never silently no-op.
	FNotificationInfo Info(FText::GetEmpty());
	Info.ExpireDuration = 5.0f;
	Info.bUseSuccessFailIcons = true;
	if (UDS)
	{
		Info.Text = FText::Format(
			LOCTEXT("GenOk", "Generated tuning data '{0}'."), FText::FromString(UDS->GetName()));
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(SNotificationItem::CS_Success);
		}
	}
	else
	{
		Info.Text = LOCTEXT("GenNone",
			"No Instance-Editable, deterministic variables found. Mark a variable 'Instance Editable' (the "
			"eye icon) and use a deterministic type (FixedPoint, int, bool, enum), then try again.");
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE

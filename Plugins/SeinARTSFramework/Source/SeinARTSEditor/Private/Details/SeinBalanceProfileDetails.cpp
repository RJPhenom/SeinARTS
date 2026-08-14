/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBalanceProfileDetails.cpp
 */

#include "Details/SeinBalanceProfileDetails.h"
#include "Balance/SeinBalanceProfile.h"
#include "Util/SeinBalanceTableExport.h"
#include "Engine/DataTable.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

#define LOCTEXT_NAMESPACE "SeinBalanceProfileDetails"

TSharedRef<IDetailCustomization> FSeinBalanceProfileDetails::MakeInstance()
{
	return MakeShared<FSeinBalanceProfileDetails>();
}

void FSeinBalanceProfileDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	for (const TWeakObjectPtr<UObject>& Obj : Objects)
	{
		if (USeinBalanceProfile* Profile = Cast<USeinBalanceProfile>(Obj.Get()))
		{
			WeakProfile = Profile;
			break;
		}
	}
	if (!WeakProfile.IsValid())
	{
		return;
	}

	IDetailCategoryBuilder& Cat = DetailBuilder.EditCategory(
		TEXT("Balance"),
		LOCTEXT("BalanceCategory", "Balance"),
		ECategoryPriority::Important);

	Cat.AddCustomRow(LOCTEXT("BalanceActionsRow", "Balance Actions"))
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(LOCTEXT("BalanceHelp",
				"Preview lists the classes this profile currently matches. Gather rebuilds the tuning "
				"table from those source classes; Push writes edited values back into them. Re-gathering "
				"an existing table discards table edits after confirmation."))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.ToolTipText(LOCTEXT("PreviewTip", "List the entity or ability classes this profile currently matches."))
			.OnClicked(this, &FSeinBalanceProfileDetails::OnPreviewClicked)
			.Content()
			[
				SNew(STextBlock).Text(LOCTEXT("PreviewBtn", "Preview Matched Targets"))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.ToolTipText(LOCTEXT("GatherTip", "Build the flat tuning DataTable from the matched entities' components."))
			.OnClicked(this, &FSeinBalanceProfileDetails::OnGatherClicked)
			.Content()
			[
				SNew(STextBlock).Text(LOCTEXT("GatherBtn", "Gather → Table"))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.ToolTipText(LOCTEXT("PushTip", "Write edited table values back into the source Blueprints."))
			.OnClicked(this, &FSeinBalanceProfileDetails::OnPushClicked)
			.Content()
			[
				SNew(STextBlock).Text(LOCTEXT("PushBtn", "Push Table → Source"))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.ToolTipText(LOCTEXT("SyncTip", "Compare the table to the source without writing — reports cells that differ."))
			.OnClicked(this, &FSeinBalanceProfileDetails::OnCheckSyncClicked)
			.Content()
			[
				SNew(STextBlock).Text(LOCTEXT("SyncBtn", "Check Sync"))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(this, &FSeinBalanceProfileDetails::GetPreviewText)
		]
	];
}

FText FSeinBalanceProfileDetails::GetPreviewText() const
{
	return PreviewText;
}

FReply FSeinBalanceProfileDetails::OnPreviewClicked()
{
	USeinBalanceProfile* Profile = WeakProfile.Get();
	if (!Profile)
	{
		return FReply::Handled();
	}

	TArray<UClass*> Classes;
	Profile->ResolveTargetClasses(Classes);

	if (Classes.Num() == 0)
	{
		PreviewText = Profile->TargetKind == ESeinBalanceTargetKind::Abilities
			? LOCTEXT("PreviewNoAbilities",
				"No matching classes. Add an Ability Root and check its exclusions.")
			: LOCTEXT("PreviewNoEntities",
				"No matching classes. Add an Included Root and check its exclusions.");
	}
	else
	{
		FString List;
		for (const UClass* Cls : Classes)
		{
			List += FString::Printf(TEXT("• %s\n"), *Cls->GetName());
		}
		PreviewText = FText::Format(
			LOCTEXT("PreviewList", "Matched {0} class(es):\n{1}"),
			FText::AsNumber(Classes.Num()),
			FText::FromString(List));
	}

	return FReply::Handled();
}

FReply FSeinBalanceProfileDetails::OnGatherClicked()
{
	USeinBalanceProfile* Profile = WeakProfile.Get();
	if (!Profile)
	{
		return FReply::Handled();
	}

	UDataTable* Table = SeinBalanceTable::GatherToTable(Profile);

	FNotificationInfo Info(FText::GetEmpty());
	Info.ExpireDuration = 5.0f;
	Info.bUseSuccessFailIcons = true;
	if (Table)
	{
		Info.Text = FText::Format(
			LOCTEXT("GatherOk", "Gathered into '{0}'."), FText::FromString(Table->GetName()));
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(SNotificationItem::CS_Success);
		}
	}
	else
	{
		Info.Text = LOCTEXT("GatherFail",
			"Gather produced no table — check the selected roots and tracked fields (see the Output Log).");
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}
	return FReply::Handled();
}

FReply FSeinBalanceProfileDetails::OnPushClicked()
{
	USeinBalanceProfile* Profile = WeakProfile.Get();
	if (!Profile)
	{
		return FReply::Handled();
	}

	int32 SkippedCells = 0;
	const int32 NumWritten = SeinBalanceTable::PushToEntities(Profile, SkippedCells);

	FNotificationInfo Info(FText::GetEmpty());
	Info.ExpireDuration = 6.0f;
	Info.bUseSuccessFailIcons = true;
	if (NumWritten < 0)
	{
		Info.Text = LOCTEXT("PushAbort", "Push cancelled, or the generated table is missing or structurally stale (Gather first).");
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}
	else
	{
		const FText Base = FText::Format(
			LOCTEXT("PushOk", "Pushed {0} changed value(s) into the source Blueprints. Save them (Ctrl+S) to persist."),
			FText::AsNumber(NumWritten));
		Info.Text = (SkippedCells > 0)
			? FText::Format(LOCTEXT("PushOkSkip", "{0}\n({1} write error(s); see Output Log.)"),
				Base, FText::AsNumber(SkippedCells))
			: Base;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(SNotificationItem::CS_Success);
		}
	}
	return FReply::Handled();
}

FReply FSeinBalanceProfileDetails::OnCheckSyncClicked()
{
	USeinBalanceProfile* Profile = WeakProfile.Get();
	if (!Profile)
	{
		return FReply::Handled();
	}

	int32 CellsChecked = 0;
	const int32 Diffs = SeinBalanceTable::CheckSync(Profile, CellsChecked);

	FNotificationInfo Info(FText::GetEmpty());
	Info.ExpireDuration = 5.0f;
	Info.bUseSuccessFailIcons = true;
	if (Diffs < 0)
	{
		Info.Text = LOCTEXT("SyncNoTable", "No generated table to check — Gather first.");
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}
	else if (Diffs == 0)
	{
		Info.Text = FText::Format(
			LOCTEXT("SyncOk", "In sync — all {0} tuning cell(s) match the source Blueprints."),
			FText::AsNumber(CellsChecked));
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(SNotificationItem::CS_Success);
		}
	}
	else
	{
		Info.Text = CellsChecked == 0
			? LOCTEXT("SyncStructureDrift",
				"The table rows or columns no longer match this profile. Gather to rebuild it before Push.")
			: FText::Format(
				LOCTEXT("SyncDrift", "{0} of {1} tuning cell(s) differ from the source. Gather to pull source in, or Push to write table edits out."),
				FText::AsNumber(Diffs), FText::AsNumber(CellsChecked));
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(SNotificationItem::CS_None);
		}
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE

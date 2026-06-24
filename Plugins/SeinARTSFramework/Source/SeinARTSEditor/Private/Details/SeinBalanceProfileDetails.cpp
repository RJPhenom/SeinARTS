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
				"Preview lists the entity classes this profile currently matches (Included Roots and "
				"their subclasses, minus Excluded). Gather builds the tuning table from their components; "
				"Push writes edited values back into the Blueprints. (Gather/Push arrive in later phases.)"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.ToolTipText(LOCTEXT("PreviewTip", "List the ASeinActor classes this profile currently matches."))
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
				SNew(STextBlock).Text(LOCTEXT("PushBtn", "Push Table → Entities"))
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
		PreviewText = LOCTEXT("PreviewNone",
			"No matching classes. Add an Included Root (a parent unit class) — its subclasses appear here.");
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
			"Gather produced no table — check Included Roots and Tracked Components (see the Output Log).");
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}
	return FReply::Handled();
}

FReply FSeinBalanceProfileDetails::OnPushClicked()
{
	// Phase C — write-back into the source Blueprints. Honest stub for now.
	FNotificationInfo Info(LOCTEXT("PushTodo", "Push Table → Entities lands in Phase C (write-back)."));
	Info.ExpireDuration = 4.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE

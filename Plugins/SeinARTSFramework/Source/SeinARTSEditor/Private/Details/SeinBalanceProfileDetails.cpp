/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinBalanceProfileDetails.cpp
 * @author       RJ Macklem
 * @created      24 Jun 2026
 * @latest       14 Aug 2026
 * @brief        Implements Balance Data workflow controls and the filtered
 *               native/designer component-type picker.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Details/SeinBalanceProfileDetails.h"
#include "Actor/SeinActor.h"
#include "Actor/SeinEntityBridgeComponent.h"
#include "Balance/SeinBalanceProfile.h"
#include "Components/SeinComponentEligibility.h"
#include "Util/SeinBalanceTableExport.h"
#include "Engine/DataTable.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

#define LOCTEXT_NAMESPACE "SeinBalanceProfileDetails"

namespace
{
	DECLARE_DELEGATE_OneParam(
		FOnSeinBalanceComponentPicked,
		const UScriptStruct*);

	struct FSeinBalanceComponentChoice
	{
		explicit FSeinBalanceComponentChoice(const UScriptStruct* InStruct)
			: Struct(InStruct)
			, DisplayName(InStruct ? InStruct->GetDisplayNameText() : FText::GetEmpty())
			, SearchText(InStruct
				? DisplayName.ToString() + TEXT(" ") + InStruct->GetPathName()
				: FString())
		{
		}

		const UScriptStruct* Struct = nullptr;
		FText DisplayName;
		FString SearchText;
	};

	class SSeinBalanceComponentPicker final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SSeinBalanceComponentPicker) {}
			SLATE_ARGUMENT(TArray<const UScriptStruct*>, Candidates)
			SLATE_EVENT(FOnSeinBalanceComponentPicked, OnPicked)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			OnPicked = InArgs._OnPicked;
			for (const UScriptStruct* Struct : InArgs._Candidates)
			{
				AllChoices.Add(MakeShared<FSeinBalanceComponentChoice>(Struct));
			}
			FilteredChoices = AllChoices;

			ChildSlot
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4.f)
				[
					SNew(SSearchBox)
					.HintText(LOCTEXT(
						"TrackedComponentSearchHint",
						"Search component types"))
					.OnTextChanged(
						this,
						&SSeinBalanceComponentPicker::OnSearchChanged)
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SAssignNew(
						ListView,
						SListView<TSharedPtr<FSeinBalanceComponentChoice>>)
					.ListItemsSource(&FilteredChoices)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(
						this,
						&SSeinBalanceComponentPicker::GenerateRow)
					.OnSelectionChanged(
						this,
						&SSeinBalanceComponentPicker::OnSelectionChanged)
				]
			];
		}

	private:
		TSharedRef<ITableRow> GenerateRow(
			TSharedPtr<FSeinBalanceComponentChoice> Choice,
			const TSharedRef<STableViewBase>& OwnerTable) const
		{
			return SNew(
				STableRow<TSharedPtr<FSeinBalanceComponentChoice>>,
				OwnerTable)
				.ToolTipText(Choice.IsValid() && Choice->Struct
					? FText::FromString(Choice->Struct->GetPathName())
					: FText::GetEmpty())
				[
					SNew(STextBlock)
					.Text(Choice.IsValid()
						? Choice->DisplayName
						: FText::GetEmpty())
				];
		}

		void OnSearchChanged(const FText& SearchText)
		{
			const FString Search = SearchText.ToString();
			FilteredChoices.Reset();
			for (const TSharedPtr<FSeinBalanceComponentChoice>& Choice : AllChoices)
			{
				if (Choice.IsValid()
					&& (Search.IsEmpty()
						|| Choice->SearchText.Contains(
							Search,
							ESearchCase::IgnoreCase)))
				{
					FilteredChoices.Add(Choice);
				}
			}
			if (ListView.IsValid())
			{
				ListView->RequestListRefresh();
			}
		}

		void OnSelectionChanged(
			TSharedPtr<FSeinBalanceComponentChoice> Choice,
			ESelectInfo::Type SelectionType)
		{
			if (Choice.IsValid()
				&& Choice->Struct
				&& SelectionType != ESelectInfo::Direct)
			{
				OnPicked.ExecuteIfBound(Choice->Struct);
			}
		}

		FOnSeinBalanceComponentPicked OnPicked;
		TArray<TSharedPtr<FSeinBalanceComponentChoice>> AllChoices;
		TArray<TSharedPtr<FSeinBalanceComponentChoice>> FilteredChoices;
		TSharedPtr<SListView<TSharedPtr<FSeinBalanceComponentChoice>>> ListView;
	};
}

void SeinBalanceProfileDetails::CollectTrackedComponentCandidates(
	const USeinBalanceProfile& Profile,
	TArray<const UScriptStruct*>& OutCandidates)
{
	OutCandidates.Reset();
	if (Profile.TargetKind != ESeinBalanceTargetKind::Entities)
	{
		return;
	}

	const auto TryAdd = [&Profile, &OutCandidates](const UScriptStruct* Struct)
	{
		if (!Struct
			|| Struct->GetOutermost() == GetTransientPackage()
			|| Struct->HasAnyFlags(RF_NewerVersionExists)
			|| (Struct->StructFlags & STRUCT_NewerVersionExists) != 0
			|| !SeinComponentEligibility::IsEntityComponentStruct(Struct)
			|| Profile.TrackedComponents.ContainsByPredicate(
				[Struct](const TObjectPtr<UScriptStruct>& Existing)
				{
					return Existing.Get() == Struct;
				}))
		{
			return;
		}
		OutCandidates.AddUnique(Struct);
	};

	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		if (It->IsNative())
		{
			TryAdd(*It);
		}
	}

	TArray<UClass*> MatchedClasses;
	Profile.ResolveTargetClasses(MatchedClasses);
	for (const UClass* MatchedClass : MatchedClasses)
	{
		const ASeinActor* EntityCDO = MatchedClass
			? Cast<ASeinActor>(MatchedClass->GetDefaultObject())
			: nullptr;
		const USeinEntityBridgeComponent* Bridge = EntityCDO
			? EntityCDO->GetEntityBridge()
			: nullptr;
		if (!Bridge)
		{
			continue;
		}

		for (const FInstancedStruct& Component : Bridge->ComponentData)
		{
			const UScriptStruct* Struct = Component.GetScriptStruct();
			if (Struct && Struct->IsA<UUserDefinedStruct>())
			{
				TryAdd(Struct);
			}
		}
	}

	OutCandidates.Sort([](const UScriptStruct& A, const UScriptStruct& B)
	{
		const int32 NameOrder = A.GetDisplayNameText().ToString().Compare(
			B.GetDisplayNameText().ToString());
		return NameOrder == 0
			? A.GetPathName() < B.GetPathName()
			: NameOrder < 0;
	});
}

TSharedRef<IDetailCustomization> FSeinBalanceProfileDetails::MakeInstance()
{
	return MakeShared<FSeinBalanceProfileDetails>();
}

void FSeinBalanceProfileDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	if (Objects.Num() != 1)
	{
		return;
	}
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

	TrackedComponentsHandle = DetailBuilder.GetProperty(
		GET_MEMBER_NAME_CHECKED(USeinBalanceProfile, TrackedComponents));
	PropertyUtilities = DetailBuilder.GetPropertyUtilities();
	if (TrackedComponentsHandle.IsValid()
		&& TrackedComponentsHandle->IsValidHandle())
	{
		IDetailCategoryBuilder& TrackingCategory = DetailBuilder.EditCategory(
			TEXT("Tracking"),
			LOCTEXT("TrackingCategory", "Tracking"));
		TrackingCategory.AddProperty(TrackedComponentsHandle);
		TrackingCategory.AddCustomRow(
			LOCTEXT("AddTrackedComponentSearch", "Add Tracked Component Type"))
		.Visibility(TAttribute<EVisibility>::CreateSP(
			this,
			&FSeinBalanceProfileDetails::GetTrackedComponentPickerVisibility))
		.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(LOCTEXT("AddTrackedComponentLabel", "Add Component Type"))
			.ToolTipText(LOCTEXT(
				"AddTrackedComponentLabelTip",
				"Add a native or designer-authored entity component to this profile's explicit tracking list."))
		]
		.ValueContent()
		.MinDesiredWidth(220.f)
		.MaxDesiredWidth(420.f)
		[
			SAssignNew(TrackedComponentPickerButton, SComboButton)
			.OnGetMenuContent(this, &FSeinBalanceProfileDetails::GenerateTrackedComponentPicker)
			.ContentPadding(FMargin(6.f, 2.f))
			.ToolTipText(LOCTEXT(
				"AddTrackedComponentTip",
				"Choose an eligible component type. Leave Tracked Components empty to track every eligible component found on matched entities."))
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush(TEXT("Icons.Plus")))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AddTrackedComponentButton", "Add Component Type"))
				]
			]
		];
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

TSharedRef<SWidget> FSeinBalanceProfileDetails::GenerateTrackedComponentPicker()
{
	TArray<const UScriptStruct*> Candidates;
	if (const USeinBalanceProfile* Profile = WeakProfile.Get())
	{
		SeinBalanceProfileDetails::CollectTrackedComponentCandidates(
			*Profile,
			Candidates);
	}

	return SNew(SBox)
		.WidthOverride(360.f)
		.MaxDesiredHeight(500.f)
		[
			SNew(SSeinBalanceComponentPicker)
			.Candidates(MoveTemp(Candidates))
			.OnPicked(FOnSeinBalanceComponentPicked::CreateSP(
				this,
				&FSeinBalanceProfileDetails::OnTrackedComponentPicked))
		];
}

void FSeinBalanceProfileDetails::OnTrackedComponentPicked(
	const UScriptStruct* InStruct)
{
	if (TrackedComponentPickerButton.IsValid())
	{
		TrackedComponentPickerButton->SetIsOpen(false);
	}

	USeinBalanceProfile* Profile = WeakProfile.Get();
	const bool bAlreadyTracked = Profile
		&& Profile->TrackedComponents.ContainsByPredicate(
			[InStruct](const TObjectPtr<UScriptStruct>& Existing)
			{
				return Existing.Get() == InStruct;
			});

	if (Profile
		&& Profile->TargetKind == ESeinBalanceTargetKind::Entities
		&& !bAlreadyTracked
		&& SeinComponentEligibility::IsEntityComponentStruct(InStruct)
		&& TrackedComponentsHandle.IsValid()
		&& TrackedComponentsHandle->IsValidHandle())
	{
		const TSharedPtr<IPropertyHandleArray> ArrayHandle =
			TrackedComponentsHandle->AsArray();
		if (ArrayHandle.IsValid())
		{
			FScopedTransaction Transaction(LOCTEXT(
				"AddTrackedComponentTransaction",
				"Add Balance Component Type"));
			bool bChanged = false;
			const FPropertyHandleItemAddResult AddResult = ArrayHandle->AddItem();
			if (AddResult.GetAccessResult() == FPropertyAccess::Success)
			{
				const TSharedRef<IPropertyHandle> ElementHandle =
					ArrayHandle->GetElement(AddResult.GetIndex());
				if (ElementHandle->SetValue(InStruct) != FPropertyAccess::Success)
				{
					ArrayHandle->DeleteItem(AddResult.GetIndex());
				}
				else
				{
					bChanged = true;
					if (PropertyUtilities.IsValid())
					{
						PropertyUtilities->ForceRefresh();
					}
				}
			}
			if (!bChanged)
			{
				Transaction.Cancel();
			}
		}
	}
}

EVisibility FSeinBalanceProfileDetails::GetTrackedComponentPickerVisibility() const
{
	const USeinBalanceProfile* Profile = WeakProfile.Get();
	return Profile
		&& Profile->TargetKind == ESeinBalanceTargetKind::Entities
		? EVisibility::Visible
		: EVisibility::Collapsed;
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

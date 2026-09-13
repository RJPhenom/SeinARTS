/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinAutoTagDetails.cpp
 * @author       RJ Macklem
 * @created      7 Sep 2026
 * @latest       7 Sep 2026
 * @brief        Exposes identity initialization and explicit reference-preserving tag migration.
 * @disclaimer   Generated with assistance from an AI language model.
 */

#include "Details/SeinAutoTagDetails.h"

#include "Util/SeinAutoTagGenerator.h"
#include "Abilities/SeinAbility.h"
#include "Effects/SeinEffect.h"
#include "Components/SeinIdentityPayload.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Engine/Blueprint.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

namespace SeinAutoTagDetailsLocal
{
	static void ShowRegenerationResult(
		const UBlueprint& Blueprint,
		const FSeinAutoTagRegenerationResult& Result)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}

		FNotificationInfo Info(Result.ToUserMessage(&Blueprint));
		Info.ExpireDuration = Result.IsFailure() ? 8.0f : 4.0f;
		Info.bUseSuccessFailIcons = true;
		if (TSharedPtr<SNotificationItem> Item =
			FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(
				Result.IsFailure()
					? SNotificationItem::CS_Fail
					: SNotificationItem::CS_Success);
		}
	}

    static UBlueprint* BlueprintForObject(UObject* Object)
    {
        if (!Object) return nullptr;
        if (auto* BP = Cast<UBlueprint>(Object)) return BP;
        if (auto* BP = Object->GetTypedOuter<UBlueprint>()) return BP;
        if (auto* Class = Object->GetTypedOuter<UClass>())
            if (auto* BP = Cast<UBlueprint>(Class->ClassGeneratedBy)) return BP;
        return Cast<UBlueprint>(Object->GetClass()->ClassGeneratedBy);
    }

    static void ShowRenameDialog(TWeakObjectPtr<UBlueprint> WeakBP)
    {
        UBlueprint* BP = WeakBP.Get();
        FGameplayTag Current;
        bool bGenerated = false;
        if (!SeinAutoTag::ReadAssetIdentity(BP, Current, bGenerated)) return;
        const auto Input = SNew(SEditableTextBox).Text(FText::FromString(Current.ToString()));
        const auto Window = SNew(SWindow).Title(LOCTEXT("RenameTitle", "Rename Identity Tag"))
            .ClientSize(FVector2D(650, 150)).SupportsMinimize(false).SupportsMaximize(false);
        const TWeakPtr<SWindow> WeakWindow = Window;
        Window->SetContent(SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(12)
            [SNew(STextBlock).Text(LOCTEXT("RenameHelp", "Enter the new root tag. All descendants move with it. Redirects preserve older references."))]
            + SVerticalBox::Slot().AutoHeight().Padding(12, 0)[Input]
            + SVerticalBox::Slot().AutoHeight().Padding(12)
            [SNew(SButton).Text(LOCTEXT("RenameApply", "Review and Rename"))
                .OnClicked_Lambda([WeakBP, WeakWindow, Input]()
                {
                    if (UBlueprint* Blueprint = WeakBP.Get())
                    {
                        const FText Confirm = FText::Format(LOCTEXT("RenameConfirm",
                            "Rename this identity and ALL descendant tags to {0}? The hierarchy will be preserved. Loaded references will be updated and persistent redirects will preserve older serialized references. Save the changed assets afterward. This operation clears editor undo history because dictionary changes cannot be undone with assets."), Input->GetText());
                        if (FMessageDialog::Open(EAppMsgType::YesNo, Confirm) == EAppReturnType::Yes)
                        {
                            const auto Result = SeinAutoTag::RenameAssetTag(Blueprint, FName(*Input->GetText().ToString()));
                            ShowRegenerationResult(*Blueprint, Result);
                            if (!Result.IsFailure()) if (auto Pinned = WeakWindow.Pin()) Pinned->RequestDestroyWindow();
                        }
                    }
                    return FReply::Handled();
                })]);
        FSlateApplication::Get().AddWindow(Window);
    }

	/** Walk objects-being-customized back to the owning UBlueprint. CDOs are
	 *  typically what's customized; their outer is the UClass, whose
	 *  ClassGeneratedBy is the BP. */
	static UBlueprint* ResolveOwningBlueprint(IDetailLayoutBuilder& DetailBuilder)
	{
		TArray<TWeakObjectPtr<UObject>> Objects;
		DetailBuilder.GetObjectsBeingCustomized(Objects);
		for (const TWeakObjectPtr<UObject>& WeakObj : Objects)
		{
			UObject* Obj = WeakObj.Get();
			if (!Obj) continue;
            if (UBlueprint* BP = BlueprintForObject(Obj)) return BP;
		}
		return nullptr;
	}

	/** Initialize missing identities or open the explicit migration dialog. */
	static TSharedRef<SWidget> MakeResetButton(TWeakObjectPtr<UBlueprint> WeakBP)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("ResetToAutoTooltip",
					"Generate the identity from this asset name and migrate its previous generated tag and references when needed."))
				.OnClicked_Lambda([WeakBP]() -> FReply
				{
					if (UBlueprint* BP = WeakBP.Get())
					{
						const FSeinAutoTagRegenerationResult Result =
							SeinAutoTag::RegenerateAssetTagDetailed(
								BP,
								/*bForceOverManual=*/true);
						ShowRegenerationResult(*BP, Result);
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Text(LOCTEXT("ResetToAutoLabel", "Initialize Tag"))
				]
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
            [SNew(SButton).Text(LOCTEXT("RenameTag", "Rename Tag..."))
                .OnClicked_Lambda([WeakBP]() { ShowRenameDialog(WeakBP); return FReply::Handled(); })];
    }

	/** Add a "Initialize Tag" row inside `Category`, hidden when no BP is
	 *  resolvable (defensive — the asset is being viewed without a Blueprint
	 *  context, e.g. native CDO). */
	static void AddResetRowToCategory(IDetailCategoryBuilder& Category, IDetailLayoutBuilder& DetailBuilder)
	{
		TWeakObjectPtr<UBlueprint> WeakBP = ResolveOwningBlueprint(DetailBuilder);
		if (!WeakBP.IsValid()) return;
		Category.AddCustomRow(LOCTEXT("ResetToAutoRowFilter", "Initialize Tag"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AutoTagRowLabel", "Auto Tag"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			MakeResetButton(WeakBP)
		];
	}
}

// ─────────────────────────────────────────────────────────────────────
// USeinAbility — adds "Initialize Tag" beside the general ability fields.
// ─────────────────────────────────────────────────────────────────────

TSharedRef<IDetailCustomization> FSeinAbilityAutoTagDetails::MakeInstance()
{
	return MakeShared<FSeinAbilityAutoTagDetails>();
}

void FSeinAbilityAutoTagDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& AbilityCategory = DetailBuilder.EditCategory(TEXT("General"));
	SeinAutoTagDetailsLocal::AddResetRowToCategory(AbilityCategory, DetailBuilder);
}

// ─────────────────────────────────────────────────────────────────────
// USeinEffect — adds "Initialize Tag" beside the general effect fields.
// ─────────────────────────────────────────────────────────────────────

TSharedRef<IDetailCustomization> FSeinEffectAutoTagDetails::MakeInstance()
{
	return MakeShared<FSeinEffectAutoTagDetails>();
}

void FSeinEffectAutoTagDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& EffectCategory = DetailBuilder.EditCategory(TEXT("General"));
	SeinAutoTagDetailsLocal::AddResetRowToCategory(EffectCategory, DetailBuilder);
}

// ─────────────────────────────────────────────────────────────────────
// FSeinIdentityPayload — property-type customization. Adds the Reset
// button as the last child row after the struct's authored fields.
// ─────────────────────────────────────────────────────────────────────

TSharedRef<IPropertyTypeCustomization> FSeinIdentityComponentAutoTagDetails::MakeInstance()
{
	return MakeShared<FSeinIdentityComponentAutoTagDetails>();
}

void FSeinIdentityComponentAutoTagDetails::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& /*CustomizationUtils*/)
{
	// Collapse the header row entirely. The FInstancedStruct customization
	// (`FSeinInstancedStructDetails`) that owns the outer "Index [N] → Sein
	// Identity Component" row already labels this entry; a second "Sein
	// Identity Component" header below it is redundant. EVisibility::Collapsed
	// removes the row from the layout. Children still render via
	// CustomizeChildren — UE's struct-customization pipeline doesn't gate
	// the children area on header visibility. (Mirrors the same fix on
	// FSeinCoverComponentDetails.)
	HeaderRow.Visibility(EVisibility::Collapsed);
}

void FSeinIdentityComponentAutoTagDetails::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& /*CustomizationUtils*/)
{
	// Default-render every authored field — we just want to APPEND a button row.
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);
	for (uint32 i = 0; i < NumChildren; ++i)
	{
		TSharedPtr<IPropertyHandle> Child = PropertyHandle->GetChildHandle(i);
		if (Child.IsValid() && Child->GetProperty())
		{
			ChildBuilder.AddProperty(Child.ToSharedRef());
		}
	}

	// Resolve the owning BP via the property's owning object chain.
	TArray<UObject*> OuterObjects;
	PropertyHandle->GetOuterObjects(OuterObjects);
	UBlueprint* OwningBP = nullptr;
	for (UObject* Obj : OuterObjects)
	{
		if (!Obj) continue;
        OwningBP = SeinAutoTagDetailsLocal::BlueprintForObject(Obj);
        if (OwningBP) break;
	}
	if (!OwningBP) return;
	TWeakObjectPtr<UBlueprint> WeakBP = OwningBP;

	ChildBuilder.AddCustomRow(LOCTEXT("IdentityResetRowFilter", "Initialize Tag"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("AutoTagRowLabel2", "Auto Tag"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SeinAutoTagDetailsLocal::MakeResetButton(WeakBP)
	];
}

void SeinAutoTagDetails::AddIdentityActions(IDetailLayoutBuilder& DetailBuilder)
{
    SeinAutoTagDetailsLocal::AddResetRowToCategory(DetailBuilder.EditCategory(TEXT("SeinARTS")), DetailBuilder);
}

#undef LOCTEXT_NAMESPACE

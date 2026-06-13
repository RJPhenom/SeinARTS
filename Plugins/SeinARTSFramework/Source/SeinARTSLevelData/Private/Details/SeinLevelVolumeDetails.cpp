/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLevelVolumeDetails.cpp
 */

#include "Details/SeinLevelVolumeDetails.h"

#if WITH_EDITOR

#include "Volumes/SeinLevelVolume.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailGroup.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "SeinLevelVolumeDetails"

TSharedRef<IDetailCustomization> FSeinLevelVolumeDetails::MakeInstance()
{
	return MakeShared<FSeinLevelVolumeDetails>();
}

void FSeinLevelVolumeDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Edit the shared parent category — the same "SeinARTS" category that hosts
	// Navigation / Fog Of War (those nest via "SeinARTS|..." property categories;
	// BakedAsset's plain "SeinARTS" category guarantees the parent exists here).
	IDetailCategoryBuilder& SeinCategory = DetailBuilder.EditCategory(
		TEXT("SeinARTS"), FText::GetEmpty(), ECategoryPriority::Default);

	// A "Bake" sub-group: the bake button above its baked-asset output.
	IDetailGroup& BakeGroup = SeinCategory.AddGroup(
		TEXT("Bake"), LOCTEXT("BakeGroupLabel", "Bake"),
		/*bForAdvanced*/ false, /*bStartExpanded*/ true);

	// Capture the volume(s) by weak ptr for the click handler (the DetailBuilder
	// reference is not safe to hold past CustomizeDetails).
	TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
	DetailBuilder.GetObjectsBeingCustomized(CustomizedObjects);

	// --- Bake Level Data button (above the asset) ---
	BakeGroup.AddWidgetRow()
	.FilterString(LOCTEXT("BakeButtonFilter", "Bake Level Data"))
	.WholeRowContent()
	[
		SNew(SBox)
		.Padding(FMargin(0.f, 2.f))
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.ToolTipText(LOCTEXT("BakeButtonTip",
				"Bake the unified level data covering every Sein Level Volume in this level "
				"(shared trace pass + all registered layer providers)."))
			.OnClicked_Lambda([CustomizedObjects]() -> FReply
			{
				for (const TWeakObjectPtr<UObject>& Obj : CustomizedObjects)
				{
					if (ASeinLevelVolume* Volume = Cast<ASeinLevelVolume>(Obj.Get()))
					{
						// BeginBake is world-global — one call bakes every volume.
						Volume->BakeLevelData();
						break;
					}
				}
				return FReply::Handled();
			})
			[
				SNew(SBox)
				.Padding(FMargin(3.f))  // breathing room around the label inside the button
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BakeButtonLabel", "Bake Level Data"))
					.Justification(ETextJustify::Center)
				]
			]
		]
	];

	// --- Baked Asset (below the button) ---
	// Authored at plain "SeinARTS"; hide the default row and re-add it inside the
	// Bake group so it sits directly under the button.
	const TSharedRef<IPropertyHandle> BakedAssetHandle =
		DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ASeinLevelVolume, BakedAsset));
	DetailBuilder.HideProperty(BakedAssetHandle);
	BakeGroup.AddPropertyRow(BakedAssetHandle);
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_EDITOR

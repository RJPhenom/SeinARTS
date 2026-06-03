#include "Editor/SeinNavVolumeDetails.h"

#include "Volumes/SeinNavVolume.h"
#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "SeinARTSNavigationEditor"

TSharedRef<IDetailCustomization> FSeinNavVolumeDetails::MakeInstance()
{
	return MakeShared<FSeinNavVolumeDetails>();
}

void FSeinNavVolumeDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	CachedWorld.Reset();
	ASeinNavVolume* CachedVolume = nullptr;
	for (const TWeakObjectPtr<UObject>& Obj : Objects)
	{
		if (ASeinNavVolume* Vol = Cast<ASeinNavVolume>(Obj.Get()))
		{
			CachedWorld = Vol->GetWorld();
			CachedVolume = Vol;
			break;
		}
	}

	// Add a "Build" subcategory under "SeinARTS" with the Rebuild button.
	// Using the pipe-nested category name puts the bake control beside the
	// volume's own SeinARTS|Overrides / Output / Debug subcategories under
	// a single SeinARTS top-level — keeps the detail panel coherent instead
	// of having a separate top-level for the bake button.
	IDetailCategoryBuilder& BuildCategory = DetailBuilder.EditCategory(
		TEXT("SeinARTS|Build"), LOCTEXT("SeinNavBuildCategory", "Build"),
		ECategoryPriority::Important);

	BuildCategory.AddCustomRow(LOCTEXT("RebuildRowFilter", "Rebuild"))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("RebuildDesc",
				"Build the nav grid covering every Sein Nav Volume in this level.\n"
				"Bake runs in the background — cancel or watch progress in the toast."))
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 4.0f)
		[
			SNew(SBox)
			.HeightOverride(32.0f)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.IsEnabled_Raw(this, &FSeinNavVolumeDetails::IsRebuildEnabled)
				.OnClicked_Raw(this, &FSeinNavVolumeDetails::OnRebuildClicked)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RebuildButton", "Bake Navigation"))
				]
			]
		]
	];

	// Forward to the active nav impl so subclasses can extend the panel
	// (per-bake options, multi-stage UX, etc.). Default impl is a no-op so
	// subclasses that don't override see no change. The framework's bake
	// button above stays first for consistent UX.
	if (UWorld* World = CachedWorld.Get())
	{
		if (USeinNavigation* Nav = USeinNavigationSubsystem::GetNavigationForWorld(World))
		{
			Nav->CustomizeVolumeDetails(DetailBuilder, CachedVolume);
		}
	}
}

FReply FSeinNavVolumeDetails::OnRebuildClicked()
{
	if (UWorld* World = CachedWorld.Get())
	{
		USeinNavigationSubsystem::BeginBake(World);
	}
	return FReply::Handled();
}

bool FSeinNavVolumeDetails::IsRebuildEnabled() const
{
	UWorld* World = CachedWorld.Get();
	return World && !USeinNavigationSubsystem::IsBaking(World);
}

#undef LOCTEXT_NAMESPACE

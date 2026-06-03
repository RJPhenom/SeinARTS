/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		AssetDefinition_SeinWidgetBlueprint.cpp
 * @date:		4/16/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of the SeinARTS Widget Blueprint asset definition.
 */

#include "Definitions/AssetDefinition_SeinWidgetBlueprint.h"
#include "Widgets/SeinWidgetBlueprint.h"
#include "WidgetBlueprintEditor.h"
#include "Engine/Blueprint.h"
#include "Misc/MessageDialog.h"
#include "Toolkits/IToolkitHost.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

FText UAssetDefinition_SeinWidgetBlueprint::GetAssetDisplayName() const
{
	return LOCTEXT("SeinWidgetBlueprintDisplayName", "SeinARTS Widget");
}

FLinearColor UAssetDefinition_SeinWidgetBlueprint::GetAssetColor() const
{
	return FLinearColor(FColor::FromHex(TEXT("0095FF")));
}

TSoftClassPtr<UObject> UAssetDefinition_SeinWidgetBlueprint::GetAssetClass() const
{
	return USeinWidgetBlueprint::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_SeinWidgetBlueprint::GetAssetCategories() const
{
	static const TArray<FAssetCategoryPath, TFixedAllocator<1>> Categories = {
		FAssetCategoryPath(LOCTEXT("SeinARTSCategory", "SeinARTS"))
	};
	return Categories;
}

EAssetCommandResult UAssetDefinition_SeinWidgetBlueprint::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	// Mirrors UAssetDefinition_WidgetBlueprint::OpenAssets (UE 5.7). Creating
	// FWidgetBlueprintEditor is what wires up the Designer / Graph / Preview
	// application modes — without this path the asset opens in the generic
	// Blueprint editor and the Designer tab is missing.
	const EToolkitMode::Type Mode = OpenArgs.GetToolkitMode();
	EAssetCommandResult Result = EAssetCommandResult::Unhandled;

	for (UBlueprint* Blueprint : OpenArgs.LoadObjects<UBlueprint>())
	{
		if (Blueprint && Blueprint->SkeletonGeneratedClass && Blueprint->GeneratedClass)
		{
			TSharedRef<FWidgetBlueprintEditor> NewEditor(new FWidgetBlueprintEditor);
			TArray<UBlueprint*> Blueprints;
			Blueprints.Add(Blueprint);
			NewEditor->InitWidgetBlueprintEditor(Mode, OpenArgs.ToolkitHost, Blueprints, /*bShouldOpenInDefaultsMode=*/false);
		}
		else
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT(
				"FailedToLoadSeinWidgetBlueprint",
				"SeinARTS Widget Blueprint could not be loaded because it derives from an invalid class.\nCheck to make sure the parent class for this blueprint hasn't been removed!"));
		}

		Result = EAssetCommandResult::Handled;
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE

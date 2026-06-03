/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		AssetDefinition_SeinWidgetBlueprint.h
 * @date:		4/16/2026
 * @author:		RJ Macklem
 * @brief:		Asset definition for USeinWidgetBlueprint. Opens the UMG
 *				designer (FWidgetBlueprintEditor) when the asset is opened,
 *				giving SeinARTS Widget Blueprints the full Designer + Graph
 *				+ Palette UX of stock Widget Blueprints.
 */

#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"
#include "AssetDefinition_SeinWidgetBlueprint.generated.h"

/**
 * Asset definition for USeinWidgetBlueprint.
 *
 * UE 5.7 removed the public UAssetDefinition_WidgetBlueprint header, so we
 * subclass UAssetDefinitionDefault directly and replicate UMG's OpenAssets
 * flow (creating FWidgetBlueprintEditor) ourselves.
 */
UCLASS()
class UAssetDefinition_SeinWidgetBlueprint : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
};

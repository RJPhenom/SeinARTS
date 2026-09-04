/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinAssetDefinitions.h
 * @brief:		Asset definitions for the SeinARTS Blueprint + data asset types.
 *				Owns each type's display name, color, and create-menu section
 *				(the grey headers inside the SeinARTS flyout). These replaced
 *				the legacy FAssetTypeActions classes: only a UAssetDefinition
 *				can declare an ECategoryMenuType::Section, and the legacy
 *				proxy converts sub-menus to nested flyouts instead.
 *
 *				Sections: Balance | Behaviour Policies | Core | User Interface
 *				(the widget definition lives in AssetDefinition_SeinWidgetBlueprint).
 *				Registration is explicit in FSeinARTSEditorModule so Startup and
 *				PreUnload stay symmetric across module reloads.
 */

#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"
#include "SeinAssetDefinitions.generated.h"

class UBlueprint;

/**
 * Shared base for every Sein Blueprint-class asset definition. Restores the
 * two behaviors the legacy FAssetTypeActions_Blueprint base classes provided
 * and UAssetDefinitionDefault does NOT: opening the asset in the real
 * Blueprint editor (the Default fallback is the generic property grid — no
 * viewport, no graphs), and revision diffs through the Blueprint differ.
 * Mirrors FAssetTypeActions_Blueprint::OpenAssetEditor, including the
 * data-only defaults-view fast path for graphless Blueprints.
 */
UCLASS(Abstract)
class UAssetDefinition_SeinBlueprintBase : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
	virtual EAssetCommandResult PerformAssetDiff(const FAssetDiffArgs& DiffArgs) const override;

protected:
	static bool ShouldUseDataOnlyEditor(const UBlueprint* Blueprint);
};

/**
 * Asset definition for Unit (SeinActor) Blueprints.
 * Color: #0095FF (Blue). Section: Core.
 */
UCLASS()
class UAssetDefinition_SeinActorBlueprint : public UAssetDefinition_SeinBlueprintBase
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
};

/**
 * Asset definition for Sein entity component Blueprints
 * (USeinEntityComponentBlueprint — data-only authoring components).
 * Color: #FF8000 (Orange). Section: Core.
 */
UCLASS()
class UAssetDefinition_SeinEntityComponentBlueprint : public UAssetDefinition_SeinBlueprintBase
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;

	/** Opens the dedicated data-only editor (variables + defaults, no graph
	 *  surfaces) instead of the full Blueprint editor — the lock-in half of
	 *  the data-only contract; the compile gate is the belt. */
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
};

/**
 * Asset definition for Ability (SeinAbility) Blueprints.
 * Color: #FF0000 (Red). Section: Core.
 */
UCLASS()
class UAssetDefinition_SeinAbilityBlueprint : public UAssetDefinition_SeinBlueprintBase
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
};

/**
 * Asset definition for Effect (USeinEffect) Blueprints.
 * Color: #FF0000 (Red — matches Ability). Section: Core.
 */
UCLASS()
class UAssetDefinition_SeinEffectBlueprint : public UAssetDefinition_SeinBlueprintBase
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
};

/**
 * Asset definition for Formation (USeinFormation) Blueprints.
 * Color: #0095FF (Blue — matches Entity Blueprint). Section: Behaviour Policies.
 */
UCLASS()
class UAssetDefinition_SeinFormationBlueprint : public UAssetDefinition_SeinBlueprintBase
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
};

/**
 * Asset definition for Movement Mode (USeinMovementBlueprint) Blueprints.
 * Color: #0095FF (Blue — matches Entity Blueprint). Section: Behaviour Policies.
 * The asset class is a soft path so the editor module keeps no link dependency
 * on the Movement module; with that module absent the path never resolves and
 * the definition simply never matches an asset.
 */
UCLASS()
class UAssetDefinition_SeinMovementBlueprint : public UAssetDefinition_SeinBlueprintBase
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
};

/**
 * Asset definition for the Balance Profile data asset (USeinBalanceProfile).
 * Color: #B266FF (Purple). Section: Balance.
 */
UCLASS()
class UAssetDefinition_SeinBalanceProfile : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
};

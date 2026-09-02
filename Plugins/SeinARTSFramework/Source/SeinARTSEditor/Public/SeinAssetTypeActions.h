/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinAssetTypeActions.h
 * @date:		4/13/2026
 * @author:		RJ Macklem
 * @brief:		Asset type actions for SeinARTS Blueprint types.
 *				Provides per-type colors and category registration.
 */

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions/AssetTypeActions_Blueprint.h"
#include "AssetTypeActions_Base.h"

/**
 * Asset type actions for Unit (SeinActor) Blueprints.
 * Color: #0095FF (Blue)
 */
class FAssetTypeActions_SeinActorBlueprint : public FAssetTypeActions_Blueprint
{
public:
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override { return FColor::FromHex(TEXT("0095FF")); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;
};

/**
 * Asset type actions for Sein entity component Blueprints
 * (USeinEntityComponentBlueprint — data-only authoring components).
 * Color: #FF8000 (Orange)
 */
class FAssetTypeActions_SeinEntityComponentBlueprint : public FAssetTypeActions_Blueprint
{
public:
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override { return FColor::FromHex(TEXT("FF8000")); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;

	/** Opens the dedicated data-only editor (variables + defaults, no graph
	 *  surfaces) instead of the full Blueprint editor — the lock-in half of
	 *  the data-only contract; the compile gate is the belt. */
	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects,
		TSharedPtr<IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>()) override;
};

/**
 * Asset type actions for Ability (SeinAbility) Blueprints.
 * Color: #FF0000 (Red)
 */
class FAssetTypeActions_SeinAbilityBlueprint : public FAssetTypeActions_Blueprint
{
public:
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override { return FColor::FromHex(TEXT("FF0000")); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;
};

/**
 * Asset type actions for Effect (USeinEffect) Blueprints.
 * Color: #FF0000 (Red — matches Ability)
 */
class FAssetTypeActions_SeinEffectBlueprint : public FAssetTypeActions_Blueprint
{
public:
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override { return FColor::FromHex(TEXT("FF0000")); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;
};

/**
 * Asset type actions for Formation (USeinFormation) Blueprints.
 * Color: #0095FF (Blue — matches Entity Blueprint)
 */
class FAssetTypeActions_SeinFormationBlueprint : public FAssetTypeActions_Blueprint
{
public:
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override { return FColor::FromHex(TEXT("0095FF")); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;
};

/**
 * Asset type actions for Movement Mode (USeinMovementBlueprint) Blueprints.
 * Color: #0095FF (Blue — matches Entity Blueprint).
 * The supported class is resolved by path so the editor module keeps no link
 * dependency on the Movement module; the module skips registration when it is absent.
 */
class FAssetTypeActions_SeinMovementBlueprint : public FAssetTypeActions_Blueprint
{
public:
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override { return FColor::FromHex(TEXT("0095FF")); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;
};

/**
 * Asset type actions for the Balance Profile data asset (USeinBalanceProfile).
 * Not a Blueprint type — derives from FAssetTypeActions_Base.
 * Color: #B266FF (Purple)
 */
class FAssetTypeActions_SeinBalanceProfile : public FAssetTypeActions_Base
{
public:
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override { return FColor::FromHex(TEXT("B266FF")); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;
};

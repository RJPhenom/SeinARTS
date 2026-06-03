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
 * Color: #FFFF00 (Yellow)
 */
class FAssetTypeActions_SeinEffectBlueprint : public FAssetTypeActions_Blueprint
{
public:
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override { return FColor::FromHex(TEXT("FFFF00")); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;
};

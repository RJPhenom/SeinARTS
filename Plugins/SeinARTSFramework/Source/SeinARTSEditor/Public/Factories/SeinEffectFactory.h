/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinEffectFactory.h
 * @brief:		Factory for creating SeinEffect Blueprint classes via Content Browser.
 */

#pragma once

#include "CoreMinimal.h"
#include "Factories/BlueprintFactory.h"
#include "SeinEffectFactory.generated.h"

UCLASS()
class USeinEffectFactory : public UBlueprintFactory
{
	GENERATED_BODY()

public:
	USeinEffectFactory();

	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext) override;
	virtual bool ConfigureProperties() override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
	virtual FName GetNewAssetThumbnailOverride() const override { return TEXT("ClassThumbnail.SeinEffect"); }
	virtual FName GetNewAssetIconOverride() const override { return TEXT("ClassIcon.SeinEffect"); }
};

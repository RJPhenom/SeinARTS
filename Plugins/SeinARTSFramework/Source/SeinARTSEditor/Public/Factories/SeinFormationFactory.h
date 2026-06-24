/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinFormationFactory.h
 * @brief:		Factory for creating SeinFormation Blueprint classes via Content Browser.
 */

#pragma once

#include "CoreMinimal.h"
#include "Factories/BlueprintFactory.h"
#include "SeinFormationFactory.generated.h"

UCLASS()
class USeinFormationFactory : public UBlueprintFactory
{
	GENERATED_BODY()

public:
	USeinFormationFactory();

	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext) override;
	virtual bool ConfigureProperties() override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
	virtual FName GetNewAssetThumbnailOverride() const override { return TEXT("ClassThumbnail.SeinFormation"); }
	virtual FName GetNewAssetIconOverride() const override { return TEXT("ClassIcon.SeinFormation"); }
};

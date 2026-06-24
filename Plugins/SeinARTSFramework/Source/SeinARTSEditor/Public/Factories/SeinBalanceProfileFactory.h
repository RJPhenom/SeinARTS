/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBalanceProfileFactory.h
 * @brief   Right-click → SeinARTS → Balance Profile. Creates a USeinBalanceProfile
 *          data asset and opens it for editing. Auto-discovered via bCreateNew —
 *          no explicit module registration needed (mirrors USeinSimComponentFactory).
 */

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "SeinBalanceProfileFactory.generated.h"

UCLASS()
class USeinBalanceProfileFactory : public UFactory
{
	GENERATED_BODY()

public:
	USeinBalanceProfileFactory();

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
	virtual FName GetNewAssetThumbnailOverride() const override { return TEXT("ClassThumbnail.SeinBalanceProfile"); }
	virtual FName GetNewAssetIconOverride() const override { return TEXT("ClassIcon.SeinBalanceProfile"); }
};

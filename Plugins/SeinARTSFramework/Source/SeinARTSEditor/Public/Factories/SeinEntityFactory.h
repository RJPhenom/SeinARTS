/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinEntityFactory.h
 * @brief:		Factory for creating SeinActor Blueprint classes — the bare
 *				entity template (Entity Bridge with authored ComponentData array). Designers
 *				compose additional sim components (Abilities, Movement, Combat
 *				replacement, etc.) via the Components panel. Combat in particular
 *				is intentionally designer-overridden per DESIGN §11, so no
 *				pre-seeded starter kit is shipped.
 */

#pragma once

#include "CoreMinimal.h"
#include "Factories/BlueprintFactory.h"
#include "SeinEntityFactory.generated.h"

UCLASS()
class USeinEntityFactory : public UBlueprintFactory
{
	GENERATED_BODY()

public:
	USeinEntityFactory();

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext) override;
	virtual bool ConfigureProperties() override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
	virtual FName GetNewAssetThumbnailOverride() const override { return TEXT("ClassThumbnail.SeinActor"); }
	virtual FName GetNewAssetIconOverride() const override { return TEXT("ClassIcon.SeinActor"); }
};

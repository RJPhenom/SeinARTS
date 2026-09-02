/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDataComponentBlueprintFactory.h
 * @brief   AC-authoring prototype: Content Browser factory for designer data
 *          components — a Blueprint subclass of USeinEntityComponent whose
 *          variables become the injected payload (auto-synced to a paired
 *          UserDefinedStruct on compile; see SeinDataComponentSync). The
 *          class is data-only by contract: the compile gate errors on any
 *          graph content.
 */

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "SeinDataComponentBlueprintFactory.generated.h"

UCLASS(HideCategories = Object)
class SEINARTSEDITOR_API USeinDataComponentBlueprintFactory : public UFactory
{
	GENERATED_BODY()

public:
	USeinDataComponentBlueprintFactory();

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent,
		FName Name, EObjectFlags Flags, UObject* Context,
		FFeedbackContext* Warn) override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinWidgetBlueprintFactory.h
 * @date:		4/16/2026
 * @author:		RJ Macklem
 * @brief:		Factory for creating SeinUserWidget-derived Widget Blueprints
 *				via the Content Browser. Subclasses UWidgetBlueprintFactory so
 *				widget tree setup, UMG broadcast, and project-settings hooks
 *				all match the engine factory — we only override what's needed
 *				to produce a USeinWidgetBlueprint with a USeinUserWidget parent.
 */

#pragma once

#include "CoreMinimal.h"
#include "WidgetBlueprintFactory.h"
#include "SeinWidgetBlueprintFactory.generated.h"

/**
 * Factory that creates Widget Blueprints derived from USeinUserWidget.
 * Produces a USeinWidgetBlueprint asset so the editor can apply a
 * SeinARTS-specific color and icon while still opening the UMG designer.
 * Appears in the Content Browser under "SeinARTS" (+ Basic if enabled).
 */
UCLASS()
class USeinWidgetBlueprintFactory : public UWidgetBlueprintFactory
{
	GENERATED_BODY()

public:
	USeinWidgetBlueprintFactory();

	// UFactory interface
	virtual bool ConfigureProperties() override;
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext) override;
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
	virtual FName GetNewAssetThumbnailOverride() const override { return TEXT("ClassThumbnail.SeinWidgetBlueprint"); }
};

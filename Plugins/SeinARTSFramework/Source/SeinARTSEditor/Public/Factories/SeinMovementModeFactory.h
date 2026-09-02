/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementModeFactory.h
 * @brief   Content-Browser factory for a "Movement Mode" — a Blueprint
 *          pre-parented to USeinMovement, so the designer gets the base ComputeMotion
 *          policy to build on — override Compute Motion (Tier 1) to return a custom
 *          desired velocity + facing, or override the whole Tick (Tier 2) to drive the
 *          unit yourself with the Mover Handle toolkit. Pair it with tuning variables +
 *          the Class-Defaults "Sync Tuning Struct" button to generate the matching UDS.
 *
 *          Auto-discovered (a bCreateNew UFactory needs no explicit registration). The
 *          parent class (USeinMovement) and asset class (USeinMovementBlueprint) are
 *          resolved by path so the editor module keeps no link dependency on the
 *          Movement module.
 */

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "SeinMovementModeFactory.generated.h"

UCLASS()
class USeinMovementModeFactory : public UFactory
{
	GENERATED_BODY()

public:
	USeinMovementModeFactory();

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual FText GetToolTip() const override;

	// Create-menu icon + thumbnail (resolved from the SeinARTS editor style set — see
	// SeinARTSEditorStyle.cpp). Without these the entry falls back to the generic Blueprint icon.
	virtual FName GetNewAssetThumbnailOverride() const override { return TEXT("ClassThumbnail.SeinMovement"); }
	virtual FName GetNewAssetIconOverride() const override { return TEXT("ClassIcon.SeinMovement"); }
};

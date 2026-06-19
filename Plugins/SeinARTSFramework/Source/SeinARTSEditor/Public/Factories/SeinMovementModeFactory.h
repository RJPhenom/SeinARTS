/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementModeFactory.h
 * @brief   Content-Browser factory for a "SeinARTS Movement Mode" — a Blueprint
 *          pre-parented to USeinMovement, so the designer gets the RTS-default loop
 *          (USeinMovement::BP_Tick_Implementation) to build on and can override the
 *          Compute Steer / Compute Desired Speed hooks (Tier 1) or the whole Tick
 *          (Tier 2). Pair it with tuning variables + the Class-Defaults "Sync Tuning
 *          Struct" button to generate the matching UDS.
 *
 *          Auto-discovered (a bCreateNew UFactory needs no explicit registration). The
 *          parent class is resolved by path so the editor module keeps no link
 *          dependency on the Movement module.
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
};

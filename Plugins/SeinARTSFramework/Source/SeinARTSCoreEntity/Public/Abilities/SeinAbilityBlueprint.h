/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinAbilityBlueprint.h
 * @date:		4/13/2026
 * @author:		RJ Macklem
 * @brief:		Blueprint asset subclass for SeinAbility types.
 *				Exists so the editor can assign a unique asset type color,
 *				icon, and thumbnail to Ability Blueprints.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/Blueprint.h"
#include "SeinAbilityBlueprint.generated.h"

/**
 * Thin UBlueprint subclass that identifies a Blueprint as a SeinAbility.
 * Follows the same pattern as UAnimBlueprint / UWidgetBlueprint — gives the
 * editor a unique SupportedClass for asset type actions, colors, and filtering.
 */
UCLASS(BlueprintType)
class SEINARTSCOREENTITY_API USeinAbilityBlueprint : public UBlueprint
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual bool SupportedByDefaultBlueprintFactory() const override { return false; }
#endif
};

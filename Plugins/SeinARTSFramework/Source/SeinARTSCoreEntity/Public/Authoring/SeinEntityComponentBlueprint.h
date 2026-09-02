/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinEntityComponentBlueprint.h
 * @date:    9/1/2026
 * @author:  RJ Macklem
 * @brief:   Blueprint asset subclass for designer-authored Sein entity
 *           components (USeinDataComponent subclasses). Exists so the editor
 *           can assign the asset its own type color (#FF8000), the
 *           SeinComponentIcon thumbnail, and the data-only editing surface —
 *           same pattern as USeinActorBlueprint / USeinAbilityBlueprint.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/Blueprint.h"
#include "SeinEntityComponentBlueprint.generated.h"

/**
 * Thin UBlueprint subclass identifying a Blueprint as a Sein entity component.
 * Data-only by contract: the compile gate errors on any graph content, and
 * the asset opens in the dedicated variables + defaults editor.
 */
UCLASS(BlueprintType)
class SEINARTSCOREENTITY_API USeinEntityComponentBlueprint : public UBlueprint
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual bool SupportedByDefaultBlueprintFactory() const override { return false; }
#endif
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFormationBlueprint.h
 * @brief   Thin UBlueprint subclass that identifies a Blueprint as a SeinFormation. Mirrors the
 *          SeinAbilityBlueprint / SeinEffectBlueprint pattern so the editor can attach asset type
 *          actions, color, icon, and thumbnail renderer specific to formations.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/Blueprint.h"
#include "SeinFormationBlueprint.generated.h"

/**
 * Tags a Blueprint asset as a SeinARTS formation so the editor treats it like one. You do not pick
 * this class directly; it is the asset type behind formation Blueprints in the Content Browser.
 *
 * A thin UBlueprint subclass carrying no data or logic of its own. Its only job is to give
 * formation Blueprints a distinct asset type, so the editor can attach formation-specific asset
 * actions, color, icon, and thumbnail renderer to them. It mirrors the Sein Ability Blueprint and
 * Sein Effect Blueprint pattern. SupportedByDefaultBlueprintFactory returns false so the generic
 * Blueprint factory never offers to create one; formation Blueprints are created through their own
 * dedicated factory instead.
 */
UCLASS(BlueprintType)
class SEINARTSCOREENTITY_API USeinFormationBlueprint : public UBlueprint
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual bool SupportedByDefaultBlueprintFactory() const override { return false; }
#endif
};

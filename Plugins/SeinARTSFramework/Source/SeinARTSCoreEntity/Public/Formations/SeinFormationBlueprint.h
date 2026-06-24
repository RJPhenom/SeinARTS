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

UCLASS(BlueprintType)
class SEINARTSCOREENTITY_API USeinFormationBlueprint : public UBlueprint
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual bool SupportedByDefaultBlueprintFactory() const override { return false; }
#endif
};

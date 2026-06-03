/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEffectBlueprint.h
 * @brief   Thin UBlueprint subclass that identifies a Blueprint as a
 *          SeinEffect. Mirrors the SeinAbilityBlueprint / SeinActorBlueprint
 *          pattern so the editor can attach asset type actions, color, icon,
 *          and thumbnail renderer specific to effects.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/Blueprint.h"
#include "SeinEffectBlueprint.generated.h"

UCLASS(BlueprintType)
class SEINARTSCOREENTITY_API USeinEffectBlueprint : public UBlueprint
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual bool SupportedByDefaultBlueprintFactory() const override { return false; }
#endif
};

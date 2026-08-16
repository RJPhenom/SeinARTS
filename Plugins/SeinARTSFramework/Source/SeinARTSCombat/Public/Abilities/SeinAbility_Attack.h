/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAbility_Attack.h
 * @brief   Starter-content attack: fire every ready weapon at the commanded
 *          target until it dies. Deliberately bland — no chasing, no target
 *          switching, no doctrine; that is a game's engagement policy to
 *          author (subclass in Blueprint, or replace outright). Out-of-range
 *          activation approach is the base ability's own Out Of Range
 *          Behavior; weapons that cannot legally fire this tick simply hold.
 */

#pragma once

#include "CoreMinimal.h"
#include "Abilities/SeinAbility.h"
#include "SeinAbility_Attack.generated.h"

UCLASS(meta = (DisplayName = "SeinARTS Attack Ability"))
class SEINARTSCOMBAT_API USeinAbility_Attack : public USeinAbility
{
	GENERATED_BODY()

public:
	USeinAbility_Attack();

	virtual bool CanActivate_Implementation() override;
	virtual void OnTick_Implementation(FFixedPoint DeltaTime) override;
};

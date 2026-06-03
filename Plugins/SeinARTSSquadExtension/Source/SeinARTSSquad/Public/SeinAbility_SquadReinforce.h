/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAbility_SquadReinforce.h
 * @brief   Starter-content squad ability: reinforce one missing member.
 *          Lives on the squad entity. CoH-style behavior — fills the first
 *          empty slot in declaration order, charges the slot's ReinforceCost
 *          from the owning player at enqueue, then the squad system progresses
 *          the reinforce entry over the slot's ReinforceBuildTime and spawns
 *          the new member at the squad's transform when complete.
 *
 *          Ships under the SeinARTS.Starter.SquadTactics.* namespace per
 *          the framework's starter-content policy. Designers use as-is or subclass
 *          in BP for project-specific gating (faction restrictions, custom
 *          fail VO, etc.).
 */

#pragma once

#include "CoreMinimal.h"
#include "Abilities/SeinAbility.h"
#include "SeinAbility_SquadReinforce.generated.h"

UCLASS(meta = (DisplayName = "SeinARTS Squad Reinforce Ability"))
class SEINARTSSQUAD_API USeinAbility_SquadReinforce : public USeinAbility
{
	GENERATED_BODY()

public:
	USeinAbility_SquadReinforce();

	virtual bool CanActivate_Implementation() override;
	virtual void OnActivate_Implementation() override;
};

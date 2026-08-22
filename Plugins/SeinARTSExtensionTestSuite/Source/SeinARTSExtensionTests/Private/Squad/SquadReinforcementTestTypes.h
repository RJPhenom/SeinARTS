/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SquadReinforcementTestTypes.h
 * @author       RJ Macklem
 * @created      22 Aug 2026
 * @latest       22 Aug 2026
 * @brief        Declares authored test types for Squad reinforcement lifecycle tests.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "SeinAbility_SquadReinforce.h"
#include "SquadReinforcementTestTypes.generated.h"

/** Authored-tag subclass of the shipped starter reinforcement ability. */
UCLASS()
class USeinSquadReinforcementCommandTestAbility final
	: public USeinAbility_SquadReinforce
{
	GENERATED_BODY()

public:
	USeinSquadReinforcementCommandTestAbility();
};

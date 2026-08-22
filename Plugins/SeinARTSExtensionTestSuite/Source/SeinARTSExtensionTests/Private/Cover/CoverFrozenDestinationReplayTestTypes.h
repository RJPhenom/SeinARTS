/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         CoverFrozenDestinationReplayTestTypes.h
 * @author       RJ Macklem
 * @created      22 Aug 2026
 * @latest       22 Aug 2026
 * @brief        Declares the authored move ability used by Cover replay tests.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "Abilities/SeinAbility.h"
#include "CoverFrozenDestinationReplayTestTypes.generated.h"

/** Authored ground-move ability that exercises the production Move To action. */
UCLASS()
class USeinCoverFrozenDestinationReplayMoveAbility final : public USeinAbility
{
	GENERATED_BODY()

public:
	USeinCoverFrozenDestinationReplayMoveAbility();

	virtual void OnActivate_Implementation() override;
	virtual void OnTick_Implementation(FFixedPoint DeltaTime) override;
};

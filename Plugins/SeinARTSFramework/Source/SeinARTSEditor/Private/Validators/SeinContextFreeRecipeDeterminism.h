/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinContextFreeRecipeDeterminism.h
 * @brief   Shared, side-effect-free Blueprint analysis for canonical-state recipes.
 */

#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UClass;

namespace SeinContextFreeRecipeDeterminism
{
	/**
	 * Validate the Blueprint behavior inherited by one recipe class.
	 *
	 * The analysis follows the two recipe entry points through local/inherited
	 * helper graphs, pure Blueprint Function Library composition, collapsed
	 * graphs, and macro expansions. It never compiles, saves, registers, or
	 * otherwise mutates the Blueprint.
	 */
	bool ValidateClass(
		const UClass* RecipeClass,
		TArray<FText>& OutDiagnostics);

	/** Validate one recipe Blueprint plus any Blueprint recipe parents. */
	bool ValidateBlueprint(
		const UBlueprint* Blueprint,
		TArray<FText>& OutDiagnostics);
}

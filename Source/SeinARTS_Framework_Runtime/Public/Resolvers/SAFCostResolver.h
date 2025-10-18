#pragma once

#include "CoreMinimal.h"
#include "Structs/SAFResources.h"

class UAbilitySystemComponent;
class USAFAttributes;
class APlayerState;
class USAFUnitAsset;

/**
 * SAFCostResolver
 *
 * Utility namespace for resolving cost bundles for units.
 * Order of resolution:
 *   1. If AbilitySystem has USAFAttributes, return live bundle from ASC.
 *   2. Else, fall back to the UnitAsset’s RuntimeCosts (requires PlayerState).
 *   3. Returns empty FSAFResources if both fail.
 */
namespace SAFCostResolver {
	SEINARTS_FRAMEWORK_RUNTIME_API FSAFResources ResolveCosts(
		const UAbilitySystemComponent* AbilitySystem,
		const USAFUnitAsset* UnitAsset,
		const APlayerState* PlayerState
	);
}

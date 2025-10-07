#pragma once

#include "CoreMinimal.h"

class UAbilitySystemComponent;
class USAFAttributes;
class USAFAsset;
struct FSAFAttributesRow;

/**
 * SAFSpacingResolver
 *
 * Utility namespace for resolving formation spacing.
 * Tries in order:
 *   1. Live value from AbilitySystem, if any (USAFAttributes::GetFormationSpacing).
 *   2. Value from the AttributeRow on the Asset.
 *   3. Falls back to 50.f if none valid.
 */
namespace SAFSpacingResolver {
	SEINARTS_FRAMEWORK_RUNTIME_API float ResolveSpacing(const UAbilitySystemComponent* AbilitySystem, const USAFAsset* Asset);
}

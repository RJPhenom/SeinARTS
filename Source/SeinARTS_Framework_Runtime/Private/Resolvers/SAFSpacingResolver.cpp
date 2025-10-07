#include "Resolvers/SAFSpacingResolver.h"
#include "AbilitySystemComponent.h"
#include "Gameplay/Attributes/SAFUnitAttributes.h"
#include "Assets/Units/SAFUnitAsset.h"
#include "Structs/SAFAttributesRow.h"

/**
 * SAFSpacingResolver
 *
 * Utility namespace for resolving formation spacing.
 * Tries in order:
 *   1. Live value from AbilitySystem, if any (USAFAttributes::GetFormationSpacing).
 *   2. Value from the AttributeRow on the Asset.
 *   3. Falls back to 50.f if none valid.
 */
float SAFSpacingResolver::ResolveSpacing(const UAbilitySystemComponent* AbilitySystem, const USAFAsset* Asset) {
	if (AbilitySystem) {
		if (const USAFUnitAttributes* Attribute = AbilitySystem->GetSet<USAFUnitAttributes>()) {
			const float Value = Attribute->GetFormationSpacing();
			if (FMath::IsFinite(Value)) return Value;
		}
	}

	return Asset->FallbackSpacing;
}

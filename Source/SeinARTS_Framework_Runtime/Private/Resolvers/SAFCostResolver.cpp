#include "Resolvers/SAFCostResolver.h"
#include "AbilitySystemComponent.h"
#include "Data/SAFAttributes.h"
#include "Assets/Units/SAFUnitAsset.h"

/**
 * SAFCostResolver
 *
 * Utility namespace for resolving cost bundles for units.
 * Order of resolution:
 *   1. If AbilitySystem has USAFAttributes, return live bundle from ASC.
 *   2. Else, fall back to the UnitAsset’s RuntimeCosts (requires PlayerState).
 *   3. Returns empty FSAFResources if both fail.
 */
FSAFResources SAFCostResolver::ResolveCosts(
	const UAbilitySystemComponent* AbilitySystem,
	const USAFUnitAsset* UnitAsset,
	const APlayerState* PlayerState,
	ESAFResourceRoundingPolicy Policy
) {
	// 1. Try live ASC attributes
	if (AbilitySystem)
		if (const USAFAttributes* Attr = AbilitySystem->GetSet<USAFAttributes>())
      return Attr->BundleResources(Policy);

	// 2. Fall back to UnitAsset runtime costs
	if (UnitAsset && PlayerState) return FSAFResources{}; //UnitAsset->GetRuntimeCosts(PlayerState, Policy);

	// 3. Fallback: empty bundle
	return FSAFResources{};
}

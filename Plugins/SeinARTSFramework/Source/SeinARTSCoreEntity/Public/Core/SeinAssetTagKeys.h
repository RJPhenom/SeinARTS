/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinAssetTagKeys.h
 * @brief:   Shared AssetRegistry tag keys that surface a Sein asset's identity
 *           gameplay tag onto its FAssetData, so editor tooling can read it
 *           WITHOUT loading the asset (and its CDO).
 *
 *           Emitted by `USeinAbility` / `USeinEffect` / `ASeinActor`
 *           `GetAssetRegistryTags` overrides (runtime module); read by the
 *           editor auto-tag collision check (`SeinAutoTagGenerator`). The
 *           stored value is the tag's bare `FGameplayTag::ToString()` form, so
 *           a string compare against another tag's `ToString()` is a valid
 *           equality test.
 *
 *           Accessors are Meyers singletons rather than namespace-scope FName
 *           globals to sidestep any static-init-order question around the FName
 *           pool — the name is interned on first use, which is always well
 *           after engine startup for these editor/save-time call sites.
 */

#pragma once

#include "CoreMinimal.h"

namespace SeinAssetTagKeys
{
	/** `USeinAbility::AbilityTag` (direct CDO property). */
	inline FName AbilityTag()  { static const FName Key(TEXT("SeinAbilityTag"));  return Key; }

	/** `USeinEffect::EffectTag` (direct CDO property). */
	inline FName EffectTag()   { static const FName Key(TEXT("SeinEffectTag"));   return Key; }

	/** `FSeinIdentityComponent::IdentityTag`, nested in the entity bridge's
	 *  `ComponentData` array on an `ASeinActor` CDO. */
	inline FName IdentityTag() { static const FName Key(TEXT("SeinIdentityTag")); return Key; }
}

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCanonicalStatePropertyPolicy.h
 * @brief   Cooked-stable reflected-state exclusion policy.
 */

#pragma once

#include "CoreMinimal.h"

class FProperty;

/**
 * Shared property policy for canonical reflected state.
 *
 * Custom UPROPERTY metadata is stripped when WITH_METADATA is false, so it
 * cannot define a lockstep schema. This policy uses only cooked flags, property
 * types, and exact native field identities and therefore produces the same
 * result in Editor, Development, and Shipping.
 */
class SEINARTSCOREENTITY_API FSeinCanonicalStatePropertyPolicy
{
public:
	static bool ShouldSkip(const FProperty& Property);
};

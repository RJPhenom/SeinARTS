/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinAbilityDeterminismValidationTestTypes.h
 * @author       RJ Macklem
 * @created      14 Aug 2026
 * @latest       14 Aug 2026
 * @brief        Reflection fixtures for Ability determinism validation tests.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "SeinAbilityDeterminismValidationTestTypes.generated.h"

UCLASS(meta = (SeinPresentationOnly))
class USeinPresentationOnlyValidationLibrary
	: public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, meta = (SeinDeterministic))
	static int32 DeterministicSignatureOnPresentationOwner()
	{
		return 7;
	}
};

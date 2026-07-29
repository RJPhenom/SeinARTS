/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementCanonicalState.h
 * @brief   Exact reflected continuation state for movement policies.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "SeinMovementCanonicalState.generated.h"

/** One exact class instance encoded with full tagged UPROPERTY state. */
USTRUCT(meta = (SeinDeterministic))
struct FSeinMovementPolicyObjectState
{
	GENERATED_BODY()

	UPROPERTY()
	FString ExactClassPath;

	UPROPERTY()
	FGuid ReflectedSchemaDigest;

	UPROPERTY()
	FGuid ReflectedValueDigest;

	UPROPERTY()
	TArray<uint8> StateBytes;
};

/** One persistent per-entity movement instance in full-handle order. */
USTRUCT(meta = (SeinDeterministic))
struct FSeinMovementPolicyInstanceState
{
	GENERATED_BODY()

	UPROPERTY()
	FSeinEntityHandle Entity;

	UPROPERTY()
	FSeinMovementPolicyObjectState Object;
};

/**
 * Canonical state owned by USeinMovementSubsystem.
 *
 * CoverageIdentity/Claims bind the exact native-layer completeness contract
 * into both the descriptor and payload. Blueprint variables remain ordinary
 * reflected state and require no framework-specific save/load hook.
 */
USTRUCT(meta = (SeinDeterministic))
struct FSeinMovementCanonicalState
{
	GENERATED_BODY()

	UPROPERTY()
	FName CoverageIdentity;

	UPROPERTY()
	TArray<FString> CoverageClaims;

	UPROPERTY()
	bool bHasAvoidance = false;

	UPROPERTY()
	FSeinMovementPolicyObjectState Avoidance;

	UPROPERTY()
	TArray<FSeinMovementPolicyInstanceState> MovementInstances;
};

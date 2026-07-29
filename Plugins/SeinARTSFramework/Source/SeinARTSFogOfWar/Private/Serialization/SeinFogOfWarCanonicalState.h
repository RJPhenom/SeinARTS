/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarCanonicalState.h
 * @brief   Universal bounded envelope for any fog implementation.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinFogOfWarCanonicalState.generated.h"

/**
 * The only FoW payload Core sees. Concrete state bytes remain owned and
 * decoded by the exact implementation codec generation selected by the world.
 */
USTRUCT(meta = (SeinDeterministic))
struct FSeinFogOfWarCanonicalStateEnvelope
{
	GENERATED_BODY()

	UPROPERTY()
	bool bEnabled = false;

	UPROPERTY()
	FString ImplementationClassPath;

	UPROPERTY()
	FString StableImplementationId;

	UPROPERTY()
	uint32 StateSchemaVersion = 0;

	UPROPERTY()
	uint32 BehaviorRevision = 0;

	UPROPERTY()
	uint32 CodecRevision = 0;

	UPROPERTY()
	FGuid PayloadSchemaDigest;

	UPROPERTY()
	FGuid CodecDescriptorDigest;

	UPROPERTY()
	FGuid StaticEnvironmentDigest;

	UPROPERTY()
	int32 MaxRecursionDepth = 0;

	UPROPERTY()
	int32 MaxPayloadBytes = 0;

	UPROPERTY()
	int32 MaxAggregateElements = 0;

	UPROPERTY()
	TArray<uint8> PayloadBytes;
};

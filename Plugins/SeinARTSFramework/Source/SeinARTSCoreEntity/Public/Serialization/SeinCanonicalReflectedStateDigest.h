/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCanonicalReflectedStateDigest.h
 * @brief   Digest-only canonical projection of live reflected sim state.
 */

#pragma once

#include "CoreMinimal.h"

class UClass;
class UObject;
class UScriptStruct;
class UStruct;

/**
 * Resource limits for one reflected root. Unordered-container entries are
 * independently reduced to leaf digests before canonical sorting, so the
 * aggregate byte ceiling is also independent of native hash-table layout.
 */
struct SEINARTSCOREENTITY_API FSeinCanonicalReflectedStateLimits
{
	int32 MaxAggregateElements = 1024 * 1024;
	int32 MaxStringCharacters = 1024 * 1024;
	int32 MaxTotalStringCharacters = 8 * 1024 * 1024;
	int32 MaxRecursionDepth = 64;
	int32 MaxInstancedObjects = 4096;
};

/**
 * Additive digest-only companion to FSeinCanonicalStateCodec.
 *
 * This projection is deliberately not a reversible wire format. It hashes
 * trusted live state using textual reflected identities, canonical unordered
 * containers, stable asset/class paths, and recursively projected instanced
 * subobjects. Existing command/replay/state codec bytes are therefore not
 * changed by this API. Reflected fields are authoritative by default.
 * Exclusions come from FSeinCanonicalStatePropertyPolicy, whose cooked flags,
 * types, and exact native identities are deliberately identical in Editor and
 * Shipping; editor-only UPROPERTY metadata never defines lockstep state.
 */
class SEINARTSCOREENTITY_API FSeinCanonicalReflectedStateDigest
{
public:
	static constexpr uint32 CurrentFormatVersion = 2;

	/** Hash the exact supported reflected shape of a struct or class. */
	static bool ComputeSchemaDigest(
		const UStruct* Type,
		const FSeinCanonicalReflectedStateLimits& Limits,
		FGuid& OutDigest,
		FString& OutError);

	/**
	 * Hash one struct value against a schema digest already computed for Type.
	 * Supplying the schema separately lets callers cache it per component type.
	 */
	static bool ComputeStructValueDigest(
		const UScriptStruct* Type,
		const void* StructMemory,
		const FGuid& SchemaDigest,
		const FSeinCanonicalReflectedStateLimits& Limits,
		FGuid& OutDigest,
		FString& OutError);

	/** Hash one UObject's reflected value without including outer/name identity. */
	static bool ComputeObjectValueDigest(
		const UObject* Object,
		const FGuid& SchemaDigest,
		const FSeinCanonicalReflectedStateLimits& Limits,
		FGuid& OutDigest,
		FString& OutError);
};

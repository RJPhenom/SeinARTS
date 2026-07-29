/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDeterministicValueDigest.h
 * @brief   Canonical reflected-value digest for deterministic simulation data.
 */

#pragma once

#include "CoreMinimal.h"

struct FInstancedStruct;

/** Stable failure vocabulary for the canonical reflected-value encoder. */
enum class ESeinDeterministicValueDigestResult : uint8
{
	Success,
	InvalidOptions,
	InvalidRoot,
	NonDeterministicStruct,
	UnsupportedProperty,
	InvalidInstancedStruct,
	RecursionLimitExceeded,
	ByteLimitExceeded,
	ElementLimitExceeded,
};

/** Resource limits applied before hashing. They bound both hostile data and mistakes. */
struct SEINARTSCOREENTITY_API FSeinDeterministicValueDigestOptions
{
	/** Maximum nesting below the root value. The root itself is depth zero. */
	int32 MaxRecursionDepth = 64;

	/** Maximum size of any canonical encoded value, including the root stream. */
	uint64 MaxEncodedBytes = 16ULL * 1024ULL * 1024ULL;

	/** Maximum aggregate dynamic-container entries visited by one digest. */
	uint64 MaxAggregateElements = 1024ULL * 1024ULL;

	/**
	 * UField metadata is unavailable in cooked builds. Leave this false to fail
	 * closed. A Shipping caller may set it true only after a frozen compatibility
	 * manifest has admitted every concrete type path and editor validation has
	 * proved those declarations SeinDeterministic.
	 */
	bool bTrustCookedTypesWithoutMetadata = false;
};

/** Deterministic diagnostic returned on failure. Empty on success. */
struct SEINARTSCOREENTITY_API FSeinDeterministicValueDigestError
{
	ESeinDeterministicValueDigestResult Result =
		ESeinDeterministicValueDigestResult::Success;
	FString FieldPath;
	FString Message;

	void Reset()
	{
		Result = ESeinDeterministicValueDigestResult::Success;
		FieldPath.Reset();
		Message.Reset();
	}
};

/**
 * Encodes a reflected deterministic value into a versioned canonical byte stream,
 * then returns its BLAKE3-128 digest as an explicitly big-endian FGuid.
 *
 * The root and every ordinary nested UScriptStruct must carry
 * `meta=(SeinDeterministic)`. FGameplayTag, FGameplayTagContainer, and the
 * FInstancedStruct wrapper are supported engine primitives; every concrete
 * FInstancedStruct value is validated independently and framed by type path.
 *
 * Arrays retain authored order. Set and map entries are encoded independently,
 * lexicographically sorted by their complete canonical bytes, then framed into
 * the parent stream. UObject/class/soft references, floating point, FText, and
 * every unknown reflected property kind fail closed.
 */
class SEINARTSCOREENTITY_API FSeinDeterministicValueDigest
{
public:
	static ESeinDeterministicValueDigestResult Compute(
		const UScriptStruct* Struct,
		const void* StructMemory,
		FGuid& OutDigest,
		FSeinDeterministicValueDigestError* OutError = nullptr,
		const FSeinDeterministicValueDigestOptions& Options = {});

	static ESeinDeterministicValueDigestResult Compute(
		const FInstancedStruct& Value,
		FGuid& OutDigest,
		FSeinDeterministicValueDigestError* OutError = nullptr,
		const FSeinDeterministicValueDigestOptions& Options = {});
};

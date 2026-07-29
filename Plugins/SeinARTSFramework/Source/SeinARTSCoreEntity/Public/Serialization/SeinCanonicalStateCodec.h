/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCanonicalStateCodec.h
 * @brief   Bounded canonical serialization for deterministic reflected state.
 */

#pragma once

#include "CoreMinimal.h"

class FProperty;
class UClass;
class UObject;

/** Return true when a reflected UObject property belongs on the bounded wire. */
using FSeinWirePropertyFilter = bool (*)(const FProperty& Property);

/**
 * Consensus-safe cost plus the receiver-local native storage charge needed to
 * decode the same value. CanonicalCostBytes is exact wire bytes plus a frozen
 * charge per logical container element; it never depends on C++ layout.
 * NativeAllocationBytes deliberately does depend on the receiving build and is
 * used only for its local hostile-allocation ceiling. It conservatively
 * includes container growth slack and bounded UTF conversion scratch.
 */
struct SEINARTSCOREENTITY_API FSeinWireCost
{
	static constexpr uint64 CanonicalBytesPerLogicalElement = 16;

	uint64 CanonicalCostBytes = 0;
	uint64 NativeAllocationBytes = 0;
};

/**
 * Limits applied before any variable-sized reflected value is allocated.
 * MaxBytes bounds the wire representation. MaxNativeAllocationBytes is an
 * orthogonal receiver-local ceiling; a negative value uses MaxBytes for both
 * limits.
 */
struct SEINARTSCOREENTITY_API FSeinStructWireLimits
{
	int32 MaxBytes = 4096;
	int32 MaxAggregateElements = 256;
	int32 MaxStringBytes = 1024;
	int32 MaxRecursionDepth = 64;
	int32 MaxNativeAllocationBytes = INDEX_NONE;
};

/** Trusted, already-canonical catalogs used by the reflected struct wire. */
struct SEINARTSCOREENTITY_API FSeinStructWireCatalogView
{
	TConstArrayView<const UScriptStruct*> DynamicStructs;
	TConstArrayView<FName> Names;
};

/**
 * Reversible deterministic-struct codec shared by canonical state, commands,
 * and replay metadata.
 *
 * The caller supplies the trusted root type. Nested FInstancedStruct and raw
 * FName values are represented only by indices into the supplied frozen
 * catalogs; no object path or name text is resolved from input. Arrays,
 * strings, optionals, and tag containers are length-checked against both the
 * remaining byte slice and aggregate limits before they can allocate.
 */
class SEINARTSCOREENTITY_API FSeinCanonicalStateCodec
{
public:
	/**
	 * BLAKE3-128 identity of the exact reflected shape consumed by this codec.
	 * Editor-only fields are omitted so Editor and cooked builds agree; every
	 * other field must be supported by the bounded wire or the call fails.
	 */
	static bool ComputeSchemaDigest(
		const UScriptStruct* Struct,
		FGuid& OutDigest,
		FString& OutError);

	static bool EncodeWithCost(
		const UScriptStruct* Struct,
		const void* StructMemory,
		FSeinStructWireCatalogView Catalog,
		const FSeinStructWireLimits& Limits,
		TArray<uint8>& OutBytes,
		FString& OutError,
		FSeinWireCost& OutCost);

	/** Convenience wrapper returning only the receiver-local native charge. */
	static bool Encode(
		const UScriptStruct* Struct,
		const void* StructMemory,
		FSeinStructWireCatalogView Catalog,
		const FSeinStructWireLimits& Limits,
		TArray<uint8>& OutBytes,
		FString& OutError,
		uint64* OutDecodedAllocationBytes = nullptr);

	/** Transactional: OutStructMemory is changed only after a complete decode. */
	static bool DecodeWithCost(
		TConstArrayView<uint8> Bytes,
		const UScriptStruct* Struct,
		void* OutStructMemory,
		FSeinStructWireCatalogView Catalog,
		const FSeinStructWireLimits& Limits,
		FString& OutError,
		FSeinWireCost& OutCost);

	/** Transactional: OutStructMemory is changed only after a complete decode. */
	static bool Decode(
		TConstArrayView<uint8> Bytes,
		const UScriptStruct* Struct,
		void* OutStructMemory,
		FSeinStructWireCatalogView Catalog,
		const FSeinStructWireLimits& Limits,
		FString& OutError,
		uint64* OutDecodedAllocationBytes = nullptr);

	/**
	 * Validate and encode a UObject's selected reflected properties through the
	 * same bounded wire used for deterministic structs. The filter is applied
	 * recursively; excluded fields keep their class-default value on restore.
	 */
	static bool ValidateObjectClass(
		const UClass* Class,
		FSeinWirePropertyFilter PropertyFilter,
		FString& OutError);

	/** Digest of the exact filtered, order-sensitive UObject wire layout. */
	static bool ComputeObjectSchemaDigest(
		const UClass* Class,
		FSeinWirePropertyFilter PropertyFilter,
		FGuid& OutDigest,
		FString& OutError);

	static bool EncodeObject(
		const UObject& Object,
		FSeinStructWireCatalogView Catalog,
		const FSeinStructWireLimits& Limits,
		FSeinWirePropertyFilter PropertyFilter,
		TArray<uint8>& OutBytes,
		FString& OutError,
		uint64* OutDecodedAllocationBytes = nullptr);

	/**
	 * Decode directly into a newly constructed, non-authoritative candidate.
	 * The caller owns transactionality by discarding the candidate on failure.
	 */
	static bool DecodeObject(
		TConstArrayView<uint8> Bytes,
		UObject& OutObject,
		FSeinStructWireCatalogView Catalog,
		const FSeinStructWireLimits& Limits,
		FSeinWirePropertyFilter PropertyFilter,
		FString& OutError,
		uint64* OutDecodedAllocationBytes = nullptr);
};

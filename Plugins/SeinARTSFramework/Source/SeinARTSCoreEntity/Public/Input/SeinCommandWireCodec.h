/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandWireCodec.h
 * @brief   Bounded, schema-selected command serialization for hostile inputs.
 */

#pragma once

#include "CoreMinimal.h"
#include "Input/SeinCommandSchemaRegistry.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "Templates/Function.h"

using FSeinCommandWireSchemaLookup =
	TFunctionRef<bool(FGameplayTag, int32, FSeinCommandSchemaDescriptor&)>;

/** Opaque command envelope codec. The schema key is parsed before its body. */
class SEINARTSCOREENTITY_API FSeinCommandWireCodec
{
public:
	static constexpr int32 MaxWireCommandBytes = 16 * 1024 * 1024;
	static constexpr int32 MaxDecodedCommandAllocationBytes = MaxWireCommandBytes * 2;

	static bool EncodeWithCost(
		const FSeinCommand& Command,
		const FSeinCommandSchemaDescriptor& Schema,
		TArray<uint8>& OutBytes,
		FString& OutError,
		FSeinWireCost& OutCost);

	/**
	 * Compatibility wrapper returning the receiver-local native decoder charge.
	 * Consensus-facing admission must use EncodeWithCost and CanonicalCostBytes.
	 */
	static bool Encode(
		const FSeinCommand& Command,
		const FSeinCommandSchemaDescriptor& Schema,
		TArray<uint8>& OutBytes,
		FString& OutError,
		uint64* OutDecodedAllocationBytes = nullptr);

	/** Transactional and subject to a receiver-local native-allocation ceiling. */
	static bool DecodeWithCost(
		TConstArrayView<uint8> Bytes,
		FSeinCommandWireSchemaLookup FindSchema,
		FSeinCommand& OutCommand,
		FString& OutError,
		int32 MaxNativeAllocationBytes,
		FSeinWireCost& OutCost);

	/** Compatibility wrapper returning only the native allocation charge. */
	static bool Decode(
		TConstArrayView<uint8> Bytes,
		FSeinCommandWireSchemaLookup FindSchema,
		FSeinCommand& OutCommand,
		FString& OutError,
		int32 MaxDecodedAllocationBytes = MaxDecodedCommandAllocationBytes,
		uint64* OutDecodedAllocationBytes = nullptr);
};

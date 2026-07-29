/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file SeinNetCommandWireCodec.h
 * @brief Bounded opaque batch framing shared by command RPC directions.
 */

#pragma once

#include "CoreMinimal.h"
#include "Input/SeinCommandWireCodec.h"
#include "SeinNetProtocolTypes.h"

class SEINARTSNET_API FSeinNetCommandWireCodec
{
public:
	/** Frozen consensus cost and receiver-local native ceilings are independent. */
	static constexpr uint64 CostExpansionFactor = 4;
	static constexpr uint64 MaxCanonicalCostBytes =
		static_cast<uint64>(FSeinOpaqueCommandBatch::MaxBytes)
		* CostExpansionFactor;
	static constexpr uint64 MaxNativeAllocationBytes =
		static_cast<uint64>(FSeinOpaqueCommandBatch::MaxBytes)
		* CostExpansionFactor;
	/** Compatibility alias for callers not yet migrated to dual-cost accounting. */
	static constexpr uint64 MaxDecodedAllocationBytes = MaxNativeAllocationBytes;
	static constexpr int32 FixedBatchHeaderBytes = 11;

	static bool EncodeDraftsWithCost(
		TConstArrayView<FSeinCommandSubmissionDraft> Drafts,
		int32 MaxCommands,
		FSeinCommandWireSchemaLookup FindSchema,
		FSeinOpaqueCommandBatch& OutBatch,
		FString& OutError,
		FSeinWireCost& OutCost);

	static bool EncodeDrafts(
		TConstArrayView<FSeinCommandSubmissionDraft> Drafts,
		int32 MaxCommands,
		FSeinCommandWireSchemaLookup FindSchema,
		FSeinOpaqueCommandBatch& OutBatch,
		FString& OutError,
		uint64* OutDecodedAllocationBytes = nullptr);

	static bool DecodeDraftsWithCost(
		const FSeinOpaqueCommandBatch& Batch,
		int32 MaxCommands,
		FSeinCommandWireSchemaLookup FindSchema,
		TArray<FSeinCommandSubmissionDraft>& OutDrafts,
		FString& OutError,
		FSeinWireCost& OutCost,
		uint64 NativeAllocationLimit = MaxNativeAllocationBytes);

	static bool DecodeDrafts(
		const FSeinOpaqueCommandBatch& Batch,
		int32 MaxCommands,
		FSeinCommandWireSchemaLookup FindSchema,
		TArray<FSeinCommandSubmissionDraft>& OutDrafts,
		FString& OutError,
		uint64* OutDecodedAllocationBytes = nullptr,
		uint64 DecodedAllocationLimit = MaxDecodedAllocationBytes);

	static bool EncodeCommandsWithCost(
		TConstArrayView<FSeinCommand> Commands,
		int32 MaxCommands,
		FSeinCommandWireSchemaLookup FindSchema,
		FSeinOpaqueCommandBatch& OutBatch,
		FString& OutError,
		FSeinWireCost& OutCost);

	static bool EncodeCommands(
		TConstArrayView<FSeinCommand> Commands,
		int32 MaxCommands,
		FSeinCommandWireSchemaLookup FindSchema,
		FSeinOpaqueCommandBatch& OutBatch,
		FString& OutError,
		uint64* OutDecodedAllocationBytes = nullptr);

	static bool DecodeCommandsWithCost(
		const FSeinOpaqueCommandBatch& Batch,
		int32 MaxCommands,
		FSeinCommandWireSchemaLookup FindSchema,
		TArray<FSeinCommand>& OutCommands,
		FString& OutError,
		FSeinWireCost& OutCost,
		uint64 NativeAllocationLimit = MaxNativeAllocationBytes);

	static bool DecodeCommands(
		const FSeinOpaqueCommandBatch& Batch,
		int32 MaxCommands,
		FSeinCommandWireSchemaLookup FindSchema,
		TArray<FSeinCommand>& OutCommands,
		FString& OutError,
		uint64* OutDecodedAllocationBytes = nullptr);
};

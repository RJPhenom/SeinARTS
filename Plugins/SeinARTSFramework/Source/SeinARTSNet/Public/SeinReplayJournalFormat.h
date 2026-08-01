/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file SeinReplayJournalFormat.h
 * @brief Bounded, append-only v9 replay-journal framing.
 */

#pragma once

#include "CoreMinimal.h"
#include "Serialization/SeinSnapshotEnvelopeCodec.h"
#include "SeinNetProtocolTypes.h"
#include "Simulation/SeinMatchBootstrapBarrier.h"

namespace SeinReplayJournalFormat
{
	constexpr uint32 FileFormatVersion = 9;
	constexpr uint16 FrameFormatVersion = 1;
	constexpr int32 PrefixBytes = 152;
	constexpr int32 FrameHeaderBytes = 64;
	constexpr int32 FrameDigestOffset = 48;
	constexpr int32 MaxTurnRecordsPerBatch = 1024;
	constexpr int32 FrontierPayloadBytes = 16;
	constexpr uint64 MaxFileBytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;
	/** Reader/writer shared ceiling for fixed frame descriptors/sequences. */
	constexpr uint64 MaxFrameCount = 1000000ULL;
	constexpr uint64 MaxHeaderPayloadBytes = 64ULL * 1024ULL * 1024ULL;
	constexpr uint64 MaxTurnBatchPayloadBytes = 64ULL * 1024ULL * 1024ULL;
	constexpr uint64 MaxCheckpointPayloadBytes =
		static_cast<uint64>(FSeinSnapshotEnvelopeCodec::PrefixBytes)
		+ FSeinSnapshotEnvelopeCodec::MaxBodyBytes;
	constexpr uint8 Magic[8] = {'S', 'E', 'I', 'N', 'R', 'P', 'L', '9'};
	constexpr uint8 FrameMagic[4] = {'S', 'R', 'F', '9'};

	/** One integrity-checked compatibility/identity prefix. */
	struct SEINARTSNET_API FPrefix
	{
		FGuid CommandProtocolDigest;
		FGuid MatchSettingsDigest;
		FSeinMatchBootstrapReceipt BootstrapReceipt;
		int32 ConfigFingerprint = 0;
		FGuid JournalID;
		FGuid PrefixDigest;
	};

	enum class EFrameType : uint8
	{
		Header = 1,
		TurnBatch = 2,
		Checkpoint = 3,
		Progress = 4,
		Finalize = 5,
	};

	/** Parsed metadata for one independently bounded, hash-chained frame. */
	struct SEINARTSNET_API FFrameHeader
	{
		EFrameType Type = EFrameType::Header;
		uint8 Flags = 0;
		uint64 Sequence = 0;
		int32 FirstTurn = INDEX_NONE;
		int32 LastTurn = INDEX_NONE;
		int32 TimelineTick = 0;
		uint32 PayloadBytes = 0;
		FGuid PreviousDigest;
		FGuid CurrentDigest;
	};

	/** Exact opaque fan-out bytes for one canonical assembled turn. */
	struct SEINARTSNET_API FTurnRecord
	{
		int32 TurnId = INDEX_NONE;
		FSeinOpaqueCommandBatch OpaqueCommands;
	};

	/** Durable inclusive playback frontier carried by Progress and Finalize. */
	struct SEINARTSNET_API FFrontier
	{
		int32 EndTick = 0;
		int32 FirstAppliedTurn = INDEX_NONE;
		int32 LastAppliedTurn = INDEX_NONE;
		uint32 AppliedTurnCount = 0;
	};

	/** Build exactly one digest-bound 152-byte journal prefix. Outputs are transactional. */
	SEINARTSNET_API bool BuildPrefix(
		const FGuid& CommandProtocolDigest,
		const FGuid& MatchSettingsDigest,
		const FSeinMatchBootstrapReceipt& BootstrapReceipt,
		int32 ConfigFingerprint,
		const FGuid& JournalID,
		TArray<uint8>& OutBytes,
		FPrefix& OutPrefix,
		FString& OutError);

	/** Parse and verify exactly one prefix. OutPrefix changes only on success. */
	SEINARTSNET_API bool ParsePrefix(
		TConstArrayView<uint8> Bytes,
		FPrefix& OutPrefix,
		FString& OutError);

	/**
	 * Build one complete frame (64-byte header followed by payload). The current
	 * digest binds every header field before it, including PreviousDigest, plus
	 * the exact payload bytes. Outputs change only on success.
	 */
	SEINARTSNET_API bool BuildFrame(
		EFrameType Type,
		uint8 Flags,
		uint64 Sequence,
		int32 FirstTurn,
		int32 LastTurn,
		int32 TimelineTick,
		const FGuid& PreviousDigest,
		TConstArrayView<uint8> Payload,
		TArray<uint8>& OutFrameBytes,
		FFrameHeader& OutHeader,
		FString& OutError);

	/** Prefix-first admission for exactly one 64-byte frame header. */
	SEINARTSNET_API bool ParseFrameHeader(
		TConstArrayView<uint8> Bytes,
		FFrameHeader& OutHeader,
		FString& OutError);

	/** Validate a parsed header against its exact payload without concatenating buffers. */
	SEINARTSNET_API bool ValidateFrame(
		const FFrameHeader& Header,
		TConstArrayView<uint8> Payload,
		FString& OutError);

	/** Parse and fully validate one complete header+payload frame. */
	SEINARTSNET_API bool ValidateFrame(
		TConstArrayView<uint8> FrameBytes,
		FFrameHeader& OutHeader,
		FString& OutError);

	/** Encode/decode a non-empty, contiguous batch of at most 1024 opaque turns. */
	SEINARTSNET_API bool EncodeTurnBatch(
		TConstArrayView<FTurnRecord> Records,
		TArray<uint8>& OutBytes,
		FString& OutError);
	SEINARTSNET_API bool DecodeTurnBatch(
		TConstArrayView<uint8> Bytes,
		TArray<FTurnRecord>& OutRecords,
		FString& OutError);

	/** Encode/decode the exact 16-byte Progress/Finalize payload. */
	SEINARTSNET_API bool EncodeFrontier(
		const FFrontier& Frontier,
		TArray<uint8>& OutBytes,
		FString& OutError);
	SEINARTSNET_API bool DecodeFrontier(
		TConstArrayView<uint8> Bytes,
		FFrontier& OutFrontier,
		FString& OutError);
}

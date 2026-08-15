/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSnapshotEnvelopeCodec.h
 * @brief   Bounded canonical framing for snapshot-v16 section payloads.
 */

#pragma once

#include "CoreMinimal.h"

/** Persistence behavior of one canonical snapshot section. */
enum class ESeinSnapshotSectionRole : uint8
{
	/** Persistent state that affects future fixed ticks. */
	Authoritative = 1,

	/** In-flight deterministic execution that affects future fixed ticks. */
	Continuation = 2,

	/**
	 * Descriptor-only claim for state rebuilt synchronously after restore.
	 * Derived-cache sections carry no payload and do not enter the aggregate
	 * state root. Their directory entry and leaf remain body-digest protected.
	 */
	DerivedCache = 3,

	/**
	 * Local presentation state. It is protected by the leaf and exact-body
	 * digests but deliberately excluded from the aggregate state root.
	 */
	Local = 4,
};

/** Payload byte contract. Snapshot v16 supports no compression. */
enum class ESeinSnapshotSectionCodec : uint8
{
	CanonicalBytes = 1,
};

/** One caller-owned section before encoding, or one validated decoded section. */
struct SEINARTSCOREENTITY_API FSeinSnapshotEnvelopeSection
{
	/**
	 * Stable lowercase-ASCII identifier. The accepted alphabet is
	 * [a-z0-9._/-], with an alphanumeric first and last character.
	 */
	FString SectionId;

	ESeinSnapshotSectionRole Role =
		ESeinSnapshotSectionRole::Authoritative;
	ESeinSnapshotSectionCodec Codec =
		ESeinSnapshotSectionCodec::CanonicalBytes;

	/** Non-zero version of this section's payload schema. */
	uint32 SchemaVersion = 1;

	/** Digest of the exact canonical payload schema. */
	FGuid SchemaDigest;

	/** Digest of the full section descriptor, including limits and ownership. */
	FGuid DescriptorDigest;

	/** Already-canonical section payload. Empty payloads are legal. */
	TArray<uint8> Payload;
};

/** Semantic input/output represented by the snapshot-v16 envelope. */
struct SEINARTSCOREENTITY_API FSeinSnapshotEnvelope
{
	int64 SnapshotTick = 0;
	FGuid CommandProtocolDigest;
	FGuid CompatibilityDigest;
	TArray<FSeinSnapshotEnvelopeSection> Sections;
};

/** Validated fixed-prefix metadata. Payload bytes are never exposed here. */
struct SEINARTSCOREENTITY_API FSeinSnapshotEnvelopeMetadata
{
	uint32 WireFormatVersion = 0;
	uint32 SnapshotSemanticsVersion = 0;
	uint32 SectionCount = 0;
	uint64 DirectoryBytes = 0;
	uint64 BodyBytes = 0;
	int64 SnapshotTick = 0;
	FGuid CommandProtocolDigest;
	FGuid CompatibilityDigest;
	FGuid AggregateStateRoot;
	FGuid BodyDigest;
};

/**
 * Pure canonical snapshot-v16 envelope codec.
 *
 * The fixed prefix and directory use big-endian integers and raw ASCII IDs.
 * Decode is transactional: caller outputs are unchanged on failure. It verifies
 * the declared file length and exact-body BLAKE3-128 digest before parsing the
 * directory, then verifies every section leaf and the aggregate state root
 * before copying any payload into caller-visible output.
 *
 * Decode proves framing and internal consistency only. Before restoring a
 * world, the integration layer must exact-claim every Authoritative and
 * Continuation section against the world's frozen contributor catalog and
 * reject unknown or mismatched schema/descriptor identities. BLAKE3 detects
 * accidental corruption; source authentication belongs to the transport or
 * trusted file boundary.
 */
class SEINARTSCOREENTITY_API FSeinSnapshotEnvelopeCodec
{
public:
	static constexpr uint32 WireFormatVersion = 1;
	static constexpr uint32 SnapshotSemanticsVersion = 16;
	static constexpr int32 PrefixBytes = 120;
	static constexpr uint32 MaxSections = 8192;
	static constexpr uint32 MaxSectionIdBytes = 128;
	static constexpr uint64 MaxDirectoryBytes = 4ULL * 1024ULL * 1024ULL;
	static constexpr uint64 MaxSectionPayloadBytes =
		64ULL * 1024ULL * 1024ULL;
	static constexpr uint64 MaxBodyBytes = 256ULL * 1024ULL * 1024ULL;

	/**
	 * Validate exactly one 120-byte prefix. This is the prefix-first file/stream
	 * admission seam: it exposes bounded lengths and digests, never payloads.
	 * Callers may impose a smaller session/file limit before reading the body.
	 */
	static bool ParsePrefix(
		TConstArrayView<uint8> Prefix,
		FSeinSnapshotEnvelopeMetadata& OutMetadata,
		FString& OutError);

	/**
	 * Sort sections by raw ASCII ID and encode a canonical envelope.
	 * OutBytes and OutMetadata are unchanged on failure.
	 */
	static bool Encode(
		const FSeinSnapshotEnvelope& Envelope,
		TArray<uint8>& OutBytes,
		FSeinSnapshotEnvelopeMetadata& OutMetadata,
		FString& OutError);

	/**
	 * Validate and decode one complete envelope.
	 * OutEnvelope and OutMetadata are unchanged on failure.
	 */
	static bool Decode(
		TConstArrayView<uint8> Bytes,
		FSeinSnapshotEnvelope& OutEnvelope,
		FSeinSnapshotEnvelopeMetadata& OutMetadata,
		FString& OutError);
};

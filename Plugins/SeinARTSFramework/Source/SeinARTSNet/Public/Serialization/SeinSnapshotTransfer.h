/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSnapshotTransfer.h
 * @brief   Bounded checkpoint transfer framing for resync (FEAT-01).
 *
 *          Maps a captured v15 world snapshot into the canonical snapshot
 *          envelope (one Authoritative section) for coordinator→peer
 *          transfer, and validates + decodes the received bytes back into a
 *          snapshot the trusted restore path can adopt. The envelope proves
 *          bounded framing and BLAKE3 integrity; SOURCE authentication is the
 *          transport boundary's job (in the shipped adapter, relay ownership
 *          and the protocol context), and full semantic validation belongs to
 *          USeinWorldSubsystem::RestoreSnapshot exactly as for a local
 *          checkpoint.
 */

#pragma once

#include "CoreMinimal.h"
#include "Serialization/SeinSnapshotEnvelopeCodec.h"

struct FSeinWorldSnapshot;

namespace SeinSnapshotTransfer
{
	/** Stable section identity for the single checkpoint payload section. */
	SEINARTSNET_API extern const TCHAR* const CheckpointSectionId;

	/** Encode one captured snapshot as a canonical transfer envelope.
	 *  Refuses a snapshot whose version is not current. OutBytes and
	 *  OutMetadata are unchanged on failure. */
	SEINARTSNET_API bool EncodeCheckpointEnvelope(
		const FSeinWorldSnapshot& Snapshot,
		TArray<uint8>& OutBytes,
		FSeinSnapshotEnvelopeMetadata& OutMetadata,
		FString& OutError);

	/** Validate and decode one received transfer envelope back into a
	 *  snapshot. Verifies the envelope frames exactly one checkpoint section
	 *  with the expected identity/role/schema binding, that the payload
	 *  deserializes with full consumption, and that the decoded snapshot's
	 *  tick and digests match the envelope prefix. The decoded snapshot still
	 *  carries ZERO adoption authority — every semantic gate lives in
	 *  RestoreSnapshot. OutSnapshot/OutMetadata are unchanged on failure. */
	SEINARTSNET_API bool DecodeCheckpointEnvelope(
		TConstArrayView<uint8> Bytes,
		FSeinWorldSnapshot& OutSnapshot,
		FSeinSnapshotEnvelopeMetadata& OutMetadata,
		FString& OutError);
}

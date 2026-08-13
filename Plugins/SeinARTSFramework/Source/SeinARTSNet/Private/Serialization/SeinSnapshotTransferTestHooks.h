/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinSnapshotTransferTestHooks.h
 * @author       RJ Macklem
 * @created      12 Aug 2026
 * @latest       12 Aug 2026
 * @brief        Development-only checkpoint encode midpoint hooks.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Serialization/SeinSnapshotEnvelopeCodec.h"

struct FSeinWorldSnapshot;

#if WITH_DEV_AUTOMATION_TESTS
namespace SeinSnapshotTransfer
{
	/** Encode through the production path, pausing after snapshot payload
	 *  serialization and before envelope framing. */
	bool EncodeCheckpointEnvelopeWithMidpointForTests(
		const FSeinWorldSnapshot& Snapshot,
		TArray<uint8>& OutBytes,
		FSeinSnapshotEnvelopeMetadata& OutMetadata,
		FString& OutError,
		TFunctionRef<bool(FString&)> AfterPayloadSerialized);
}
#endif

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSnapshotComponentStorageBlob.h
 * @brief   Per-storage byte payload for `FSeinWorldSnapshot`.
 *
 * Lives in its own header so UHT fully resolves the struct's USTRUCT hash
 * before SeinWorldSnapshot.h processes the TMap that uses it as the value
 * type. Inlining the declaration alongside FSeinWorldSnapshot triggered a
 * "code generation hash is zero" error in UE 5.7's UHT — an order-of-
 * resolution quirk when a USTRUCT is used as a TMap value in another
 * USTRUCT in the same file.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinSnapshotComponentStorageBlob.generated.h"

/** Per-storage byte payload. Wrapper struct so the outer TMap is
 *  USTRUCT-friendly. Captured by `ISeinComponentStorage::SerializeFromArchive`. */
USTRUCT()
struct SEINARTSCOREENTITY_API FSeinSnapshotComponentStorageBlob
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<uint8> Bytes;

	/** Number of (handle, payload) entries inside `Bytes`. Diagnostic + a
	 *  cheap sanity check on restore. */
	UPROPERTY()
	int32 EntryCount = 0;
};

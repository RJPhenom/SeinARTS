/** Bounded, failure-atomic filesystem boundary for executable replay files. */
#pragma once

#include "CoreMinimal.h"

namespace SeinReplayFileIO
{
	/** Query one open handle and admit only a caller-bounded 64-bit file size. */
	bool QueryBoundedSize(
		const FString& FilePath,
		int64 MinBytes,
		int64 MaxBytes,
		int64& OutSize,
		FString& OutError);

	/**
	 * Read one bounded range without materializing the rest of the file. The
	 * open handle's full size must also fit MaxFileBytes. OutBytes is
	 * transactional and NumBytes may not exceed TArray's int32 capacity.
	 */
	bool ReadRange(
		const FString& FilePath,
		int64 Offset,
		int64 NumBytes,
		int64 MaxFileBytes,
		TArray<uint8>& OutBytes,
		FString& OutError);

	/** Create and durably close a new file, refusing any existing destination. */
	bool CreateNew(
		const FString& FilePath,
		TConstArrayView<uint8> Bytes,
		FString& OutError);

	/**
	 * Append only when the same opened file is exactly ExpectedOffset bytes.
	 * A failed write can leave a torn tail; journal recovery deliberately owns
	 * that case and no earlier bytes are rewritten.
	 */
	bool AppendAtExpectedOffset(
		const FString& FilePath,
		int64 ExpectedOffset,
		TConstArrayView<uint8> Bytes,
		FString& OutError);

	/** Atomically expose a closed partial file at an absent sibling final path.
	 *  Appended contents are fully flushed first; platform rename metadata is
	 *  atomic for readers but is not promised power-loss durable. */
	bool PublishExistingAtomically(
		const FString& PartialPath,
		const FString& FinalPath,
		FString& OutError);

	/** Size and contents come from the same open handle; OutBytes changes only on success. */
	bool ReadBounded(
		const FString& FilePath,
		int64 MinBytes,
		int64 MaxBytes,
		TArray<uint8>& OutBytes,
		FString& OutError);
}

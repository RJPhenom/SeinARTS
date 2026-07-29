/** Bounded, failure-atomic filesystem boundary for executable replay files. */
#pragma once

#include "CoreMinimal.h"

namespace SeinReplayFileIO
{
	/** Size and contents come from the same open handle; OutBytes changes only on success. */
	bool ReadBounded(
		const FString& FilePath,
		int64 MinBytes,
		int64 MaxBytes,
		TArray<uint8>& OutBytes,
		FString& OutError);

	/** Publish a new file only after its sibling temporary file closes successfully. */
	bool WriteNewAtomically(
		const FString& FilePath,
		const TArray<uint8>& Bytes,
		FString& OutError);
}

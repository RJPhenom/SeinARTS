/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEntityHandle.h
 * @brief   Generational entity handle for safe entity referencing with pooling.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinEntityHandle.generated.h"

/**
 * Generational entity handle. Combines a slot index with a generation counter
 * to detect stale references after entity recycling.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinEntityHandle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Entity")
	int32 Index;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Entity")
	int32 Generation;

	FSeinEntityHandle()
		: Index(0)
		, Generation(0)
	{}

	FSeinEntityHandle(int32 InIndex, int32 InGeneration)
		: Index(InIndex)
		, Generation(InGeneration)
	{}

	/** Invalid handle constant (index 0, generation 0) */
	static FSeinEntityHandle Invalid()
	{
		return FSeinEntityHandle(0, 0);
	}

	/** Check validity (generation 0 is always invalid) */
	FORCEINLINE bool IsValid() const
	{
		return Generation != 0;
	}

	FORCEINLINE bool operator==(const FSeinEntityHandle& Other) const
	{
		return Index == Other.Index && Generation == Other.Generation;
	}

	FORCEINLINE bool operator!=(const FSeinEntityHandle& Other) const
	{
		return !(*this == Other);
	}

	FORCEINLINE bool operator<(const FSeinEntityHandle& Other) const
	{
		return Index < Other.Index || (Index == Other.Index && Generation < Other.Generation);
	}

	FString ToString() const
	{
		return FString::Printf(TEXT("Handle(%d:%d)"), Index, Generation);
	}
};

FORCEINLINE uint32 GetTypeHash(const FSeinEntityHandle& Handle)
{
	return HashCombine(GetTypeHash(Handle.Index), GetTypeHash(Handle.Generation));
}

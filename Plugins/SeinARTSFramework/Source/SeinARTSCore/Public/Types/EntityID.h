/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		EntityID.h
 * @date:		2/28/2026
 * @author:		RJ Macklem
 * @brief:		Unique identifier for simulation entities.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "EntityID.generated.h"

/**
 * Unique identifier for an entity in the simulation.
 * Type-safe wrapper around uint32 to prevent accidental misuse.
 */
USTRUCT(BlueprintType)
struct SEINARTSCORE_API FSeinID
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Entity")
	int32 Value;

	FSeinID()
		: Value(0)
	{}

	explicit FSeinID(int32 InValue)
		: Value(InValue)
	{}

	/** Check if this ID is valid (non-zero) */
	FORCEINLINE bool IsValid() const
	{
		return Value != 0;
	}

	/** Get invalid ID constant */
	static FORCEINLINE FSeinID Invalid()
	{
		return FSeinID(0);
	}

	// Comparison operators
	FORCEINLINE bool operator==(const FSeinID& Other) const
	{
		return Value == Other.Value;
	}

	FORCEINLINE bool operator!=(const FSeinID& Other) const
	{
		return Value != Other.Value;
	}

	FORCEINLINE bool operator<(const FSeinID& Other) const
	{
		return Value < Other.Value;
	}

	// String conversion for debugging
	FString ToString() const
	{
		return FString::Printf(TEXT("SeinID(%u)"), Value);
	}
};

/** Hash function for FSeinID so it can be used as TMap key */
FORCEINLINE uint32 GetTypeHash(const FSeinID& ID)
{
	return GetTypeHash(ID.Value);
}

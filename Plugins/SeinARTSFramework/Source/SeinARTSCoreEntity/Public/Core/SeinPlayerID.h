/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPlayerID.h
 * @brief   Type-safe player identifier.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinPlayerID.generated.h"

/**
 * Type-safe player identifier. Uses uint8 under the hood for compactness, but provides 
 * a distinct type for clarity and safety. Player ID 0 is reserved for "neutral" or "no player". 
 * 
 * Valid player IDs start from 1.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinPlayerID
{
	GENERATED_BODY()

	/**
	 * Player ID value. 0 is reserved for "neutral" or "no player". 
	 * 
	 * Valid player IDs start from 1.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Player")
	uint8 Value;

	FSeinPlayerID() : Value(0) {}
	explicit FSeinPlayerID(uint8 InValue) : Value(InValue) {}

	static FSeinPlayerID Neutral() { return FSeinPlayerID(0); }
	FORCEINLINE bool IsValid() const { return Value != 0; }
	FORCEINLINE bool IsNeutral() const { return Value == 0; }

	FORCEINLINE bool operator==(const FSeinPlayerID& Other) const { return Value == Other.Value; }
	FORCEINLINE bool operator!=(const FSeinPlayerID& Other) const { return Value != Other.Value; }
	FORCEINLINE bool operator<(const FSeinPlayerID& Other) const { return Value < Other.Value; }

	FString ToString() const { return FString::Printf(TEXT("Player(%u)"), Value); }
};

FORCEINLINE uint32 GetTypeHash(const FSeinPlayerID& ID)
{
	return GetTypeHash(ID.Value);
}

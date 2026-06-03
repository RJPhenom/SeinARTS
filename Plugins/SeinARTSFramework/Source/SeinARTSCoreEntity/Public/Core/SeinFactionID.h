/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFactionID.h
 * @brief   Type-safe faction identifier.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinFactionID.generated.h"

/**
 * Type-safe faction identifier. Uses uint8 under the hood for compactness, 
 * but provides a distinct type for clarity and safety. Faction ID 0 is reserved for "none". 
 * 
 * Valid faction IDs start from 1.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinFactionID
{
	GENERATED_BODY()

	/**
	 * Faction ID value. 0 is reserved for "none". 
	 * 
	 * Valid faction IDs start from 1.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Faction")
	uint8 Value;

	FSeinFactionID() : Value(0) {}
	explicit FSeinFactionID(uint8 InValue) : Value(InValue) {}

	static FSeinFactionID None() { return FSeinFactionID(0); }
	FORCEINLINE bool IsValid() const { return Value != 0; }

	FORCEINLINE bool operator==(const FSeinFactionID& Other) const { return Value == Other.Value; }
	FORCEINLINE bool operator!=(const FSeinFactionID& Other) const { return Value != Other.Value; }
	FORCEINLINE bool operator<(const FSeinFactionID& Other) const { return Value < Other.Value; }

	FString ToString() const { return FString::Printf(TEXT("Faction(%u)"), Value); }
};

FORCEINLINE uint32 GetTypeHash(const FSeinFactionID& ID)
{
	return GetTypeHash(ID.Value);
}

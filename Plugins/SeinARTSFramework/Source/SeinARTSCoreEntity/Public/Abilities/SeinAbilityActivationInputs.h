/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinAbilityActivationInputs.h
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Captured, bounded values supplied to one ability activation.
 *
 * @disclaimer   Generated with assistance from an AI language model.
 */
#pragma once

#include "CoreMinimal.h"
#include "SeinAbilityActivationInputs.generated.h"

class USeinAbility;

/** Values captured when an ability request is built. Empty uses class defaults. */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinAbilityActivationInputs
{
	GENERATED_BODY()

	static constexpr int32 MaxBytes = 4096;
	static constexpr int32 MaxFields = 64;

	/** Exact ability class and exposed-property schema fingerprint. */
	UPROPERTY()
	uint32 SchemaA = 0;
	UPROPERTY()
	uint32 SchemaB = 0;
	UPROPERTY()
	uint32 SchemaC = 0;
	UPROPERTY()
	uint32 SchemaD = 0;
	FGuid GetSchemaDigest() const { return FGuid(SchemaA, SchemaB, SchemaC, SchemaD); }
	void SetSchemaDigest(const FGuid& Value) { SchemaA = Value.A; SchemaB = Value.B; SchemaC = Value.C; SchemaD = Value.D; }

	/** Canonical values in schema order. */
	UPROPERTY()
	TArray<uint8> Data;

	bool IsEmpty() const { return !GetSchemaDigest().IsValid() && Data.IsEmpty(); }
	bool IsBounded() const { return Data.Num() <= MaxBytes; }

	/** Capture only exposed variables from a native or Blueprint ability template. */
	static bool Capture(const USeinAbility& Values, FSeinAbilityActivationInputs& OutInputs, FString& OutError);
	/** Decode against a trusted ability class into a disposable template. */
	bool Decode(USeinAbility& Candidate, FString& OutError) const;
	/** Validate the exposure contract, including unsupported/transient fields. */
	static bool ComputeSchemaDigest(const UClass* Class, FGuid& OutDigest, FString& OutError);
	static bool ValidateClass(const UClass* Class, FString& OutError);
};

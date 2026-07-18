#pragma once

#include "CoreMinimal.h"
#include "SeinConfigFingerprintTestTypes.generated.h"

/** Nested reflected value used to exercise recursive fingerprint canonicalization. */
USTRUCT()
struct FSeinConfigFingerprintNestedTestValue
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Marker = 0;

	UPROPERTY()
	TMap<FString, int32> ValuesByName;

	UPROPERTY()
	TSet<FString> Labels;

	UPROPERTY()
	TArray<int32> OrderedValues;
};

/** Test-only CDO whose fields cover every recursive container path in the registry. */
UCLASS()
class USeinConfigFingerprintTestSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TMap<FString, FSeinConfigFingerprintNestedTestValue> ValuesByGroup;

	UPROPERTY()
	TArray<FSeinConfigFingerprintNestedTestValue> OrderedGroups;

	UPROPERTY()
	TOptional<FSeinConfigFingerprintNestedTestValue> OptionalGroup;

	UPROPERTY()
	int32 ScalarValue = 0;
};

/** Distinct schema used to prove that a frozen contributor ID cannot change owners. */
UCLASS()
class USeinConfigFingerprintAlternateTestSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 ScalarValue = 0;
};

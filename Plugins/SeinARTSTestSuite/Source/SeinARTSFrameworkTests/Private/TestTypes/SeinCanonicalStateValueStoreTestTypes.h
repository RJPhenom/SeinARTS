#pragma once

#include "CoreMinimal.h"
#include "SeinCanonicalStateValueStoreTestTypes.generated.h"

USTRUCT(meta = (SeinDeterministic))
struct FSeinCanonicalStateValueStoreTestPayload
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Marker = 0;

	UPROPERTY()
	TArray<int32> OrderedValues;
};

USTRUCT(meta = (SeinDeterministic))
struct FSeinCanonicalStateValueStoreNameTestPayload
{
	GENERATED_BODY()

	UPROPERTY()
	FName Name;
};

USTRUCT(meta = (SeinDeterministic))
struct FSeinCanonicalStateValueStoreLargeTestPayload
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Marker = 0;

	UPROPERTY()
	TArray<FString> Chunks;
};

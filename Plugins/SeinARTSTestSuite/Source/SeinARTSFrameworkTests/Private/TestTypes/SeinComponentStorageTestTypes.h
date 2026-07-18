#pragma once

#include "CoreMinimal.h"
#include "SeinComponentStorageTestTypes.generated.h"

/** Resource-owning probe used only to verify reflected storage lifecycle balance. */
USTRUCT(meta = (SeinDeterministic))
struct FSeinComponentStorageLifecycleProbe
{
	GENERATED_BODY()

	static int32 ConstructionCount;
	static int32 DestructionCount;

	UPROPERTY()
	TArray<int32> Values;

	FSeinComponentStorageLifecycleProbe()
	{
		++ConstructionCount;
	}

	~FSeinComponentStorageLifecycleProbe()
	{
		++DestructionCount;
	}

	static void ResetCounts()
	{
		ConstructionCount = 0;
		DestructionCount = 0;
	}
};

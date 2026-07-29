#pragma once

#include "SeinSquadDispatchResolver.h"
#include "SeinSquadResolverNestedCallbackSafetyTestTypes.generated.h"

/** Squad resolver double that reallocates broker storage from PostProcess. */
UCLASS()
class USeinSquadResolverNestedCallbackSafetyTestResolver
	: public USeinSquadDispatchResolver
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 CallbackCalls = 0;

	UPROPERTY()
	int32 GrowthCount = 16;

	UPROPERTY()
	FFixedVector CallbackPosition;

	virtual void PostProcessPositions_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		TArray<FFixedVector>& InOutPositions,
		FFixedVector TargetLocation) override;
};

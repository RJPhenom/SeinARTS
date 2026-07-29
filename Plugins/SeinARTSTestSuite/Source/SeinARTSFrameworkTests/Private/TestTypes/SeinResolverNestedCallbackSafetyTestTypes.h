#pragma once

#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "SeinResolverNestedCallbackSafetyTestTypes.generated.h"

/** Grows entity and broker-component storage from the nested formation hook. */
UCLASS()
class USeinResolverNestedCallbackSafetyTestResolver
	: public USeinDefaultCommandBrokerResolver
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

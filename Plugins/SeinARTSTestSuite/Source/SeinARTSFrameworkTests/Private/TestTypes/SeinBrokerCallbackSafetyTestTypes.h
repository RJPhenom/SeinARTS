#pragma once

#include "Abilities/SeinAbility.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "SeinBrokerCallbackSafetyTestTypes.generated.h"

UCLASS()
class USeinBrokerCallbackSafetyAbility : public USeinAbility
{
	GENERATED_BODY()
};

/** Adversarial resolver that grows both entity and broker-component storage
 *  during dispatch, then optionally mutates or destroys the source broker. */
UCLASS()
class USeinBrokerCallbackSafetyResolver : public USeinCommandBrokerResolver
{
	GENERATED_BODY()

public:
	UPROPERTY()
	bool bMutateOrderOnNextResolve = false;

	UPROPERTY()
	bool bDestroyBrokerOnNextResolve = false;

	UPROPERTY()
	bool bReturnNonMemberOnNextResolve = false;

	UPROPERTY()
	bool bReturnDuplicateOnNextResolve = false;

	UPROPERTY()
	bool bReturnOversizedTargeterPointsOnNextResolve = false;

	UPROPERTY()
	bool bReturnBrokerCarrierOnNextResolve = false;

	UPROPERTY()
	bool bCommitNestedOutputAndStaleOuter = false;

	UPROPERTY()
	int32 ResolveCalls = 0;

	UPROPERTY()
	int32 GrowthCount = 16;

	UPROPERTY()
	FFixedVector ReplacementTarget;

	UPROPERTY()
	FSeinEntityHandle InjectedMember;

	UPROPERTY()
	FFixedVector NestedSettledPosition;

	UPROPERTY()
	FFixedQuaternion NestedFacing = FFixedQuaternion::Identity;

	UPROPERTY()
	TArray<FFixedVector> ObservedTargets;

	virtual FSeinBrokerDispatchPlan ResolveDispatch_Implementation(
		USeinWorldSubsystem* World,
		FSeinEntityHandle BrokerHandle,
		const FSeinBrokerOrderInput& Order) override;
};

#pragma once

#include "Abilities/SeinAbility.h"
#include "Abilities/SeinMoveToProxy.h"
#include "Engine/LatentActionManager.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "SeinAbilityContinuationValidationTestTypes.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(
	FSeinAbilityContinuationValidationNoResultDelegate);

/**
 * Minimal non-Sein async boundary used to prove that continuation analysis
 * follows arbitrary Blueprint async callbacks, not only nested Move To nodes.
 * Reusing the public result delegate is deliberate: its compiler-frame temp
 * is indistinguishable from Move To residue once node provenance is erased.
 */
UCLASS()
class USeinAbilityContinuationValidationAsyncProxy
	: public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FSeinMoveToDelegate OnFinished;

	UFUNCTION(
		BlueprintCallable,
		meta = (BlueprintInternalUseOnly = "true"))
	static USeinAbilityContinuationValidationAsyncProxy*
		StartValidationAsync();

	virtual void Activate() override {}
};

/**
 * Deliberately violates BaseAsyncTask's same-signature delegate assumption:
 * OnWithoutResult leaves the Result temporary created for OnFinished stale.
 */
UCLASS()
class USeinAbilityContinuationValidationHeterogeneousAsyncProxy
	: public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FSeinMoveToDelegate OnFinished;

	UPROPERTY(BlueprintAssignable)
	FSeinAbilityContinuationValidationNoResultDelegate OnWithoutResult;

	UFUNCTION(
		BlueprintCallable,
		meta = (BlueprintInternalUseOnly = "true"))
	static USeinAbilityContinuationValidationHeterogeneousAsyncProxy*
		StartHeterogeneousValidationAsync();

	virtual void Activate() override {}
};

/**
 * Proves that a pure implicit-Self getter is not automatically checkpoint
 * safe merely because it returns a deterministic-looking type.
 */
UCLASS()
class USeinAbilityContinuationValidationTestAbility : public USeinAbility
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	int32 UnsafeTransientValue = 7;

	UPROPERTY(Transient)
	FSeinMoveToResult UnsafeTransientResult;

	UPROPERTY(meta = (SeinStateIgnore))
	int32 MetadataOnlyIgnoreAttempt = 11;

	UPROPERTY()
	int32 NativeObservedValue = 0;

	UFUNCTION(BlueprintPure)
	int32 ReadUnsafeTransientValue() const;

	UFUNCTION(BlueprintPure)
	int32 ExtractWaypointIndex(FSeinMoveToResult Result) const;

	/** Deliberately lacks SeinContinuationSafe: opaque native mutation. */
	UFUNCTION(BlueprintCallable)
	void RecordUnsafeTransientValue();

	/** Metadata cannot make a latent resume point checkpoint-transparent. */
	UFUNCTION(
		BlueprintCallable,
		meta = (
			Latent,
			LatentInfo = "LatentInfo",
			SeinContinuationSafe))
	void ConsumeResultInSafeMarkedLatent(
		FSeinMoveToResult Result,
		FLatentActionInfo LatentInfo);
};

#pragma once

#include "Abilities/SeinAbility.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Abilities/SeinMoveToProxy.h"
#include "Movement/SeinMovement.h"
#include "SeinMoveToLifecycleTestTypes.generated.h"

UCLASS()
class USeinMoveToLifecycleTestAbility : public USeinAbility
{
	GENERATED_BODY()
};

/** Immediate, navigation-independent movement double for action lifecycle tests. */
UCLASS()
class USeinMoveToLifecycleTestMovement : public USeinMovement
{
	GENERATED_BODY()

public:
	/** Reflected continuation-state surrogate for movement snapshot tests. */
	UPROPERTY()
	FFixedPoint PersistentTestValue = FFixedPoint::Zero;

	static bool bFinishOnTick;
	static int32 BeginCount;
	static int32 TickCount;
	static int32 EndCount;
	static int32 PlanPathCallCount;
	static int32 LastTickPathWaypointCount;
	static FFixedVector RepathWaypointMarker;
	static FFixedVector LastTickMiddleWaypoint;
	static TArray<ESeinPathResult> ScriptedPathResults;
	static TArray<int32> EmptyFoundCallIndices;
	static bool bRepathPathsPartial;
	static bool bInitialPathSkipsStart;
	static TFunction<void()> MoveEndCallback;

	static void Reset();

	void SetCachedNavigationPolicyForTest(
		FGameplayTag BlockedTerrainTag,
		FSeinEntityHandle Requester)
	{
		CachedCollisionRadius = FFixedPoint::FromInt(81);
		CachedNumFootprintSamples = 8;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			CachedFootprintSamples[Index] = FFixedVector(
				FFixedPoint::FromInt(Index + 1),
				FFixedPoint::FromInt(-(Index + 1)),
				FFixedPoint::Zero);
		}
		CachedMaxStepHeight = FFixedPoint::FromInt(123);
		CachedNavLayerMask = 0x04;
		CachedNavWallPaddingCells = 7;
		CachedBlockedTerrainTags.Reset();
		CachedBlockedTerrainTags.AddTag(BlockedTerrainTag);
		CachedNavRequester = Requester;
	}

	bool HasCachedNavigationPolicyForTest(
		FGameplayTag BlockedTerrainTag,
		FSeinEntityHandle Requester) const
	{
		if (CachedCollisionRadius != FFixedPoint::FromInt(81)
			|| CachedNumFootprintSamples != 8
			|| CachedMaxStepHeight != FFixedPoint::FromInt(123)
			|| CachedNavLayerMask != 0x04
			|| CachedNavWallPaddingCells != 7
			|| CachedNavRequester != Requester
			|| !CachedBlockedTerrainTags.HasTagExact(
				BlockedTerrainTag))
		{
			return false;
		}
		for (int32 Index = 0; Index < 8; ++Index)
		{
			const FFixedVector Expected(
				FFixedPoint::FromInt(Index + 1),
				FFixedPoint::FromInt(-(Index + 1)),
				FFixedPoint::Zero);
			if (CachedFootprintSamples[Index] != Expected)
			{
				return false;
			}
		}
		return true;
	}

	virtual ESeinPathResult PlanPath(
		const FSeinPlanPathContext& Ctx, FSeinPath& OutPath) const override;
	virtual void OnMoveBegin(const FSeinMovementContext& Ctx) override;
	virtual bool Tick(const FSeinMovementContext& Ctx) override;
	virtual void OnMoveEnd(FSeinEntity& Entity) override;
};

/** Dynamic-delegate receiver that reproduces common Blueprint terminal graphs. */
UCLASS()
class USeinMoveToLifecycleTestObserver : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<USeinAbility> Ability;

	UPROPERTY()
	TObjectPtr<USeinMoveToAction> Action;

	UPROPERTY()
	TObjectPtr<USeinLatentActionManager> Manager;

	int32 CompletedCount = 0;
	int32 FailedCount = 0;
	int32 CancelledCount = 0;
	int32 PathRecomputedCount = 0;
	int32 PartialPathCount = 0;
	TArray<int32> RepathEventOrder;
	FFixedPoint RecomputedObservedRepathElapsed = FFixedPoint::MinValue;
	bool bCompletedSawTerminalAction = false;
	bool bFailedSawTerminalAction = false;
	bool bReenterCancellationOnCancelled = false;
	ESeinMoveFailureReason LastFailure = ESeinMoveFailureReason::None;

	UFUNCTION()
	void HandleCompleted(FSeinMoveToResult Result);

	UFUNCTION()
	void HandleFailed(FSeinMoveToResult Result);

	UFUNCTION()
	void HandleCancelled(FSeinMoveToResult Result);

	UFUNCTION()
	void HandlePathRecomputed(FSeinMoveToResult Result);

	UFUNCTION()
	void HandlePartialPath(FSeinMoveToResult Result);
};

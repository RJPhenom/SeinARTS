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
	static TFunction<void()> MoveEndCallback;

	static void Reset();

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
};

#pragma once

#include "Abilities/SeinAbility.h"
#include "Abilities/SeinMoveToProxy.h"
#include "Movement/SeinMovement.h"
#include "SeinNavigation.h"
#include "SeinMoveToContinuationEditorTestTypes.generated.h"

/** Deterministic blocked navigation double for escape-continuation tests. */
UCLASS()
class USeinMoveToContinuationEditorTestNavigation
	: public USeinNavigation
{
	GENERATED_BODY()

public:
	static FFixedVector EscapeOffset;

	static void Reset();

	virtual bool HasRuntimeData() const override { return true; }
	virtual bool ComputeStaticEnvironmentDigest(
		FGuid& OutDigest,
		FString& OutError) const override;
	virtual bool ComputeStateCoverageClaim(
		FSeinNavigationStateCoverageClaim& OutClaim,
		FString& OutError) const override;
	virtual bool IsPassable(const FFixedVector&) const override
	{
		return false;
	}
	virtual bool QueryEscapeTarget(
		const FSeinEscapeQuery& Query,
		FFixedVector& OutTarget) const override;
};

/** BP-parent probe whose reflected counters survive snapshot pool restore. */
UCLASS()
class USeinMoveToContinuationEditorTestAbility
	: public USeinAbility
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 CompletedCount = 0;

	UPROPERTY()
	int32 FailedCount = 0;

	UPROPERTY()
	int32 WaypointCount = 0;

	UPROPERTY()
	int32 CancelledCount = 0;

	UPROPERTY()
	int32 PartialPathCount = 0;

	UPROPERTY()
	int32 PathRecomputedCount = 0;

	/** Assertion-only observation; callbacks overwrite it before every read. */
	UPROPERTY(Transient)
	FSeinMoveToResult LastResult;

	UFUNCTION(BlueprintCallable, meta = (SeinContinuationSafe))
	void RecordCompleted(FSeinMoveToResult Result);

	UFUNCTION(BlueprintCallable, meta = (SeinContinuationSafe))
	void RecordFailed(FSeinMoveToResult Result);

	UFUNCTION(BlueprintCallable, meta = (SeinContinuationSafe))
	void RecordWaypoint(FSeinMoveToResult Result);

	UFUNCTION(BlueprintCallable, meta = (SeinContinuationSafe))
	void RecordCancelled(FSeinMoveToResult Result);

	UFUNCTION(BlueprintCallable, meta = (SeinContinuationSafe))
	void RecordPartialPath(FSeinMoveToResult Result);

	UFUNCTION(BlueprintCallable, meta = (SeinContinuationSafe))
	void RecordPathRecomputed(FSeinMoveToResult Result);
};

/** Navigation-independent persistent movement used by restore lifecycle tests. */
UCLASS()
class USeinMoveToContinuationEditorTestMovement
	: public USeinMovement
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FFixedPoint PersistentValue = FFixedPoint::Zero;

	static int32 PlanCount;
	static int32 BeginCount;
	static int32 TickCount;
	static int32 EndCount;
	static bool bAdvanceWaypoint;
	static bool bAdvanceInitialWaypointOnTick;
	static bool bFinishMove;

	static void Reset();

	virtual ESeinPathResult PlanPath(
		const FSeinPlanPathContext& Context,
		FSeinPath& OutPath) const override;
	virtual void OnMoveBegin(
		const FSeinMovementContext& Context) override;
	virtual bool Tick(
		const FSeinMovementContext& Context) override;
	virtual void OnMoveEnd(FSeinEntity& Entity) override;
};

/** Foreign listener used to prove continuation capture rejects custom routes. */
UCLASS()
class USeinMoveToContinuationEditorTestObserver
	: public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable)
	void RecordForeign(FSeinMoveToResult Result);
};

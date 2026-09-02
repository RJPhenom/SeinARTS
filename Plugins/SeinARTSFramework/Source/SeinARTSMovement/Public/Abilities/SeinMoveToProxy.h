/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoveToProxy.h
 * @brief   Blueprint async action node — the framework's AIMoveTo equivalent.
 *
 * Usage in an ability BP graph:
 *   [OnActivate] -> [Sein Move To (Dest)]
 *                       |- Completed       -> EndAbility
 *                       |- Failed (Result) -> handle failure
 *                       |- WaypointReached -> play step SFX, etc.
 *                       |- Cancelled       -> cleanup
 *
 * Acceptance radius is sourced from the unit's
 * `FSeinNavigationPayload::AcceptanceRadius` — a footprint/turn-radius
 * property of the unit, not the call site. Tune it on the nav component,
 * not here.
 *
 * Snapshot capture also checks the owning Blueprint's persistent event frame.
 * Complete standard async-node residue groups are authenticated and omitted
 * because the compiler never reads them again; restored frames therefore
 * start clean. Any other non-default frame value fails closed. Promote values
 * needed after this node to deterministic ability state and reread or
 * recompute them after the output. This runtime backstop is deliberately
 * conservative and may reject irrelevant stale data rather than risk a
 * silent replay divergence.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Actions/SeinMoveToAction.h"
#include "SeinMoveToProxy.generated.h"

class USeinAbility;
class USeinMoveToAction;
struct FSeinMoveToActionCodec;

#if WITH_DEV_AUTOMATION_TESTS
namespace UE::SeinARTSTests
{
	struct FMoveToActionContinuationTestAccess;
}
#endif

/**
 * Data shared by every Move To output. The delegate property identifies the
 * event channel; fields that do not apply to that channel retain their
 * defaults.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSMOVEMENT_API FSeinMoveToResult
{
	GENERATED_BODY()

	/** Failure reason for OnFailed, or Cancelled for OnCancelled. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Movement")
	ESeinMoveFailureReason FailureReason = ESeinMoveFailureReason::None;

	/** Zero-based waypoint reached by OnWaypointReached; otherwise INDEX_NONE. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Movement")
	int32 WaypointIndex = INDEX_NONE;

	/** Number of waypoints in the current route for OnWaypointReached. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Movement")
	int32 TotalWaypoints = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FSeinMoveToDelegate, FSeinMoveToResult, Result);

UCLASS()
class SEINARTSMOVEMENT_API USeinMoveToProxy : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

	friend struct FSeinMoveToActionCodec;
#if WITH_DEV_AUTOMATION_TESTS
	friend struct UE::SeinARTSTests::FMoveToActionContinuationTestAccess;
#endif

public:
	UPROPERTY(BlueprintAssignable) FSeinMoveToDelegate OnCompleted;
	UPROPERTY(BlueprintAssignable) FSeinMoveToDelegate OnFailed;
	UPROPERTY(BlueprintAssignable) FSeinMoveToDelegate OnWaypointReached;
	UPROPERTY(BlueprintAssignable) FSeinMoveToDelegate OnCancelled;

	/** Fired whenever the action commits a PARTIAL path — initial FindPath at
	 *  move-start, or a repath swap (Interval / OffPathOnly mode). The unit
	 *  continues to the partial endpoint and OnCompleted will still fire on
	 *  arrival; OnPartialPath is the BP's hook to surface "destination not
	 *  fully reachable" UI / SFX (cursor change, toast, alternate animation).
	 *  Bind only if the BP cares about distinguishing partial from full
	 *  arrivals — most simple movement bindings can ignore it. */
	UPROPERTY(BlueprintAssignable) FSeinMoveToDelegate OnPartialPath;

	/** Fired whenever the action RECOMPUTES its path mid-move — an interval repath or an
	 *  off-path-drift repath committing a fresh route to the same destination. Non-terminal;
	 *  the move continues and OnCompleted/OnFailed still fire normally. Bind to react to a
	 *  changed route (re-evaluate, refresh path UI/SFX). Fires on repaths only, not the
	 *  initial path. */
	UPROPERTY(BlueprintAssignable) FSeinMoveToDelegate OnPathRecomputed;

	/** Move the ability's owning entity to Destination using its movement
	 *  profile. Acceptance radius is read from the unit's
	 *  `FSeinNavigationPayload::AcceptanceRadius`. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability|Movement",
	          meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Ability",
	                  DisplayName = "Move To",
	                  SeinCheckpointActionClass = "/Script/SeinARTSMovement.SeinMoveToAction"))
	static USeinMoveToProxy* SeinMoveTo(
		USeinAbility* Ability,
		FFixedVector Destination);

	virtual void Activate() override;

	// Observer callbacks invoked by USeinMoveToAction
	void NotifyCompleted();
	void NotifyFailed(ESeinMoveFailureReason Reason);
	void NotifyWaypointReached(int32 Index, int32 Total);
	void NotifyCancelled();
	void NotifyPartialPath();
	void NotifyPathRecomputed();

	/**
	 * Release this proxy while abandoning an old deterministic timeline
	 * (snapshot restore or module unload). Deliberately silent: no gameplay
	 * delegate belongs to a timeline that will never advance again. Safe to
	 * call more than once.
	 */
	void AbandonForSnapshotRestore();

private:
	UPROPERTY() TObjectPtr<USeinAbility> CachedAbility;
	FFixedVector CachedDestination;

	UPROPERTY() TObjectPtr<USeinMoveToAction> RunningAction;

	void BroadcastFailure(ESeinMoveFailureReason Reason);
	void ReleaseAfterTerminal();
};

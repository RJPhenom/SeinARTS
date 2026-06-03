/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoveToAction.h
 * @brief   Latent action that moves a sim entity along a USeinNavigation-
 *          produced path. Implementation-agnostic: the action never touches
 *          grids, pathfinders, or A* internals — it only consumes FSeinPath.
 *
 *          Kinematics are read from FSeinMovementComponent (TopSpeed /
 *          Acceleration / TurnRate); pathfinding + acceptance + repath knobs
 *          are read from FSeinNavigationComponent. Steering is minimal:
 *          seek toward next waypoint with an arrive radius at the final
 *          waypoint.
 */

#pragma once

#include "CoreMinimal.h"
#include "Abilities/SeinLatentAction.h"
#include "SeinPathTypes.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinMoveToAction.generated.h"

class USeinMoveToProxy;
class USeinMovement;

/** Reasons a move can fail. Passed via USeinLatentAction::Fail() reason code. */
UENUM(BlueprintType)
enum class ESeinMoveFailureReason : uint8
{
	None                UMETA(DisplayName = "None"),
	PathNotFound        UMETA(DisplayName = "Path Not Found"),
	EntityDestroyed     UMETA(DisplayName = "Entity Destroyed"),
	NoMovementComponent UMETA(DisplayName = "No Movement Component"),
	NoNavigation        UMETA(DisplayName = "No Navigation"),
	Cancelled           UMETA(DisplayName = "Cancelled"),
	/** Chassis was stranded — entered the escape-nudge fallback (driving up
	 *  the WallDistance gradient toward open space) but couldn't make
	 *  meaningful progress before the escape timer expired, or no passable
	 *  neighbor existed to nudge toward. Distinct from `PathNotFound` so
	 *  AI scripts can react differently (e.g., abandon order vs retry
	 *  destination): Stranded means "we tried the escape route and it
	 *  didn't help," PathNotFound means "we never found a plannable path." */
	Stranded            UMETA(DisplayName = "Stranded")
};

UCLASS()
class SEINARTSMOVEMENT_API USeinMoveToAction : public USeinLatentAction
{
	GENERATED_BODY()

public:

	/** Set up a move toward `InDestination`. Acceptance radius is read from
	 *  `FSeinNavigationComponent::AcceptanceRadius` on first TickAction. */
	void Initialize(const FFixedVector& InDestination);

	virtual bool TickAction(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override;
	virtual void OnCancel() override;
	virtual void OnFail(uint8 ReasonCode) override;

	/** Optional observer — receives Completed/Failed/Waypoint/Cancelled events. */
	TWeakObjectPtr<USeinMoveToProxy> Observer;

	UPROPERTY()
	FSeinPath Path;

	bool IsPathValid() const { return Path.bIsValid; }

	/** Index of the waypoint the entity is currently heading toward. Public so
	 *  debug rendering can draw "entity → current waypoint → remaining path". */
	int32 GetCurrentWaypointIndex() const { return CurrentWaypointIndex; }

private:

	FFixedVector Destination;

	/** Resolved at first TickAction from FSeinNavigationComponent::AcceptanceRadius. */
	FFixedPoint AcceptanceRadiusSq = FFixedPoint::Zero;

	int32 CurrentWaypointIndex = 0;
	bool bPathResolved = false;

	/** Position-keeping: set once this action has claimed its Destination as the
	 *  owner's DesiredPosition ("home"). See the claim/supersede block in
	 *  TickAction — the newest move for an entity always wins. */
	bool bHomeClaimed = false;

	/** Agent's position at the moment the current `Path` was committed
	 *  (initial FindPath or a successful repath). Used by OffPathOnly
	 *  drift detection as the implicit start of the polyline.
	 *
	 *  Why this exists: `USeinNavigationAStar::BuildSmoothedPath`
	 *  deliberately skips `CellPath[0]` to avoid a visible "hook" at move
	 *  start. So `Path.Waypoints[0]` is NOT the agent's starting position
	 *  — it's the first LoS-collapsed cell DOWN the path, potentially
	 *  many meters ahead. An agent walking from its start position toward
	 *  `Waypoints[0]` is on-path, but a naive perpendicular-to-polyline
	 *  measurement reads it as "off-path" by the full agent→Waypoints[0]
	 *  distance (T<0 in `OffPathSegDistSqXY` clamps to the segment-start
	 *  endpoint). Storing the origin lets the drift calc include an
	 *  implicit `[PathOriginAgentPos → Waypoints[0]]` segment, capturing
	 *  the "approach the first waypoint along its line" semantic. */
	FFixedVector PathOriginAgentPos = FFixedVector::ZeroVector;

	/** Time since the last repath fired (Interval mode). Reset to zero
	 *  whenever a fresh path is committed. Compared against
	 *  `FSeinNavigationComponent::RepathInterval`. */
	FFixedPoint TimeSinceLastRepath = FFixedPoint::Zero;

	/** Consecutive interval-repath failures since the last successful repath
	 *  (or move-start). When this hits
	 *  `FSeinNavigationComponent::RepathFailureLimit` the action fails with
	 *  `PathNotFound` instead of marching toward an increasingly stale
	 *  path. Reset on every successful repath. */
	int32 ConsecutiveRepathFailures = 0;

	/** Escape-nudge fallback state. When the chassis ends up in a position
	 *  A* can't expand from (`Path.bIsPartial && Waypoints.Num() == 1`, or
	 *  repeated repath failures), we override `Path` with a single waypoint
	 *  pointing at the highest-WD passable neighbor cell and let the normal
	 *  carrot/steering pipeline drive the chassis toward it. Once the
	 *  chassis reaches a cell with WD ≥ Required (back in C-space), or the
	 *  escape timer expires without meaningful progress, we exit. Exit
	 *  modes: success → force immediate repath; failure → `Stranded`.
	 *
	 *  `bInEscapeMode` true while the override is active. `EscapeTimer`
	 *  counts seconds since escape entry. `EscapeStartPos` is the chassis
	 *  position when we entered escape — compared against current pos to
	 *  detect "isn't moving even with the nudge target set." */
	bool bInEscapeMode = false;
	FFixedPoint EscapeTimer = FFixedPoint::Zero;
	FFixedVector EscapeStartPos = FFixedVector::ZeroVector;

	/** Instantiated on first tick from FSeinMovementComponent::MovementClass
	 *  (or USeinBasicMovement if the soft class is null/unresolved). Owns the
	 *  actual advance-along-path logic. */
	UPROPERTY()
	TObjectPtr<USeinMovement> Movement;

	void NotifyCompleted();
	void NotifyWaypointReached(int32 Index, int32 Total);
	void NotifyPartialPath();

	/** Reset transient sim state on the owner's FSeinMovementComponent when
	 *  the action terminates abnormally (cancel / fail). Currently clears
	 *  `bArrivalImminent` so AnimBPs don't show stale "approaching" state
	 *  after a mid-arrival cancellation. */
	void ResetTransientMoveState();
};

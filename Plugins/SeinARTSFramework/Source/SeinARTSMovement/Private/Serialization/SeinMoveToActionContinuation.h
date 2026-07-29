/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoveToActionContinuation.h
 * @brief   Exact future-affecting state for one active Move To continuation.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinPathTypes.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinMoveToActionContinuation.generated.h"

/**
 * Module-private wire payload for USeinMoveToAction.
 *
 * Keep the manual action-field mapping centralized in FSeinMoveToActionCodec.
 * InitialThrottleStreak and FSeinPath::DebugCellPath are deliberately absent:
 * both are diagnostic-only and are never read by simulation behavior.
 */
USTRUCT(meta = (SeinDeterministic))
struct FSeinMoveToActionContinuation
{
	GENERATED_BODY()

	UPROPERTY()
	FFixedVector Destination = FFixedVector::ZeroVector;

	UPROPERTY()
	FFixedPoint AcceptanceRadiusSq = FFixedPoint::Zero;

	UPROPERTY()
	int32 CurrentWaypointIndex = 0;

	UPROPERTY()
	bool bPathResolved = false;

	UPROPERTY()
	bool bAuthoritativeDestination = false;

	UPROPERTY()
	FFixedVector PathOriginAgentPos = FFixedVector::ZeroVector;

	UPROPERTY()
	FFixedPoint TimeSinceLastRepath = FFixedPoint::Zero;

	UPROPERTY()
	int32 ConsecutiveRepathFailures = 0;

	UPROPERTY()
	FFixedPoint BestDistToFinalSq = FFixedPoint::Zero;

	UPROPERTY()
	FFixedPoint TimeStalledNearGoal = FFixedPoint::Zero;

	UPROPERTY()
	FFixedPoint HoldTime = FFixedPoint::Zero;

	UPROPERTY()
	FFixedPoint NextEscalationAt = FFixedPoint::Zero;

	UPROPERTY()
	bool bStage1Fired = false;

	UPROPERTY()
	bool bForceRepathNow = false;

	UPROPERTY()
	bool bEscapeMode = false;

	UPROPERTY()
	FFixedVector EscapeTarget = FFixedVector::ZeroVector;

	UPROPERTY()
	FFixedPoint EscapeAcceptSq = FFixedPoint::Zero;

	UPROPERTY()
	FFixedPoint EscapeHoldTime = FFixedPoint::Zero;

	UPROPERTY()
	int32 EscapeAttempts = 0;

	UPROPERTY()
	int32 TotalEscapeEntries = 0;

	UPROPERTY()
	FFixedPoint FootprintRadius = FFixedPoint::Zero;

	UPROPERTY()
	FFixedPoint StallBandSq = FFixedPoint::Zero;

	UPROPERTY()
	FSeinPath Path;

	UPROPERTY()
	bool bHasMovementBinding = false;

	UPROPERTY()
	bool bMovementFinalized = false;

	UPROPERTY()
	FString OnCompletedFunction;

	UPROPERTY()
	FString OnFailedFunction;

	UPROPERTY()
	FString OnWaypointReachedFunction;

	UPROPERTY()
	FString OnCancelledFunction;

	UPROPERTY()
	FString OnPartialPathFunction;

	UPROPERTY()
	FString OnPathRecomputedFunction;
};

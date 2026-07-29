/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationCanonicalState.h
 * @brief   Canonical continuation payload for deferred navigation work.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinPathTypes.h"
#include "SeinNavigationCanonicalState.generated.h"

/** One ready async result, paired with the request identity that produced it. */
USTRUCT(meta = (SeinDeterministic))
struct FSeinNavigationAsyncResultState
{
	GENERATED_BODY()

	UPROPERTY()
	FSeinPathRequest Request;

	UPROPERTY()
	FSeinPath Path;
};

/**
 * Exact scheduler state owned by USeinNavigationSubsystem.
 *
 * Map entries are flattened into strict ascending requester order so the
 * canonical wire and restore validation never depend on TMap iteration.
 */
USTRUCT(meta = (SeinDeterministic))
struct FSeinNavigationContinuationState
{
	GENERATED_BODY()

	UPROPERTY()
	int32 PathRequestsThisTick = 0;

	UPROPERTY()
	int32 LastResetTick = -1;

	UPROPERTY()
	int32 LastDrainTick = -1;

	UPROPERTY()
	TArray<FSeinPathRequest> QueuedRequests;

	UPROPERTY()
	TArray<FSeinNavigationAsyncResultState> ReadyResults;
};

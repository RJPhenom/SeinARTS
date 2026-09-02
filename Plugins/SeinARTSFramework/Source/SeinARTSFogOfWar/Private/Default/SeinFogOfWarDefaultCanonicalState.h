/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarDefaultCanonicalState.h
 * @brief   Minimal authoritative samples for the shipped fog implementation.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SeinVisionPayload.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "SeinFogOfWarDefaultCanonicalState.generated.h"

/** Sticky observer state. ExploredCells is a packed row-major bit set. */
USTRUCT(meta = (SeinDeterministic))
struct FSeinFogDefaultObserverState
{
	GENERATED_BODY()

	UPROPERTY()
	FSeinPlayerID Observer;

	UPROPERTY()
	TArray<uint8> ExploredCells;

	UPROPERTY()
	TArray<FSeinEntityHandle> SeenEntities;
};

/** Last effective source inputs sampled on the configured fog cadence. */
USTRUCT(meta = (SeinDeterministic))
struct FSeinFogDefaultSourceInput
{
	GENERATED_BODY()

	UPROPERTY()
	FSeinEntityHandle Source;

	UPROPERTY()
	FSeinPlayerID Owner;

	UPROPERTY()
	FFixedVector WorldPos;

	UPROPERTY()
	FFixedQuaternion Rotation;

	UPROPERTY()
	FFixedPoint EyeHeight;

	UPROPERTY()
	TArray<FSeinVisionStamp> Stamps;
};

/** One ordered dynamic-blocker rasterization sample from the last fog tick. */
USTRUCT(meta = (SeinDeterministic))
struct FSeinFogDefaultDynamicBlockerInput
{
	GENERATED_BODY()

	UPROPERTY()
	FFixedVector WorldPos;

	UPROPERTY()
	FFixedQuaternion Rotation;

	UPROPERTY()
	FSeinStampShape Shape;

	UPROPERTY()
	FFixedPoint Height;

	UPROPERTY()
	uint8 LayerMask = 0;
};

/**
 * Minimal persistent model. Live bits/refcounts/footprints/overlay are derived
 * transactionally from these samples and the locally verified static grid.
 */
USTRUCT(meta = (SeinDeterministic))
struct FSeinFogOfWarDefaultCanonicalState
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid StaticEnvironmentDigest;

	UPROPERTY()
	TArray<FSeinFogDefaultObserverState> Observers;

	UPROPERTY()
	TArray<FSeinFogDefaultSourceInput> Sources;

	UPROPERTY()
	TArray<FSeinFogDefaultDynamicBlockerInput> DynamicBlockers;
};


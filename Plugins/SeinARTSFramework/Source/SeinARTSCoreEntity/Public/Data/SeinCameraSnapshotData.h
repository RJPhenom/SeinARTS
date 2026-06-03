/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCameraSnapshotData.h
 * @brief   Camera snapshot wire format — designer-facing struct passed
 *          through the ISeinSnapshotCameraProvider interface.
 *
 * Lives in its own header so it can be `BlueprintType` (BP-exposable) without
 * dragging the rest of the FSeinWorldSnapshot's transitive types (which
 * include things like FInstancedStruct nested in player state) through the
 * BP visibility check.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinCameraSnapshotData.generated.h"

/**
 * Camera state subset of FSeinWorldSnapshot. Fields are designer-friendly
 * generic camera concepts (pivot location, yaw, pitch, zoom). The
 * `CustomBytes` blob is the escape hatch for camera systems whose state
 * doesn't fit those (FOV-zoom, orbital, cinematic rigs) — implement
 * ISeinSnapshotCameraProvider, serialize whatever you need into the bytes.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinCameraSnapshotData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Snapshot|Camera")
	bool bHasState = false;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Snapshot|Camera")
	FVector PivotLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Snapshot|Camera")
	float Yaw = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Snapshot|Camera")
	float Pitch = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Snapshot|Camera")
	float ZoomDistance = 0.0f;

	/** Free-form blob for project-specific camera state (FOV, orbital params,
	 *  cinematic rig pose, etc.) that doesn't map onto the generic fields
	 *  above. Custom providers serialize via FMemoryWriter / FMemoryReader. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Snapshot|Camera")
	TArray<uint8> CustomBytes;
};

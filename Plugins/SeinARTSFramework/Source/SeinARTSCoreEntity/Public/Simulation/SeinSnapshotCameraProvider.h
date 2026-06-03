/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSnapshotCameraProvider.h
 * @brief   Interface any pawn (or any UObject reachable from the local PC)
 *          can implement to round-trip camera / view state through
 *          USeinWorldSubsystem snapshots.
 *
 * Why an interface (not hard-coupled to ASeinCameraPawn): the framework
 * ships its own camera pawn as a designer-friendly default, but projects
 * routinely swap in third-party / Fab-marketplace camera systems, write
 * their own custom camera, or use a Blueprint-only pawn. Hard-binding the
 * snapshot system to ASeinCameraPawn would lock those projects out of the
 * save-game UX. This interface lets ANY pawn opt in.
 *
 * Usage:
 *   - Custom C++ pawn: inherit from ISeinSnapshotCameraProvider, override
 *     CaptureCameraState_Implementation and RestoreCameraState_Implementation.
 *   - Custom BP pawn: add the interface in Class Settings → Interfaces, then
 *     implement the two events in the event graph. Snapshot fields are
 *     accessible as struct members on the FSeinWorldSnapshot wildcard pin.
 *   - Don't want camera in saves: don't implement the interface. The
 *     framework no-ops gracefully.
 *
 * Field strategy: the snapshot struct has a small set of GENERIC fields
 * (pivot location, yaw, pitch, zoom distance) plus a free-form
 * `CustomLocalUIBytes` blob for project-specific state. Implementations
 * fill whichever subset matches their camera model — FOV-based zoom?
 * orbital cameras? cinematic rigs? — using the bytes blob as an escape
 * hatch when the generic fields don't fit.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Data/SeinCameraSnapshotData.h"
#include "SeinSnapshotCameraProvider.generated.h"

UINTERFACE(MinimalAPI, BlueprintType, meta = (DisplayName = "Sein Snapshot Camera Provider"))
class USeinSnapshotCameraProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implement on any pawn (or any UObject reachable via PC->GetPawn() —
 * typically the camera pawn itself) to participate in snapshot capture +
 * restore for camera / view state.
 */
class SEINARTSCOREENTITY_API ISeinSnapshotCameraProvider
{
	GENERATED_BODY()

public:
	/**
	 * Stamp the local camera state into OutData. Called by the Framework's
	 * MatchBootstrap subsystem during snapshot capture.
	 *
	 * Set OutData.bHasState = true so the matching restore knows there's
	 * data to apply. Fill the generic fields (PivotLocation / Yaw / Pitch /
	 * ZoomDistance) directly when they match your camera model; otherwise
	 * serialize to OutData.CustomBytes via FMemoryWriter.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Snapshot")
	void CaptureCameraState(UPARAM(ref) FSeinCameraSnapshotData& OutData);

	/**
	 * Apply Data to the local camera. Called after the sim is fully restored
	 * + actor bridge reconciled, so the camera snaps to a coherent world.
	 *
	 * Skip silently if Data.bHasState is false (snapshot was captured by a
	 * pawn that didn't implement this interface, or the snapshot is from
	 * an older save / cross-project file).
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Snapshot")
	void RestoreCameraState(const FSeinCameraSnapshotData& Data);
};

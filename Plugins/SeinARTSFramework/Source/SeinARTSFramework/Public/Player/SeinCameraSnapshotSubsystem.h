/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCameraSnapshotSubsystem.h
 * @brief   Local presentation bridge for snapshot camera capture and restore.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SeinCameraSnapshotSubsystem.generated.h"

struct FSeinCameraSnapshotData;

/**
 * Connects the simulation snapshot lifecycle to the local camera provider.
 * This subsystem is non-ticking and owns only world-lifetime presentation
 * delegates; deterministic match bootstrap has no camera responsibilities.
 */
UCLASS()
class SEINARTSFRAMEWORK_API USeinCameraSnapshotSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** Release Core-owned delegate callbacks before module withdrawal. */
	void ReleaseModuleOwnedStateForModuleUnload();

private:
	void UnbindSnapshotDelegates();
	void HandleSnapshotCapture(FSeinCameraSnapshotData& CameraState);
	void HandleSnapshotRestore(const FSeinCameraSnapshotData& CameraState);

	FDelegateHandle SnapshotCaptureHandle;
	FDelegateHandle SnapshotRestoreHandle;
};

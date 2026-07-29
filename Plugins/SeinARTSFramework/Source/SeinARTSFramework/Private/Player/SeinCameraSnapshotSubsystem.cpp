/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCameraSnapshotSubsystem.cpp
 */

#include "Player/SeinCameraSnapshotSubsystem.h"

#include "Data/SeinWorldSnapshot.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Simulation/SeinSnapshotCameraProvider.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace
{
	/** Search the local controller's presentation targets in preference order. */
	UObject* FindCameraProvider(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}

		APlayerController* PlayerController = World->GetFirstPlayerController();
		if (!PlayerController)
		{
			return nullptr;
		}

		auto ImplementsProvider = [](UObject* Candidate)
		{
			return Candidate
				&& Candidate->GetClass()->ImplementsInterface(USeinSnapshotCameraProvider::StaticClass());
		};

		if (APawn* Pawn = PlayerController->GetPawn(); ImplementsProvider(Pawn))
		{
			return Pawn;
		}
		if (AActor* ViewTarget = PlayerController->GetViewTarget(); ImplementsProvider(ViewTarget))
		{
			return ViewTarget;
		}
		return ImplementsProvider(PlayerController) ? PlayerController : nullptr;
	}
}

void USeinCameraSnapshotSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(USeinWorldSubsystem::StaticClass());
}

void USeinCameraSnapshotSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	USeinWorldSubsystem* WorldSubsystem = InWorld.GetSubsystem<USeinWorldSubsystem>();
	if (!WorldSubsystem)
	{
		UE_LOG(LogTemp, Error,
			TEXT("SeinCameraSnapshot: USeinWorldSubsystem missing; camera snapshot bridge was not bound."));
		return;
	}

	// Defensive against an unexpected repeated begin-play notification.
	UnbindSnapshotDelegates();
	SnapshotCaptureHandle = WorldSubsystem->OnCaptureSnapshotPostSim.AddUObject(
		this, &USeinCameraSnapshotSubsystem::HandleSnapshotCapture);
	SnapshotRestoreHandle = WorldSubsystem->OnRestoreSnapshotPostSim.AddUObject(
		this, &USeinCameraSnapshotSubsystem::HandleSnapshotRestore);
}

void USeinCameraSnapshotSubsystem::Deinitialize()
{
	ReleaseModuleOwnedStateForModuleUnload();
	Super::Deinitialize();
}

void USeinCameraSnapshotSubsystem::ReleaseModuleOwnedStateForModuleUnload()
{
	UnbindSnapshotDelegates();
}

void USeinCameraSnapshotSubsystem::UnbindSnapshotDelegates()
{
	if (UWorld* World = GetWorld())
	{
		if (USeinWorldSubsystem* WorldSubsystem = World->GetSubsystem<USeinWorldSubsystem>())
		{
			if (SnapshotCaptureHandle.IsValid())
			{
				WorldSubsystem->OnCaptureSnapshotPostSim.Remove(SnapshotCaptureHandle);
			}
			if (SnapshotRestoreHandle.IsValid())
			{
				WorldSubsystem->OnRestoreSnapshotPostSim.Remove(SnapshotRestoreHandle);
			}
		}
	}

	SnapshotCaptureHandle.Reset();
	SnapshotRestoreHandle.Reset();
}

void USeinCameraSnapshotSubsystem::HandleSnapshotCapture(
	FSeinCameraSnapshotData& CameraState)
{
	UObject* Provider = FindCameraProvider(GetWorld());
	if (!Provider)
	{
		return;
	}

	ISeinSnapshotCameraProvider::Execute_CaptureCameraState(Provider, CameraState);
}

void USeinCameraSnapshotSubsystem::HandleSnapshotRestore(
	const FSeinCameraSnapshotData& CameraState)
{
	UObject* Provider = FindCameraProvider(GetWorld());
	if (!Provider)
	{
		return;
	}

	ISeinSnapshotCameraProvider::Execute_RestoreCameraState(Provider, CameraState);
	UE_LOG(LogTemp, Log,
		TEXT("SeinCameraSnapshot: camera state restored via provider %s (class=%s)"),
		*Provider->GetName(), *Provider->GetClass()->GetName());
}

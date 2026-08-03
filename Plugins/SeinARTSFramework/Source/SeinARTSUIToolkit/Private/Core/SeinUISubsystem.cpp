/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinUISubsystem.cpp
 * @brief   UI Subsystem implementation.
 */

#include "Core/SeinUISubsystem.h"
#include "ViewModel/SeinEntityViewModel.h"
#include "ViewModel/SeinPlayerViewModel.h"
#include "ViewModel/SeinSelectionModel.h"
#include "ViewModel/SeinLobbyViewModel.h"
#include "ViewModel/SeinMinimapViewModel.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Player/SeinPlayerController.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinUI, Log, All);

void USeinUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Depend on the sim subsystem
	Collection.InitializeDependency<USeinWorldSubsystem>();

	WorldSubsystem = GetWorld()->GetSubsystem<USeinWorldSubsystem>();
	if (WorldSubsystem.IsValid())
	{
		SimFrameDelegateHandle = WorldSubsystem->OnSimFrameCompleted.AddUObject(
			this, &USeinUISubsystem::HandleSimFrame);
	}

	// Create the selection model
	SelectionModel = NewObject<USeinSelectionModel>(this);
	SelectionModel->Initialize(this);

	// The minimap view model is created lazily on first GetMinimapViewModel() — projects
	// that never show a minimap never pay for it.

	UE_LOG(LogSeinUI, Log, TEXT("SeinUISubsystem initialized"));
}

void USeinUISubsystem::Deinitialize()
{
	ReleaseModuleOwnedStateForModuleUnload();
	Super::Deinitialize();

	UE_LOG(LogSeinUI, Log, TEXT("SeinUISubsystem deinitialized"));
}

void USeinUISubsystem::ReleaseModuleOwnedStateForModuleUnload()
{
	check(IsInGameThread());
	if (WorldSubsystem.IsValid())
	{
		WorldSubsystem->OnSimFrameCompleted.Remove(SimFrameDelegateHandle);
	}
	SimFrameDelegateHandle.Reset();

	if (SelectionModel)
	{
		SelectionModel->Deinitialize();
	}

	if (LobbyViewModel)
	{
		LobbyViewModel->Shutdown();
	}

	EntityViewModels.Empty();
	PlayerViewModels.Empty();
	SelectionModel = nullptr;
	LobbyViewModel = nullptr;
	MinimapViewModel = nullptr;
	WorldSubsystem.Reset();
}

// ==================== ViewModel Access ====================

USeinEntityViewModel* USeinUISubsystem::GetEntityViewModel(FSeinEntityHandle Handle)
{
	if (!Handle.IsValid())
	{
		return nullptr;
	}

	// Return cached if exists
	TObjectPtr<USeinEntityViewModel>* Found = EntityViewModels.Find(Handle);
	if (Found && *Found)
	{
		return *Found;
	}

	// Create new
	if (!WorldSubsystem.IsValid())
	{
		return nullptr;
	}

	USeinEntityViewModel* VM = NewObject<USeinEntityViewModel>(this);
	VM->Initialize(Handle, WorldSubsystem.Get());
	EntityViewModels.Add(Handle, VM);

	return VM;
}

USeinPlayerViewModel* USeinUISubsystem::GetPlayerViewModel(FSeinPlayerID PlayerID)
{
	// Return cached if exists
	TObjectPtr<USeinPlayerViewModel>* Found = PlayerViewModels.Find(PlayerID);
	if (Found && *Found)
	{
		return *Found;
	}

	// Create new
	if (!WorldSubsystem.IsValid())
	{
		return nullptr;
	}

	USeinPlayerViewModel* VM = NewObject<USeinPlayerViewModel>(this);
	VM->Initialize(PlayerID, WorldSubsystem.Get());
	PlayerViewModels.Add(PlayerID, VM);

	return VM;
}

USeinPlayerViewModel* USeinUISubsystem::GetLocalPlayerViewModel()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	ASeinPlayerController* PC = Cast<ASeinPlayerController>(World->GetFirstPlayerController());
	if (!PC)
	{
		return nullptr;
	}

	return GetPlayerViewModel(PC->SeinPlayerID);
}

USeinLobbyViewModel* USeinUISubsystem::GetOrCreateLobbyViewModel()
{
	if (LobbyViewModel) return LobbyViewModel;

	UWorld* World = GetWorld();
	if (!World) return nullptr;

	LobbyViewModel = NewObject<USeinLobbyViewModel>(this);
	LobbyViewModel->Initialize(World);
	return LobbyViewModel;
}

USeinMinimapViewModel* USeinUISubsystem::GetMinimapViewModel()
{
	if (!MinimapViewModel)
	{
		MinimapViewModel = NewObject<USeinMinimapViewModel>(this);
		MinimapViewModel->Initialize(WorldSubsystem.Get(), GetWorld());
	}
	return MinimapViewModel;
}

// ==================== Latest-State Refresh ====================

void USeinUISubsystem::HandleSimFrame(
	int32 LatestTick, int32 /*TicksProcessed*/)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Presentation_UIRefresh);
	// Lazy-bind the selection model to the local player controller. Cheap
	// no-op once bound — the PC doesn't exist at subsystem Initialize time,
	// so we retry here until it comes up.
	if (SelectionModel)
	{
		SelectionModel->EnsurePlayerControllerBound();
		// Ability availability can change every sim tick, but a single UI frame
		// may ask for the same selection aggregate dozens of times through BP
		// events/bindings. Invalidate once; the first read rebuilds and the rest
		// share that exact latest-state snapshot.
		SelectionModel->InvalidateAbilityCache();
	}

	// Refresh all entity ViewModels
	for (auto& Pair : EntityViewModels)
	{
		if (Pair.Value)
		{
			Pair.Value->Refresh();
		}
	}

	// Refresh all player ViewModels
	for (auto& Pair : PlayerViewModels)
	{
		if (Pair.Value)
		{
			Pair.Value->Refresh();
		}
	}

	// Refresh the minimap view model
	if (MinimapViewModel)
	{
		MinimapViewModel->Refresh();
	}

	// Crossing a 30-tick bucket is sufficient; inequality also handles snapshot
	// restore/rewind, where the latest tick can move to an earlier bucket.
	const int32 CleanupBucket = LatestTick / 30;
	if (CleanupBucket != LastCleanupBucket)
	{
		CleanupStaleViewModels();
		LastCleanupBucket = CleanupBucket;
	}
}

void USeinUISubsystem::CleanupStaleViewModels()
{
	TArray<FSeinEntityHandle> ToRemove;

	for (const auto& Pair : EntityViewModels)
	{
		if (!Pair.Value || !Pair.Value->bIsAlive)
		{
			ToRemove.Add(Pair.Key);
		}
	}

	for (const FSeinEntityHandle& Handle : ToRemove)
	{
		EntityViewModels.Remove(Handle);
	}
}

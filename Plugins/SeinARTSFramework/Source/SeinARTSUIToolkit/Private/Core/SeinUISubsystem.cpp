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
#include "Simulation/SeinWorldSubsystem.h"
#include "Player/SeinPlayerController.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinUI, Log, All);

void USeinUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Depend on the sim subsystem
	Collection.InitializeDependency<USeinWorldSubsystem>();

	WorldSubsystem = GetWorld()->GetSubsystem<USeinWorldSubsystem>();
	if (WorldSubsystem.IsValid())
	{
		SimTickDelegateHandle = WorldSubsystem->OnSimTickCompleted.AddUObject(
			this, &USeinUISubsystem::HandleSimTick);
	}

	// Create the selection model
	SelectionModel = NewObject<USeinSelectionModel>(this);
	SelectionModel->Initialize(this);

	UE_LOG(LogSeinUI, Log, TEXT("SeinUISubsystem initialized"));
}

void USeinUISubsystem::Deinitialize()
{
	if (WorldSubsystem.IsValid())
	{
		WorldSubsystem->OnSimTickCompleted.Remove(SimTickDelegateHandle);
	}
	SimTickDelegateHandle.Reset();

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

	Super::Deinitialize();

	UE_LOG(LogSeinUI, Log, TEXT("SeinUISubsystem deinitialized"));
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

// ==================== Sim Tick Refresh ====================

void USeinUISubsystem::HandleSimTick(int32 Tick)
{
	// Lazy-bind the selection model to the local player controller. Cheap
	// no-op once bound — the PC doesn't exist at subsystem Initialize time,
	// so we retry here until it comes up.
	if (SelectionModel)
	{
		SelectionModel->EnsurePlayerControllerBound();
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

	// Periodically clean up stale entity ViewModels (every 30 ticks ~ 1 second)
	if (Tick % 30 == 0)
	{
		CleanupStaleViewModels();
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

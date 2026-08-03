/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinUISubsystem.h
 * @brief   Central hub for the SeinARTS UI Toolkit. Manages ViewModel lifecycle,
 *          caching, and latest-state refresh for all active ViewModels.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "SeinUISubsystem.generated.h"

class USeinEntityViewModel;
class USeinPlayerViewModel;
class USeinSelectionModel;
class USeinLobbyViewModel;
class USeinMinimapViewModel;
class USeinWorldSubsystem;

/**
 * World subsystem providing the UI toolkit's ViewModel layer.
 *
 * Responsibilities:
 * - Creates and caches entity ViewModels (one per observed entity)
 * - Creates and caches player ViewModels (one per player)
 * - Owns the selection model (one per world)
 * - Refreshes active ViewModels once after each engine frame's sim pump
 * - Cleans up ViewModels for dead entities
 */
UCLASS()
class SEINARTSUITOOLKIT_API USeinUISubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Idempotently sever every external callback and owned view-model root
	 *  before the UI module unloads. */
	void ReleaseModuleOwnedStateForModuleUnload();

	// ========== ViewModel Access ==========

	/**
	 * Get or create an entity ViewModel for the given handle.
	 * Returns a cached instance if one already exists.
	 * Returns nullptr if the handle is invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI")
	USeinEntityViewModel* GetEntityViewModel(FSeinEntityHandle Handle);

	/**
	 * Get or create a player ViewModel for the given player ID.
	 * Returns a cached instance if one already exists.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI")
	USeinPlayerViewModel* GetPlayerViewModel(FSeinPlayerID PlayerID);

	/**
	 * Convenience: get the local player's ViewModel.
	 * Returns nullptr if there is no local player controller.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI")
	USeinPlayerViewModel* GetLocalPlayerViewModel();

	/**
	 * Get the selection model (singleton per world).
	 * Tracks the local player's selection and provides entity ViewModels.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI")
	USeinSelectionModel* GetSelectionModel() const { return SelectionModel; }

	/**
	 * Get or create the lobby view model (singleton per world). Lazily
	 * initializes against this world's `USeinLobbySubsystem` on first access.
	 * Multiple widgets share this instance + its change-event subscription —
	 * BindEventToLobbyChanged once, refresh many.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Lobby")
	USeinLobbyViewModel* GetOrCreateLobbyViewModel();

	/**
	 * Get the minimap view-model (singleton per world), lazily creating it on first
	 * access. Projects that never show a minimap never create it (and so pay nothing);
	 * once created it refreshes from the latest completed state once per engine frame.
	 * Drives a minimap Blueprint's blips, fog
	 * overlay, and background.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Minimap")
	USeinMinimapViewModel* GetMinimapViewModel();

private:
	/** Called once after the frame's sim pump — refreshes latest presentation state. */
	void HandleSimFrame(int32 LatestTick, int32 TicksProcessed);

	/** Remove ViewModels for entities that are no longer alive. */
	void CleanupStaleViewModels();

	/** Cached sim subsystem. */
	UPROPERTY()
	TWeakObjectPtr<USeinWorldSubsystem> WorldSubsystem;

	/** Entity handle → ViewModel cache. */
	UPROPERTY()
	TMap<FSeinEntityHandle, TObjectPtr<USeinEntityViewModel>> EntityViewModels;

	/** Player ID → ViewModel cache. */
	UPROPERTY()
	TMap<FSeinPlayerID, TObjectPtr<USeinPlayerViewModel>> PlayerViewModels;

	/** Selection model (singleton). */
	UPROPERTY()
	TObjectPtr<USeinSelectionModel> SelectionModel;

	/** Lobby view model (lazy singleton). */
	UPROPERTY()
	TObjectPtr<USeinLobbyViewModel> LobbyViewModel;

	/** Minimap view model (singleton). */
	UPROPERTY()
	TObjectPtr<USeinMinimapViewModel> MinimapViewModel;

	/** Delegate handle for simulation-frame completion. */
	FDelegateHandle SimFrameDelegateHandle;

	/** Last cleanup bucket, preserving the ~1-second stale-VM cadence even
	 *  when multiple simulation ticks were coalesced into one callback. */
	int32 LastCleanupBucket = 0;
};

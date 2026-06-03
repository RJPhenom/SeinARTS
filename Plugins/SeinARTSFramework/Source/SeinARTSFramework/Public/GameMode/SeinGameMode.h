/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinGameMode.h
 * @brief   RTS game mode that wires up the player shell (controller, camera,
 *          HUD) and manages player registration with the simulation.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameplayTagContainer.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinFactionID.h"
#include "Data/SeinMatchSettings.h"
#include "Player/SeinPlayerController.h"
#include "Types/FixedPoint.h"
#include "SeinGameMode.generated.h"

class ASeinPlayerStart;

/**
 * Default RTS game mode.
 *
 * Sets default pawn/controller/HUD classes to the Sein RTS variants.
 * Assigns FSeinPlayerIDs to connecting players and registers them with
 * the deterministic simulation via USeinWorldSubsystem::RegisterPlayer().
 *
 * Designers subclass this in Blueprint to configure starting resources,
 * factions, team assignments, and match flow.
 */
UCLASS(Blueprintable)
class SEINARTSFRAMEWORK_API ASeinGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASeinGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	// ========== Configuration ==========

	/** Effective per-match max-players. Reads from `ResolvedMatchSettings.Slots.Num()`
	 *  if a snapshot has been resolved (lobby-published or WorldSettings preset);
	 *  falls back to `USeinARTSCoreSettings::MaxPlayers` ceiling otherwise.
	 *  PreviousI hardcoded `MaxPlayers = 8` field has been consolidated into
	 *  PluginSettings — the framework hard cap lives there. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|GameMode")
	int32 GetEffectiveMaxPlayers() const;

	/** Default faction assigned to players when no faction is specified. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|GameMode")
	FSeinFactionID DefaultFactionID = FSeinFactionID(1);

	/** Whether to auto-start the simulation when the first player joins. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|GameMode")
	bool bAutoStartSimulation = true;

	/**
	 * Starting-resource override map applied on top of the faction's ResourceKit
	 * at player registration. Keyed by resource tag (SeinARTS.Resource.*).
	 * Leave empty to use faction-kit defaults. Match-settings-level tweaks will
	 * eventually be superseded by a dedicated match-settings system (deferred).
	 *
	 * Value type is `FFixedPoint` (not `float`) because these values feed sim
	 * starting state — designer types fixed-point amounts directly so no
	 * runtime FromFloat happens on the spawn path. Cross-arch lockstep safe.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|GameMode",
		meta = (Categories = "SeinARTS.Resource"))
	TMap<FGameplayTag, FFixedPoint> StartingResources;

	// ========== Runtime State ==========

	/** Next player ID to assign. Incremented per player. 0 = Neutral (reserved). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|GameMode")
	uint8 NextPlayerIDValue = 1;

	/** Match-settings snapshot resolved at BeginPlay from `ASeinWorldSettings`.
	 *  Empty `Slots` ⇒ no manifest configured ⇒ legacy per-controller flow.
	 *  Future: GameInstance runtime override + plugin-settings PIE default
	 *  precede the WorldSettings lookup. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|GameMode")
	FSeinMatchSettings ResolvedMatchSettings;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|GameMode")
	bool bMatchSettingsResolved = false;

	// ========== Helpers ==========

	/** Manually register a player with a specific ID and faction. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|GameMode")
	void RegisterPlayerWithSim(FSeinPlayerID PlayerID, FSeinFactionID FactionID, uint8 TeamID = 0);

	/**
	 * Find a SeinPlayerStart for the given player slot.
	 * If SlotIndex is 0, returns the first unclaimed start.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|GameMode")
	ASeinPlayerStart* FindPlayerStartForSlot(int32 SlotIndex) const;

	/**
	 * Spawn the start entity defined on a SeinPlayerStart for the given player.
	 * Called automatically during player registration if the start has a SpawnEntity set.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|GameMode")
	void SpawnStartEntity(ASeinPlayerStart* PlayerStart, FSeinPlayerID PlayerID);

protected:
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/** Resolve match settings for this world. Returns:
	 *   - lobby snapshot if published (lobby flow)
	 *   - synthesized-from-SeinPlayerStarts if any exist (PIE-direct)
	 *   - nullptr if neither (level has no slot data) */
	const FSeinMatchSettings* ResolveMatchSettingsForWorld() const;

	/** Backing storage for the synthesized match settings (PIE-direct path).
	 *  Mutable so `ResolveMatchSettingsForWorld() const` can populate it.
	 *  The returned pointer aliases this member when synthesis is used. */
	mutable FSeinMatchSettings SynthesizedMatchSettings;

	/** Walk `ResolvedMatchSettings.Slots` and pre-register Human/AI slots:
	 *  RegisterPlayer + SpawnStartEntity at the matching SeinPlayerStart. Slots
	 *  are NOT added to `ClaimedSlots` here — that happens when a controller
	 *  binds in HandleStartingNewPlayer. */
	void PreRegisterMatchSlots();

	/** Whether simulation has been started. */
	bool bSimulationStarted = false;

	/** Slots that have been bound to a connecting controller. Drives
	 *  ChoosePlayerStart's "find next available Human slot" iteration. */
	UPROPERTY()
	TMap<int32, FSeinPlayerID> ClaimedSlots;

	/**
	 * Maps player IDs to their assigned slot index.
	 * Set by lobby/matchmaker before travel, or auto-assigned on connect.
	 * Key = FSeinPlayerID value, Value = target PlayerSlot.
	 * If empty, slots are assigned in order.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|GameMode")
	TMap<uint8, int32> PreAssignedSlots;
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPlayerStart.h
 * @brief   RTS player start with player slot assignment and per-faction spawn entity.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerStart.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinFactionID.h"
#include "Data/SeinMatchSettings.h"
#include "Types/Vector.h"
#include "SeinPlayerStart.generated.h"

class ASeinActor;

/**
 * RTS-aware player start point.
 *
 * Each SeinPlayerStart represents one player slot on the map. The GameMode
 * matches connecting players to starts by PlayerSlot, then spawns the
 * faction's SpawnEntity at this location.
 *
 * ## Editor Workflow
 * Place one SeinPlayerStart per player position in the map. Set PlayerSlot
 * to 1, 2, 3, etc. The GameMode assigns connecting players to slots in
 * order (or via lobby/matchmaker assignment).
 *
 * ## SpawnEntity
 * The SpawnEntity is an ASeinActor Blueprint that represents the faction's
 * starting presence (e.g., a headquarters building). Its BeginPlay / abilities
 * can trigger any game-start logic the designer wants (spawn workers, grant
 * starting tech, etc.). This keeps start-of-game setup entirely in Blueprint.
 *
 * ## Networked / Lobby Use
 * For matchmaking or skirmish lobbies, the lobby system assigns each player
 * a target slot index before travel. The GameMode's HandleStartingNewPlayer
 * then matches the player to the SeinPlayerStart with the corresponding
 * PlayerSlot value.
 */
UCLASS(Blueprintable)
class SEINARTSFRAMEWORK_API ASeinPlayerStart : public APlayerStart
{
	GENERATED_BODY()

public:
	ASeinPlayerStart(const FObjectInitializer& ObjectInitializer);

	// ========== Slot Configuration ==========

	/**
	 * Which player slot this start belongs to (1-based).
	 * The GameMode assigns players to starts by matching this value.
	 * 0 = unassigned / available to any player.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|PlayerStart", meta = (ClampMin = "0", ClampMax = "8"))
	int32 PlayerSlot = 0;

	/**
	 * Faction ID for this start position.
	 * Overrides the GameMode's DefaultFactionID when set (Value != 0).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|PlayerStart")
	FSeinFactionID FactionID;

	/**
	 * Team index for this start position.
	 * Used for team-based game modes (FFA = each player unique team).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|PlayerStart", meta = (ClampMin = "0"))
	uint8 TeamID = 0;

	/**
	 * The entity to spawn at this start location when a player claims this slot.
	 * Typically a headquarters / base building Blueprint.
	 * The spawned entity's BeginPlay can trigger any start-of-game logic
	 * (spawn workers, grant tech, set rally points, etc.).
	 * Leave null to skip spawn entity (player gets only camera + resources).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|PlayerStart")
	TSubclassOf<ASeinActor> SpawnEntity;

	/** Editor-baked snapshot of this start's spawn location — see
	 *  `ASeinActor::PlacedSimLocation` for the cross-platform-determinism
	 *  rationale. PostEditMove writes; SeinGameMode reads at sim-spawn
	 *  time. Migration: `bSimLocationBaked` distinguishes fresh placements
	 *  from legacy levels. */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "SeinARTS|Determinism")
	FFixedVector PlacedSimLocation = FFixedVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "SeinARTS|Determinism")
	bool bSimLocationBaked = false;

#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
#endif

	/** Synthesize a default `FSeinMatchSettings` from the level's
	 *  SeinPlayerStarts. Each PlayerStart with `PlayerSlot > 0` becomes a
	 *  `Human` slot in the manifest, with FactionID + TeamID copied from
	 *  the PlayerStart actor. Output is sorted by `SlotIndex` for
	 *  deterministic iteration order on both server and client.
	 *
	 *  Used by `ASeinGameMode::ResolveMatchSettingsForWorld` and
	 *  `USeinMatchBootstrapSubsystem::OnWorldBeginPlay` as the PIE-direct
	 *  fallback when no lobby snapshot is available — both sides walk the
	 *  same level data, produce the same `FSeinMatchSettings`, and pre-
	 *  register matching entity IDs for lockstep determinism. */
	static FSeinMatchSettings SynthesizeMatchSettingsFromLevel(UWorld* World);
};

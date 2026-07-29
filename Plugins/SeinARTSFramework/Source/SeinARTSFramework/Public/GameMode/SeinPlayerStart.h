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
#include "Types/Transform.h"
#include "SeinPlayerStart.generated.h"

class ASeinActor;

/**
 * RTS-aware player start point.
 *
 * Each SeinPlayerStart represents one player slot on the map. GameMode uses
 * PlayerSlot only for authority-side controller routing; the shared bootstrap
 * transaction materializes the optional SpawnEntity on every simulation peer.
 *
 * ## Editor Workflow
 * Place one SeinPlayerStart per player position in the map. Set PlayerSlot
 * to 1, 2, 3, etc. The GameMode assigns connecting players to slots in
 * order (or via lobby/matchmaker assignment).
 *
 * ## SpawnEntity
 * The SpawnEntity is an ASeinActor Blueprint that represents the faction's
 * starting presence (e.g., a headquarters building). Its authored sim
 * components are copied during bootstrap; deterministic follow-up behavior
 * belongs in abilities/systems after tick zero, not render-actor BeginPlay.
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
	 * 0 = not part of the default match manifest.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|PlayerStart", meta = (ClampMin = "0", ClampMax = "16"))
	int32 PlayerSlot = 0;

	/**
	 * Faction ID authored for this start position's match slot.
	 * Active Human and AI slots require a valid faction.
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
	 * The entity to spawn at this start's baked transform when the match is materialized.
	 * Typically a headquarters / base building Blueprint.
	 * Leave null to skip the entity while still materializing the player state.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|PlayerStart")
	TSubclassOf<ASeinActor> SpawnEntity;

	/** Editor-baked snapshot of this start's complete spawn transform.
	 *  `PostEditMove` performs the float-to-fixed conversion once and the
	 *  serialized fixed-point value is then identical on every peer.
	 *  `bSimTransformBaked` distinguishes current placements from levels
	 *  that must be re-saved after upgrading. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, AdvancedDisplay, Category = "SeinARTS|Determinism")
	FFixedTransform PlacedSimTransform;

	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "SeinARTS|Determinism")
	bool bSimTransformBaked = false;

#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
#endif

	/** Synthesize a default `FSeinMatchSettings` from the level's
	 *  SeinPlayerStarts. Each PlayerStart with `PlayerSlot > 0` becomes a
	 *  `Human` slot in the manifest, with FactionID + TeamID copied from
	 *  the PlayerStart actor. Project DefaultMatchExtensions are copied into
	 *  the direct-world contract. Output is sorted by `SlotIndex` for
	 *  deterministic iteration order on every peer.
	 *
	 *  Used by `ASeinGameMode::ResolveMatchSettingsForWorld` and
	 *  `USeinMatchBootstrapSubsystem::OnWorldBeginPlay` as the PIE-direct
	 *  fallback when no lobby snapshot is available. The transaction performs
	 *  strict semantic, anchor, and baked-transform validation before mutation. */
	static FSeinMatchSettings SynthesizeMatchSettingsFromLevel(UWorld* World);
};

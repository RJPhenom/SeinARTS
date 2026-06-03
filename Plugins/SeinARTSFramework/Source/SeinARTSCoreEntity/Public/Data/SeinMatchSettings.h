/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMatchSettings.h
 * @brief   Match-level configuration primitive — minimal framework shape.
 *
 * `FSeinMatchSettings` is the runtime struct snapshotted into
 * `USeinWorldSubsystem` at StartMatch (immutable after). The framework
 * intentionally ships only two fields:
 *   - `Slots` — the per-player slot manifest (framework-essential: drives
 *     player registration, spawn pipeline, lobby UI).
 *   - `Extensions` — opt-in `FInstancedStruct` array. Designers + framework
 *     subsystems with match-level rules ship their own USTRUCTs and look
 *     them up by type at runtime (`FindMatchExtension<T>`). Framework C++
 *     does NOT prescribe what's in here; designer scripts compose match
 *     behavior by including the rule structs their game needs.
 *
 * For a starter pack of common RTS knobs (friendly fire, time limit, etc.)
 * see `FSeinBasicMatchSettings` (Data/SeinBasicMatchSettings.h). Designers
 * include it in `Extensions` and read it from BP — framework code never
 * references `FSeinBasicMatchSettings` directly.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinFactionID.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "SeinMatchSettings.generated.h"

/**
 * Slot occupancy state. Drives whether the game mode spawns the slot's
 * PlayerStart entity at world begin and how a connecting controller is
 * bound. Open and Closed slots produce no spawn.
 */
UENUM(BlueprintType)
enum class ESeinSlotState : uint8
{
	/** Joinable but currently empty. No spawn until a controller claims it. */
	Open,
	/** Human player expected. Spawn at world begin; controller binds on connect. */
	Human,
	/** AI fills this slot. Spawn at world begin; AI controller registers later (DESIGN §16). */
	AI,
	/** Locked out. No spawn, no controller binding. */
	Closed,
};

/**
 * Per-slot match manifest entry. SlotIndex maps 1:1 to
 * `ASeinPlayerStart::PlayerSlot` for spawn-location lookup. Faction/team here
 * are the runtime source of truth — PlayerStart's same-named fields are
 * legacy/anchor metadata only.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinMatchSlot
{
	GENERATED_BODY()

	/** 1-based slot index. ClampMax tracks `USeinARTSCoreSettings::MaxPlayers`
	 *  ceiling (16). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match",
		meta = (ClampMin = "1", ClampMax = "16"))
	int32 SlotIndex = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match")
	ESeinSlotState State = ESeinSlotState::Open;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match")
	FSeinFactionID FactionID;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match",
		meta = (ClampMin = "0"))
	uint8 TeamID = 0;

	/** Lobby/scoreboard label ("RJ", "AI - Hard", "Open"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match")
	FText DisplayName;

	/** Optional AI personality tag (DESIGN §16 — designer-extended namespace). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match",
		meta = (Categories = "SeinARTS.AI"))
	FGameplayTag AIProfile;
};

/**
 * Match flow state. Transitions are command-driven (lockstep-deterministic).
 *
 * NOTE on `Starting`: framework no longer ships a pre-match countdown
 * (cut along with the opinionated rule fields). `Starting` is preserved
 * as an enum value so designer code that wants a custom countdown UX can
 * still observe the state, but framework auto-transitions Lobby → Starting
 * → Playing instantly. Designer countdown UI delays calling StartMatch.
 */
UENUM(BlueprintType)
enum class ESeinMatchState : uint8
{
	/** Pre-match, players joining, settings configurable. */
	Lobby,
	/** Transitional. Framework moves through this in 0 ticks. */
	Starting,
	/** Normal sim execution. */
	Playing,
	/** Sim halted. Optional command-rejection mode is per-call (see SetSimPaused). */
	Paused,
	/** Victory/defeat declared, cleanup phase. */
	Ending,
	/** Match over, replay saved, ready to return to lobby. */
	Ended,
};

/**
 * Runtime match configuration snapshot. Snapshotted into
 * `USeinWorldSubsystem::CurrentMatchSettings` at StartMatch; mutation after
 * that point is forbidden (desync-safe).
 *
 * Framework intentionally ships ONLY the slot manifest + extension array.
 * Match-level rules (friendly fire, resource sharing, time limits, victory
 * conditions, etc.) are designer-driven via `Extensions` — the framework
 * never reads or interprets them. See `FSeinBasicMatchSettings` for a
 * designer-convenience starter struct covering common RTS knobs.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinMatchSettings
{
	GENERATED_BODY()

	/** Per-slot occupancy + faction/team. Built at runtime by the lobby
	 *  (`ASeinLobbyState::Slots`) and snapshotted into this struct at
	 *  StartMatch. Game mode walks this at world-begin to spawn HQs for
	 *  Human/AI slots.
	 *
	 *  PIE-direct testing (no lobby): this array stays empty; the GameMode's
	 *  legacy fallback in `ChoosePlayerStart` routes connecting PCs to
	 *  `ASeinPlayerStart`s by `PlayerSlot` field — host = slot 1, window 2 =
	 *  slot 2, etc. SeinPlayerStarts are the source of truth for slot
	 *  positions in that path. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match")
	TArray<FSeinMatchSlot> Slots;

	/** Designer + framework-module match-rule extensions. Each system that
	 *  needs match-level configuration ships its own USTRUCT and looks it
	 *  up by type at runtime via `FindMatchExtension<T>`. Multiple structs
	 *  live in this list; designer composes their match by including the
	 *  rule structs their game uses (`FSeinBasicMatchSettings` for common
	 *  RTS knobs, custom structs for game-specific rules).
	 *
	 *  Framework code does NOT prescribe what's in here. Match flow + sim
	 *  state machine are driven by framework code; per-rule policy is
	 *  designer-authored. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match",
		meta = (SeinDeterministicOnly))
	TArray<FInstancedStruct> Extensions;
};

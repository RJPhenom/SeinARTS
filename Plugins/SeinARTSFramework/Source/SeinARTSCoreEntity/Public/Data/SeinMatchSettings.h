/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMatchSettings.h
 * @brief   Match-level configuration primitive — minimal framework shape.
 *
 * `FSeinMatchSettings` is the runtime contract installed by the one-shot
 * match bootstrap transaction (immutable after). The framework
 * intentionally ships only two fields:
 *   - `Slots` — the per-player slot manifest (framework-essential: drives
 *     player registration, spawn pipeline, lobby UI).
 *   - `Extensions` — opt-in `FInstancedStruct` array. Designers + framework
 *     subsystems with match-level rules ship their own USTRUCTs and look
 *     them up by type at runtime (`FindMatchExtension<T>`). Framework C++ owns
 *     only its explicitly documented extension types; designer scripts compose
 *     the remaining policy from project-specific rule structs.
 *
 * For a starter pack of common RTS knobs (friendly fire, time limit, etc.)
 * see `FSeinBasicMatchSettings` (Data/SeinBasicMatchSettings.h). Designers
 * include it in `Extensions` and read it from BP — framework code never
 * references `FSeinBasicMatchSettings` directly.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinFactionID.h"
#include "Core/SeinPlayerID.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "SeinMatchSettings.generated.h"

struct FSeinDeterministicValueDigestError;

/**
 * Slot occupancy state. Drives whether the shared bootstrap materializer
 * creates player state and an optional PlayerStart entity, and how the
 * authority-side GameMode binds a controller. Open and Closed slots produce
 * no deterministic player or spawn state.
 */
UENUM(BlueprintType)
enum class ESeinSlotState : uint8
{
	/** Joinable during lobby assembly; produces no deterministic match state while Open. */
	Open,
	/** Human player expected. Bootstrap creates state/spawn; GameMode binds its controller. */
	Human,
	/** AI fills this slot. Bootstrap creates state/spawn; AI policy attaches separately. */
	AI,
	/** Locked out. No spawn, no controller binding. */
	Closed,
};

/**
 * Per-slot match manifest entry. SlotIndex maps 1:1 to
 * `ASeinPlayerStart::PlayerSlot` for spawn-location lookup. Faction/team here
 * are the frozen runtime source of truth. PlayerStart's same-named fields are
 * authoring defaults used when a standalone/direct-PIE manifest is synthesized.
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

	/** Optional AI personality tag (DESIGN §16 — designer-extended namespace). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match",
		meta = (Categories = "SeinARTS.AI"))
	FGameplayTag AIProfile;
};

/**
 * Match flow state. Transitions are command-driven (lockstep-deterministic).
 *
 * NOTE on `Starting`: framework no longer ships a pre-match countdown.
 * Bootstrap seals tick zero in `Starting`; the first simulation tick advances
 * it to `Playing`. A standalone map can hold authorized tick zero with
 * `bAutoStartSim=false`, while network/replay adapters own their launch timing.
 */
UENUM(BlueprintType)
enum class ESeinMatchState : uint8
{
	/** Pre-match, players joining, settings configurable. */
	Lobby,
	/** Sealed tick-zero state; the first simulation tick enters Playing. */
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
 * Runtime match configuration snapshot. Canonically installed into
 * `USeinWorldSubsystem::CurrentMatchSettings` during bootstrap; mutation after
 * that point is forbidden (desync-safe).
 *
 * Framework intentionally ships only the slot manifest + extension array.
 * Match-level rules (friendly fire, resource sharing, time limits, victory
 * conditions, etc.) are designer-driven via `Extensions`. Each module reads
 * only the extension types it owns; see `FSeinBasicMatchSettings` for a
 * designer-convenience starter struct covering common RTS knobs.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinMatchSettings
{
	GENERATED_BODY()

	/** Per-slot occupancy + faction/team. A lobby/matchmaker may supply the
	 *  manifest directly. In standalone/direct PIE without a published lobby
	 *  snapshot, the Framework synthesizes it from authored SeinPlayerStarts.
	 *  The shared bootstrap materializer consumes the canonical result on every
	 *  simulation peer; GameMode only routes Human controllers to those slots. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match")
	TArray<FSeinMatchSlot> Slots;

	/** Designer + framework-module match-rule extensions. Each system that
	 *  needs match-level configuration ships its own USTRUCT and looks it
	 *  up by type at runtime via `FindMatchExtension<T>`. Multiple structs
	 *  live in this list; designer composes their match by including the
	 *  rule structs their game uses (`FSeinBasicMatchSettings` for common
	 *  RTS knobs, custom structs for game-specific rules).
	 *
	 *  A module may interpret only the extension types it explicitly owns.
	 *  Project-specific rule policy remains designer-authored. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match",
		meta = (SeinDeterministicOnly))
	TArray<FInstancedStruct> Extensions;
};

/** Deterministic payload for an administrator-issued EndMatch command. */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinEndMatchCommandPayload
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Match")
	FSeinPlayerID Winner;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Match")
	FGameplayTag Reason;
};

/**
 * Canonicalize slot/extension order and compute the runtime compatibility
 * digest. On failure, leaves Settings unchanged and invalidates OutDigest.
 * Semantic rules such as duplicate slot/type rejection remain the caller's
 * command-schema responsibility.
 */
SEINARTSCOREENTITY_API bool SeinCanonicalizeAndDigestMatchSettings(
	FSeinMatchSettings& Settings,
	FGuid& OutDigest,
	FSeinDeterministicValueDigestError* OutError = nullptr);

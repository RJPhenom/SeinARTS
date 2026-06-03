/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWorldSettings.h
 * @brief   Per-level WorldSettings placeholder for SeinARTS-aware projects.
 *
 * Currently empty — match-settings storage was removed when slots became
 * runtime-only state (owned by `ASeinLobbyState` for lobby flow, or
 * resolved from `ASeinPlayerStart::PlayerSlot` for PIE-direct testing).
 *
 * Kept as a class so designers can opt their levels into the SeinARTS
 * world-settings type for forward-compat — future per-level configuration
 * (scenario IDs, per-level extension overrides, etc.) lands here.
 *
 * To enable: Project Settings → Maps & Modes → World Settings Class →
 * `SeinWorldSettings`.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/WorldSettings.h"
#include "SeinWorldSettings.generated.h"

UCLASS(Blueprintable, ClassGroup = (SeinARTS), meta = (DisplayName = "Sein World Settings"))
class SEINARTSFRAMEWORK_API ASeinWorldSettings : public AWorldSettings
{
	GENERATED_BODY()

public:
	/**
	 * Whether the deterministic sim should auto-start when this map loads.
	 *
	 * - Gameplay maps: leave at default `true`. Single-player Standalone
	 *   starts the sim immediately at PlayerStart binding. Listen-server /
	 *   client paths still gate on the lockstep handshake (`Sein.Net.StartMatch`
	 *   or future auto-handshake).
	 * - Menu / lobby / non-gameplay maps: set to `false`. Without this, the
	 *   Standalone sim auto-starts in the menu and ticks into the void —
	 *   when the player then clicks HOST and the world reloads, the
	 *   GI-scoped `USeinNetSubsystem`'s lockstep state (`LastSubmittedTurn`,
	 *   `ReceivedTurns`, etc.) carries the stale ghost-sim's progress into
	 *   the new map, causing immediate gate stalls.
	 * - Gameplay maps that want to delay sim start (e.g. for an intro
	 *   cinematic, or to snapshot world state before tick 0): set to `false`
	 *   and trigger the start manually from a project BP / GameMode override.
	 *
	 * `ASeinGameMode` reads this flag and only auto-starts the sim when
	 * true. Has no effect on the listen-server lockstep gate, which has its
	 * own start trigger (`Sein.Net.StartMatch` console command, or the
	 * Layer 3 auto-handshake if/when that ships).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "Auto-Start Sim"))
	bool bAutoStartSim = true;
};

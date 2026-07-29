/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWorldSettings.h
 * @brief   Per-level ownership and launch policy for deterministic bootstrap.
 *
 * To enable: Project Settings → Maps & Modes → World Settings Class →
 * `SeinWorldSettings`.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/WorldSettings.h"
#include "SeinWorldSettings.generated.h"

/** Who is permitted to claim a standalone world's one-shot bootstrap barrier. */
UENUM(BlueprintType)
enum class ESeinWorldBootstrapIntent : uint8
{
	/** The Framework materializes, self-authorizes, and optionally starts the match. */
	AutomaticMatch,

	/** Replay or another explicit adapter supplies the contract, seed, and authorization. */
	ExternalOrchestrator,
};

UCLASS(Blueprintable, ClassGroup = (SeinARTS), meta = (DisplayName = "Sein World Settings"))
class SEINARTSFRAMEWORK_API ASeinWorldSettings : public AWorldSettings
{
	GENERATED_BODY()

public:
	/**
	 * Selects the immutable owner of standalone tick-zero bootstrap. Replay
	 * playback maps use External Orchestrator so world BeginPlay leaves Core in
	 * its pristine Awaiting state. A launcher may make the same choice without
	 * a duplicate map by travelling with
	 * `?SeinBootstrap=ExternalOrchestrator` in the world URL.
	 *
	 * Network worlds always use their topology adapter and ignore this setting.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "Bootstrap Intent"))
	ESeinWorldBootstrapIntent BootstrapIntent =
		ESeinWorldBootstrapIntent::AutomaticMatch;

	/**
	 * Whether an automatically bootstrapped standalone match starts ticking
	 * immediately after its exact local receipt is authorized.
	 *
	 * - Gameplay maps: leave at default `true`. Standalone starts only after
	 *   the complete tick-zero plan is sealed and locally authorized. Network
	 *   worlds remain behind their topology's receipt consensus.
	 * - Menu / lobby / non-gameplay maps: set to `false`. Without this, the
	 *   Standalone sim auto-starts in the menu and ticks into the void —
	 *   when the player then clicks HOST and the world reloads, the
	 *   GI-scoped `USeinNetSubsystem`'s lockstep state (`LastSubmittedTurn`,
	 *   `ReceivedTurns`, etc.) carries the stale ghost-sim's progress into
	 *   the new map, causing immediate gate stalls.
	 * - Gameplay maps that want to delay tick zero (e.g. for an intro
	 *   cinematic): set to `false`, then call Start Standalone Simulation after
	 *   the Framework has materialized and authorized the world.
	 *
	 * `USeinMatchBootstrapSubsystem` owns this policy. It does not affect
	 * network topology authorization, and it is not a replay suppression flag;
	 * use Bootstrap Intent = External Orchestrator for replay worlds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "Auto-Start Sim"))
	bool bAutoStartSim = true;
};

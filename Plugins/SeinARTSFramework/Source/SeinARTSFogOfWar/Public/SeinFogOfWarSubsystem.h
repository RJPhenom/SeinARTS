/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarSubsystem.h
 * @brief   Thin world subsystem that owns the active USeinFogOfWar instance.
 *
 *          Reads `USeinARTSCoreSettings::FogOfWarClass` on Initialize, new's
 *          up that class, and re-exposes it to the rest of the engine (reader
 *          BPFL, cross-module LOS delegate bind, editor bake button).
 *
 *          The subsystem does NOT know what a "grid" or "shadowcast" is — it
 *          only knows a USeinFogOfWar exists. All vision semantics live on
 *          the active subclass. Parallels USeinNavigationSubsystem.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SeinFogOfWarSubsystem.generated.h"

class USeinFogOfWar;
class USeinFogOfWarAsset;

UCLASS()
class SEINARTSFOGOFWAR_API USeinFogOfWarSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** The active fog-of-war instance. Never null after Initialize. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Fog Of War")
	USeinFogOfWar* GetFogOfWar() const { return FogOfWar; }

	/** Convenience accessor for BP + external callers that only have a
	 *  UObject-with-world. Returns null if the world has no fog subsystem. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Fog Of War", meta = (WorldContext = "WorldContextObject"))
	static USeinFogOfWar* GetFogOfWarForWorld(const UObject* WorldContextObject);

	/** Kick off a bake for every ASeinFogOfWarVolume in `World`. Returns
	 *  true if the bake started. Routes to `FoW->BeginBake(World)`. */
	static bool BeginBake(UWorld* World);

	/** Returns true if the active fog in `World` is currently baking. */
	static bool IsBaking(UWorld* World);

	/** Request bake cancellation for `World`'s active fog. */
	static void RequestCancelBake(UWorld* World);

private:

	/** The active fog-of-war for this world. Instantiated from
	 *  `USeinARTSCoreSettings::FogOfWarClass` during Initialize. */
	UPROPERTY(Transient)
	TObjectPtr<USeinFogOfWar> FogOfWar;

	/** Handle into USeinWorldSubsystem::OnSimTickCompleted. Stamp updates run
	 *  from the sim tick (not wall clock) so all clients recompute against
	 *  identical tick-N entity positions — lockstep-safe. */
	FDelegateHandle SimTickHandle;

	/** Called in OnWorldBeginPlay — scans fog volumes for a baked asset and
	 *  hands it to the fog impl. Idempotent. */
	void LoadBakedAssetIntoFogOfWar(UWorld& World);

	/** If no baked asset loaded, let the fog impl auto-size its grid from
	 *  the level's ASeinFogOfWarVolumes so stamping + debug viz work before
	 *  the bake pipeline lands. */
	void InitGridIfUnbaked(UWorld& World);

	/** Binds `OnSimTickCompleted` so stamps recompute on the sim-tick clock
	 *  at the plugin-settings `VisionTickInterval` cadence. */
	void BindStampTick(UWorld& World);

	/** Called each sim tick — applies `VisionTickInterval` gate, then drives
	 *  the impl's TickStamps for this tick's source snapshot. */
	void HandleSimTickCompleted(int32 CurrentTick);

	/** Binds cross-module delegates on USeinWorldSubsystem so sim code can
	 *  query visibility without importing fog-of-war headers. */
	void BindSimDelegates(UWorld& World);
};

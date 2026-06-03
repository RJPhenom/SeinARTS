/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarVisibilitySubsystem.h
 * @brief   Render-side actor visibility toggle driven by fog stamp state.
 *          Walks the actor bridge's entity-to-actor map each render frame,
 *          queries the local PC's VisionGroup for each entity's cell, and
 *          calls SetActorHiddenInGame + SetActorEnableCollision based on
 *          the result. Owned-by-observer actors are always visible — unit
 *          owners see their own units regardless of fog.
 *
 *          This is the "higher-tickrate responsiveness" path the original
 *          system used: stamp compute runs at sim-tick cadence (~10 Hz)
 *          with the stamp engine, while this subsystem polls those stamps
 *          at render-tick cadence (~60 Hz) and reacts by toggling actors.
 *          Non-deterministic cadence is fine because the POLL READS are
 *          deterministic per tick — every client sees the same entity
 *          appear/disappear at the same sim tick, just with wall-clock
 *          jitter on the exact frame of transition.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SeinFogOfWarVisibilitySubsystem.generated.h"

UCLASS()
class SEINARTSFOGOFWAR_API USeinFogOfWarVisibilitySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:

	// ========== Configuration ==========

	/** If true, hidden actors also get their collision disabled so they
	 *  can't be clicked / selected / LOS-ray-traced against through fog.
	 *  Default true — matches RTS convention. Disable when you need the
	 *  hide to be visuals-only (e.g. custom minimap picking). */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Fog Of War")
	bool bDisableCollisionWhenHidden = true;

	/** Seconds between visibility polls. 0 = every render frame
	 *  (~60 Hz). Bump to 0.05–0.1 if you want a coarser cadence — stamp
	 *  data is already updated at VisionTickInterval regardless, so the
	 *  only thing changing with this setting is how quickly the render
	 *  reacts to a stamp change. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Fog Of War",
		meta = (ClampMin = "0.0", UIMax = "0.5"))
	float PollInterval = 0.0f;

	// ========== UWorldSubsystem ==========

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ========== UTickableWorldSubsystem ==========

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

private:

	float TimeSinceLastPoll = 0.f;
};

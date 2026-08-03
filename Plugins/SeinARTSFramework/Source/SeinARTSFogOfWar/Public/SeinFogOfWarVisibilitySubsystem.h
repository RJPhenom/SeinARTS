/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarVisibilitySubsystem.h
 * @brief   Render-side actor visibility toggle driven by fog stamp state.
 *          Walks bridged actors only when fog, observer, actor membership, or
 *          simulation state changes,
 *          queries the local PC's VisionGroup for each entity's cell, and
 *          calls SetActorHiddenInGame + SetActorEnableCollision based on
 *          the result. Owned-by-observer actors are always visible — unit
 *          owners see their own units regardless of fog.
 *
 *          Stamp compute and simulation-frame notifications dirty this
 *          presentation cache. Render-only frames do not repeat the O(N)
 *          actor visibility walk, and catch-up ticks are coalesced.
 *          Non-deterministic cadence is fine because the POLL READS are
 *          deterministic per tick — every client sees the same entity
 *          appear/disappear at the same sim tick, just with wall-clock
 *          jitter on the exact frame of transition.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinEntityHandle.h"
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

	/** Sever cross-module callbacks before the FogOfWar DLL unloads. */
	void ReleaseModuleOwnedStateForModuleUnload();

	// ========== UTickableWorldSubsystem ==========

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

private:

	float TimeSinceLastPoll = 0.f;
	FSeinPlayerID CachedObserver;
	bool bObserverResolved = false;
	bool bVisibilityDirty = true;
	TWeakObjectPtr<class USeinFogOfWar> SubscribedFog;
	FDelegateHandle FogMutatedHandle;
	TWeakObjectPtr<class USeinActorBridgeSubsystem> SubscribedBridge;
	FDelegateHandle ActorRegisteredHandle;
	TWeakObjectPtr<class USeinWorldSubsystem> SubscribedSim;
	FDelegateHandle SimFrameHandle;
	bool bModuleStateReleased = false;

	void ReleaseSubscriptions();
	void HandleFogMutated();
	void HandleActorRegistered(FSeinEntityHandle Handle);
	void HandleSimFrame(int32 LatestTick, int32 TicksProcessed);
};

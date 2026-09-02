/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarVisibilitySubsystem.cpp
 */

#include "SeinFogOfWarVisibilitySubsystem.h"

#include "SeinFogOfWar.h"
#include "SeinFogOfWarSubsystem.h"
#include "SeinARTSFogOfWarModule.h"

#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Actor/SeinActor.h"
#include "Core/SeinEntityHandle.h"

#include "Engine/World.h"
#include "Stats/Stats.h"

void USeinFogOfWarVisibilitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<USeinFogOfWarSubsystem>();
	Collection.InitializeDependency<USeinActorBridgeSubsystem>();
	Collection.InitializeDependency<USeinWorldSubsystem>();
	if (UWorld* World = GetWorld())
	{
		if (USeinFogOfWarSubsystem* FogSub =
			World->GetSubsystem<USeinFogOfWarSubsystem>())
		{
			if (USeinFogOfWar* Fog = FogSub->GetFogOfWar())
			{
				SubscribedFog = Fog;
				FogMutatedHandle = Fog->OnFogOfWarMutated.AddUObject(
					this, &USeinFogOfWarVisibilitySubsystem::HandleFogMutated);
			}
		}
		if (USeinActorBridgeSubsystem* Bridge =
			World->GetSubsystem<USeinActorBridgeSubsystem>())
		{
			SubscribedBridge = Bridge;
			ActorRegisteredHandle = Bridge->OnActorRegistered.AddUObject(
				this, &USeinFogOfWarVisibilitySubsystem::HandleActorRegistered);
		}
		if (USeinWorldSubsystem* Sim =
			World->GetSubsystem<USeinWorldSubsystem>())
		{
			SubscribedSim = Sim;
			SimFrameHandle = Sim->OnSimFrameCompleted.AddUObject(
				this, &USeinFogOfWarVisibilitySubsystem::HandleSimFrame);
		}
	}
}

void USeinFogOfWarVisibilitySubsystem::Deinitialize()
{
	ReleaseSubscriptions();
	Super::Deinitialize();
}

void USeinFogOfWarVisibilitySubsystem::ReleaseModuleOwnedStateForModuleUnload()
{
	check(IsInGameThread());
	bModuleStateReleased = true;
	ReleaseSubscriptions();
}

void USeinFogOfWarVisibilitySubsystem::ReleaseSubscriptions()
{
	if (USeinFogOfWar* Fog = SubscribedFog.Get())
	{
		Fog->OnFogOfWarMutated.Remove(FogMutatedHandle);
	}
	if (USeinActorBridgeSubsystem* Bridge = SubscribedBridge.Get())
	{
		Bridge->OnActorRegistered.Remove(ActorRegisteredHandle);
	}
	if (USeinWorldSubsystem* Sim = SubscribedSim.Get())
	{
		Sim->OnSimFrameCompleted.Remove(SimFrameHandle);
	}
	SubscribedFog.Reset();
	SubscribedBridge.Reset();
	SubscribedSim.Reset();
	FogMutatedHandle.Reset();
	ActorRegisteredHandle.Reset();
	SimFrameHandle.Reset();
}

bool USeinFogOfWarVisibilitySubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return !bModuleStateReleased && World != nullptr && World->IsGameWorld();
}

TStatId USeinFogOfWarVisibilitySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USeinFogOfWarVisibilitySubsystem, STATGROUP_Tickables);
}

void USeinFogOfWarVisibilitySubsystem::Tick(float DeltaTime)
{
	// Poll-interval gate. Default 0 = every frame; bumping skips frames
	// without costing a poll. Stamps themselves are always current at the
	// last VisionTickInterval — this just throttles the react-rate.
	TimeSinceLastPoll += DeltaTime;
	if (PollInterval > 0.f && TimeSinceLastPoll < PollInterval) return;
	TimeSinceLastPoll = 0.f;

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld()) return;

	USeinFogOfWarSubsystem* FogSub = World->GetSubsystem<USeinFogOfWarSubsystem>();
	if (!FogSub) return;
	USeinFogOfWar* Fog = FogSub->GetFogOfWar();
	if (!Fog) return;

	// Permissive when fog has no runtime data — matches LOS-resolver
	// behavior. Nothing hides; system is effectively a no-op.
	if (!Fog->HasRuntimeData()) return;

	USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
	USeinActorBridgeSubsystem* Bridge = World->GetSubsystem<USeinActorBridgeSubsystem>();
	if (!Sim || !Bridge) return;

	const FSeinPlayerID Observer = UE::SeinARTSFogOfWar::ResolveLocalObserverPlayerID(World);
	if (!bObserverResolved || Observer != CachedObserver)
	{
		CachedObserver = Observer;
		bObserverResolved = true;
		bVisibilityDirty = true;
	}
	if (!bVisibilityDirty) return;
	bVisibilityDirty = false;

	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Presentation_FogActorVisibility);
	// Walk only materialized render actors. Entity movement, owner/policy/mask
	// changes, and catch-up pumps dirty this pass via OnSimFrameCompleted, while
	// render-only frames skip it entirely.
	const bool bDisableColl = bDisableCollisionWhenHidden;
	Bridge->ForEachRegisteredActor(
		[this, Sim, Fog, Observer, bDisableColl](
			FSeinEntityHandle Handle, ASeinActor& Actor)
		{
			// Centralized policy + owner + bits check — same helper the
			// cover system uses to gate per-player query results. Single
			// source of truth: FSeinFogVisibilityPayload::FogVisibilityPolicy
			// + owner-sees-own + Explored-bit reveal + active-vision check
			// all live inside `USeinFogOfWar::IsEntityVisibleToObserver`.
			const bool bVisible = Fog->IsEntityVisibleToObserver(Observer, *Sim, Handle);

			const bool bShouldBeHidden = !bVisible;
			if (Actor.IsHidden() != bShouldBeHidden)
			{
				Actor.SetActorHiddenInGame(bShouldBeHidden);
				if (bDisableColl)
				{
					Actor.SetActorEnableCollision(bVisible);
				}
			}
		});
}

void USeinFogOfWarVisibilitySubsystem::HandleFogMutated()
{
	bVisibilityDirty = true;
}

void USeinFogOfWarVisibilitySubsystem::HandleActorRegistered(
	FSeinEntityHandle /*Handle*/)
{
	bVisibilityDirty = true;
}

void USeinFogOfWarVisibilitySubsystem::HandleSimFrame(
	int32 /*LatestTick*/, int32 /*TicksProcessed*/)
{
	bVisibilityDirty = true;
}

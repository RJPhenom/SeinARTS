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
#include "Core/SeinEntityPool.h"
#include "Core/SeinEntityHandle.h"
#include "Types/Entity.h"

#include "Engine/World.h"
#include "Stats/Stats.h"

void USeinFogOfWarVisibilitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void USeinFogOfWarVisibilitySubsystem::Deinitialize()
{
	Super::Deinitialize();
}

bool USeinFogOfWarVisibilitySubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->IsGameWorld();
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

	// Walk live entities. Bridge lookup is O(log N) per entity; per-frame
	// cost is O(N) over live entities. 1000 entities × 60 FPS = 60k
	// lookups/sec — comfortably under a millisecond on modern hardware.
	const bool bDisableColl = bDisableCollisionWhenHidden;
	Sim->GetEntityPool().ForEachEntity(
		[this, Sim, Bridge, Fog, Observer, bDisableColl](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			ASeinActor* Actor = Bridge->GetActorForEntity(Handle);
			if (!Actor) return; // abstract entity (broker/squad) or not yet bridged

			// Centralized policy + owner + bits check — same helper the
			// cover system uses to gate per-player query results. Single
			// source of truth: FSeinFogVisibilityComponent::FogVisibilityPolicy
			// + owner-sees-own + Explored-bit reveal + active-vision check
			// all live inside `USeinFogOfWar::IsEntityVisibleToObserver`.
			const bool bVisible = Fog->IsEntityVisibleToObserver(Observer, *Sim, Handle);

			const bool bShouldBeHidden = !bVisible;
			if (Actor->IsHidden() != bShouldBeHidden)
			{
				Actor->SetActorHiddenInGame(bShouldBeHidden);
				if (bDisableColl)
				{
					Actor->SetActorEnableCollision(bVisible);
				}
			}
		});
}

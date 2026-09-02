/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinActorBridgeSubsystem.cpp
 * @brief   Actor bridge implementation — spawns actors, syncs transforms,
 *          routes visual events from sim to render layer.
 */

#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Actor/SeinActor.h"
#include "Actor/SeinEntityBridgeComponent.h"
#include "Events/SeinVisualEvent.h"
#include "Types/FixedPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "SeinARTSCoreEntityLog.h"  // LogSeinBridge (module-shared)

namespace SeinBridgeLocal
{
	/** True if the entity component on this actor class marks it abstract
	 *  (no render-side existence, sim-only). Walks SCS so BP-added entity
	 *  components are visible — `GetDefault<ASeinActor>(Class)` only sees
	 *  native default subobjects. */
	static bool IsClassAbstract(TSubclassOf<class ASeinActor> ActorClass)
	{
		if (!ActorClass) return false;

		TArray<const USeinEntityBridgeComponent*> EntityComps;
		AActor::GetActorClassDefaultComponents<USeinEntityBridgeComponent>(ActorClass, EntityComps);
		for (const USeinEntityBridgeComponent* EC : EntityComps)
		{
			if (EC && EC->bIsAbstract) return true;
		}
		return false;
	}
}

void USeinActorBridgeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	SimSubsystem = GetWorld()->GetSubsystem<USeinWorldSubsystem>();
	if (SimSubsystem.IsValid())
	{
		SimFrameDelegateHandle = SimSubsystem->OnSimFrameCompleted.AddUObject(
			this, &USeinActorBridgeSubsystem::HandleSimFrame);
	}

	UE_LOG(LogSeinBridge, Log, TEXT("SeinActorBridgeSubsystem initialized"));
}

void USeinActorBridgeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!bAutoRegisterOnBeginPlay)
	{
		UE_LOG(LogSeinBridge, Log,
			TEXT("OnWorldBeginPlay: auto-register disabled (a match-flow orchestrator owns the bootstrap order)."));
		return;
	}

	RegisterAllPlacedActors(InWorld);
}

void USeinActorBridgeSubsystem::RegisterAllPlacedActors(UWorld& InWorld)
{
	if (!SimSubsystem.IsValid()) return;

	// Collect every ASeinActor in the level, then sort by actor name BEFORE
	// iterating. This is the determinism gate for placed-actor entity-ID
	// assignment: TActorIterator's natural order is implementation-defined
	// and varies between processes, so each PIE window (server + clients)
	// would otherwise hand out different IDs to the same level actors and
	// lockstep would diverge from frame zero. Lexicographic FString sort by
	// AActor::GetName() is stable across processes (the name is the actor's
	// in-level label, identical on every machine for the same level).
	TArray<ASeinActor*> Sorted;
	Sorted.Reserve(64);
	for (TActorIterator<ASeinActor> It(&InWorld); It; ++It)
	{
		if (ASeinActor* A = *It) Sorted.Add(A);
	}
	Sorted.Sort([](const ASeinActor& A, const ASeinActor& B)
	{
		return A.GetName() < B.GetName();
	});

	int32 NumRegistered = 0;
	int32 NumSkipped = 0;
	for (ASeinActor* PlacedActor : Sorted)
	{
		if (PlacedActor->HasValidEntity())
		{
			++NumSkipped;
		}
		else if (RegisterPlacedActor(*PlacedActor).IsValid())
		{
			++NumRegistered;
		}
	}

	UE_LOG(LogSeinBridge, Log,
		TEXT("RegisterAllPlacedActors: stable-sorted, registered %d placed ASeinActor(s); %d already had entities."),
		NumRegistered, NumSkipped);
}

FSeinEntityHandle USeinActorBridgeSubsystem::RegisterPlacedActor(
	ASeinActor& PlacedActor)
{
	if (!SimSubsystem.IsValid() || PlacedActor.HasValidEntity())
	{
		return FSeinEntityHandle::Invalid();
	}

	const FSeinPlayerID PlacedOwner = PlacedActor.PlayerSlot > 0
		? FSeinPlayerID(static_cast<uint8>(PlacedActor.PlayerSlot))
		: FSeinPlayerID::Neutral();
	const FSeinEntityHandle Handle = SimSubsystem->SpawnEntityFromPlacedActor(
		&PlacedActor, PlacedOwner);
	if (!Handle.IsValid())
	{
		return FSeinEntityHandle::Invalid();
	}

	// Actor-side Blueprint callbacks may query their entity immediately after
	// bridge registration, so stamp the handle first.
	PlacedActor.InitializeWithEntity(Handle);
	RegisterActor(Handle, &PlacedActor);
	return Handle;
}

void USeinActorBridgeSubsystem::Deinitialize()
{
	if (SimSubsystem.IsValid())
	{
		SimSubsystem->OnSimFrameCompleted.Remove(SimFrameDelegateHandle);
	}
	SimFrameDelegateHandle.Reset();
	OnActorRegistered.Clear();
	EntityActorMap.Empty();

	Super::Deinitialize();

	UE_LOG(LogSeinBridge, Log, TEXT("SeinActorBridgeSubsystem deinitialized"));
}

TStatId USeinActorBridgeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USeinActorBridgeSubsystem, STATGROUP_Tickables);
}

// ==================== Tick (Render Frame) ====================

void USeinActorBridgeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!SimSubsystem.IsValid())
	{
		return;
	}

	// Flush and dispatch all visual events queued by the sim
	TArray<FSeinVisualEvent> Events = SimSubsystem->FlushVisualEvents();
	for (const FSeinVisualEvent& Event : Events)
	{
		DispatchVisualEvent(Event);
	}

	// Child-transform poses are now applied by a render-side AC subscribing
	// to USeinEntityBridgeComponent::OnVisualEvent and ticking against sim state
	// directly. The bridge no longer fans poses out — keeps it lean.
}

bool USeinActorBridgeSubsystem::IsTickable() const
{
	// Only tick when there is render-side work: pending visual events to drain, or actors under
	// management. When the sim is dormant (no entities, no events) this returns false, so the bridge
	// costs zero per frame. Transform sync runs on the OnSimFrameCompleted delegate, not this Tick, so
	// skipping the engine Tick never affects it.
	return SimSubsystem.IsValid()
		&& (SimSubsystem->HasPendingVisualEvents() || EntityActorMap.Num() > 0);
}

// ==================== Simulation Frame Callback ====================

void USeinActorBridgeSubsystem::HandleSimFrame(
	int32 /*LatestTick*/, int32 TicksProcessed)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Presentation_ActorBridgeSync);
	for (auto It = EntityActorMap.CreateIterator(); It; ++It)
	{
		if (!It->Value.IsValid())
		{
			// Actor was destroyed externally — remove stale entry.
			It.RemoveCurrent();
			continue;
		}

		ASeinActor* Actor = It->Value.Get();

		// Shift the transform snapshot on the entity component. The entity
		// component IS the bridge surface — render-side ACs that need sim
		// state subscribe to its OnVisualEvent multicast or query sim
		// storage directly.
		if (USeinEntityBridgeComponent* Comp = Actor->GetEntityBridge())
		{
			Comp->OnSimFrame(TicksProcessed);
		}
	}
}

// ==================== Visual Event Dispatch ====================

void USeinActorBridgeSubsystem::DispatchVisualEvent(const FSeinVisualEvent& Event)
{
	// Broadcast to global UI listeners before per-actor routing
	OnVisualEventDispatched.Broadcast(Event);

	switch (Event.Type)
	{
	case ESeinVisualEventType::EntitySpawned:
		SpawnActorForEntity(Event.PrimaryEntity, Event);
		break;

	case ESeinVisualEventType::EntityDestroyed:
		HandleEntityDestroyed(Event.PrimaryEntity, Event);
		break;

	case ESeinVisualEventType::TechResearched:
		// Broadcast for UI refresh
		OnTechResearched.Broadcast(Event.PlayerID, Event.Tag);
		break;

	case ESeinVisualEventType::Ping:
		// Ping events are not entity-specific — no actor routing needed.
		// HUD/UI systems should listen for these via a custom delegate or poll.
		break;

	default:
	{
		// Route to the target actor's entity component — its OnVisualEvent
		// multicast fans out to subscribed render-side ACs.
		TWeakObjectPtr<ASeinActor>* ActorPtr = EntityActorMap.Find(Event.PrimaryEntity);
		if (ActorPtr && ActorPtr->IsValid())
		{
			ASeinActor* Actor = ActorPtr->Get();
			if (USeinEntityBridgeComponent* Comp = Actor->FindComponentByClass<USeinEntityBridgeComponent>())
			{
				Comp->HandleVisualEvent(Event);
			}
		}
		break;
	}
	}
}

// ==================== Actor Lifecycle ====================

void USeinActorBridgeSubsystem::SpawnActorForEntity(FSeinEntityHandle Handle, const FSeinVisualEvent& SpawnEvent)
{
	if (!SimSubsystem.IsValid()) return;

	// Don't double-spawn
	if (EntityActorMap.Contains(Handle))
	{
		UE_LOG(LogSeinBridge, Warning, TEXT("Actor already exists for entity %s, skipping spawn"), *Handle.ToString());
		return;
	}

	TSubclassOf<ASeinActor> ActorClass = SimSubsystem->GetEntityActorClass(Handle);
	if (!ActorClass)
	{
		UE_LOG(LogSeinBridge, Error, TEXT("No actor class stored for entity %s"), *Handle.ToString());
		return;
	}

	// Abstract entities skip actor spawn entirely (per §1 bIsAbstract). Downstream
	// visual events find no actor in EntityActorMap and no-op gracefully.
	// Checks both the legacy archetype-definition flag AND the new entity-component
	// top-level flag during the Phase-1 migration window.
	if (SeinBridgeLocal::IsClassAbstract(ActorClass))
	{
		UE_LOG(LogSeinBridge, Verbose, TEXT("Entity %s is abstract (class %s); skipping actor spawn"),
			*Handle.ToString(), *ActorClass->GetName());
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	// Convert fixed-point spawn location to float for actor placement
	const FVector SpawnLocation = SpawnEvent.Location.ToVector();
	const FRotator SpawnRotation = FRotator::ZeroRotator;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ASeinActor* NewActor = World->SpawnActor<ASeinActor>(ActorClass, SpawnLocation, SpawnRotation, SpawnParams);
	if (!NewActor)
	{
		UE_LOG(LogSeinBridge, Error, TEXT("Failed to spawn actor for entity %s (class: %s)"),
			*Handle.ToString(), *ActorClass->GetName());
		return;
	}

	NewActor->InitializeWithEntity(Handle);
	RegisterActor(Handle, NewActor);

	UE_LOG(LogSeinBridge, Verbose, TEXT("Spawned actor %s for entity %s"),
		*NewActor->GetName(), *Handle.ToString());
}

void USeinActorBridgeSubsystem::HandleEntityDestroyed(FSeinEntityHandle Handle, const FSeinVisualEvent& DestroyEvent)
{
	TWeakObjectPtr<ASeinActor>* ActorPtr = EntityActorMap.Find(Handle);
	if (!ActorPtr || !ActorPtr->IsValid())
	{
		EntityActorMap.Remove(Handle);
		return;
	}

	ASeinActor* Actor = ActorPtr->Get();

	// Route the destroy event through the entity component's multicast — render
	// ACs subscribed to OnVisualEvent see the EntityDestroyed type and can
	// clean up (e.g. construction render AC's EndPlay cleanup of placeholders).
	if (USeinEntityBridgeComponent* Comp = Actor->FindComponentByClass<USeinEntityBridgeComponent>())
	{
		Comp->HandleVisualEvent(DestroyEvent);
	}

	// Give the actor time for death animations before cleanup
	Actor->SetLifeSpan(DestroyActorDelay);

	// Remove from our map immediately so we don't route further events to it
	EntityActorMap.Remove(Handle);

	UE_LOG(LogSeinBridge, Verbose, TEXT("Entity %s destroyed — actor %s scheduled for removal in %.1fs"),
		*Handle.ToString(), *Actor->GetName(), DestroyActorDelay);
}

void USeinActorBridgeSubsystem::ReconcileBridgeAfterRestore()
{
	if (!SimSubsystem.IsValid()) return;
	USeinWorldSubsystem* Sim = SimSubsystem.Get();

	// Pass 1 — cull orphans. Walk the bridge map; for any handle whose sim
	// entity no longer exists, scrub the actor.
	int32 NumOrphansCulled = 0;
	for (auto It = EntityActorMap.CreateIterator(); It; ++It)
	{
		const FSeinEntityHandle Handle = It->Key;
		const bool bSimAlive = Sim->IsEntityAlive(Handle);
		if (bSimAlive) continue;

		ASeinActor* Actor = It->Value.Get();
		if (Actor)
		{
			// Synthesize an EntityDestroyed event and route through the entity
			// component so subscribed render ACs can clean up — same path
			// HandleEntityDestroyed uses, minus the visual-event payload from
			// the sim.
			if (USeinEntityBridgeComponent* Comp = Actor->FindComponentByClass<USeinEntityBridgeComponent>())
			{
				FSeinVisualEvent DestroyEvent;
				DestroyEvent.Type = ESeinVisualEventType::EntityDestroyed;
				DestroyEvent.PrimaryEntity = Handle;
				Comp->HandleVisualEvent(DestroyEvent);
			}
			Actor->SetLifeSpan(DestroyActorDelay);
			++NumOrphansCulled;
			UE_LOG(LogSeinBridge, Verbose,
				TEXT("ReconcileBridgeAfterRestore: culling orphan actor %s (entity %s no longer in sim)"),
				*Actor->GetName(), *Handle.ToString());
		}
		It.RemoveCurrent();
	}

	// Pass 2 — spawn missing. Walk every alive sim entity; if no actor in
	// the bridge map, spawn one using the entity's stored actor class. Skips
	// abstract entities (per §1 bIsAbstract). Uses the entity's current sim
	// transform as the spawn location — same as a normal EntitySpawned event.
	int32 NumActorsSpawned = 0;
	int32 NumAbstractSkipped = 0;
	int32 NumMissingClass = 0;
	Sim->GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, const FSeinEntity& Entity)
	{
		if (EntityActorMap.Contains(Handle)) return;

		TSubclassOf<ASeinActor> ActorClass = Sim->GetEntityActorClass(Handle);
		if (!ActorClass)
		{
			++NumMissingClass;
			return;
		}

		if (SeinBridgeLocal::IsClassAbstract(ActorClass))
		{
			++NumAbstractSkipped;
			return;
		}

		// Build a synthetic spawn event and route it through the regular
		// spawn path so AC OnEntitySpawned hooks fire identically to the
		// normal EntitySpawned visual-event flow.
		FSeinVisualEvent SpawnEvent;
		SpawnEvent.Type = ESeinVisualEventType::EntitySpawned;
		SpawnEvent.PrimaryEntity = Handle;
		SpawnEvent.Location = Entity.Transform.GetLocation();
		SpawnActorForEntity(Handle, SpawnEvent);
		if (EntityActorMap.Contains(Handle))
		{
			++NumActorsSpawned;
		}
	});

	UE_LOG(LogSeinBridge, Log,
		TEXT("ReconcileBridgeAfterRestore: culled %d orphan actor(s), spawned %d missing actor(s), skipped %d abstract, %d entities had no class registered."),
		NumOrphansCulled, NumActorsSpawned, NumAbstractSkipped, NumMissingClass);
}

// ==================== Public API ====================

ASeinActor* USeinActorBridgeSubsystem::GetActorForEntity(FSeinEntityHandle Handle) const
{
	const TWeakObjectPtr<ASeinActor>* Found = EntityActorMap.Find(Handle);
	return (Found && Found->IsValid()) ? Found->Get() : nullptr;
}

void USeinActorBridgeSubsystem::ForEachRegisteredActor(
	TFunctionRef<void(FSeinEntityHandle, ASeinActor&)> Visitor) const
{
	for (const TPair<FSeinEntityHandle, TWeakObjectPtr<ASeinActor>>& Pair :
		EntityActorMap)
	{
		if (ASeinActor* Actor = Pair.Value.Get())
		{
			Visitor(Pair.Key, *Actor);
		}
	}
}

void USeinActorBridgeSubsystem::RegisterActor(FSeinEntityHandle Handle, ASeinActor* Actor)
{
	if (!Handle.IsValid() || !Actor)
	{
		UE_LOG(LogSeinBridge, Warning, TEXT("RegisterActor: invalid handle or null actor"));
		return;
	}

	EntityActorMap.Add(Handle, Actor);
	OnActorRegistered.Broadcast(Handle);

	UE_LOG(LogSeinBridge, Verbose,
		TEXT("RegisterActor: %s linked to entity %s."),
		*Actor->GetName(), *Handle.ToString());
}

void USeinActorBridgeSubsystem::UnregisterActor(FSeinEntityHandle Handle)
{
	EntityActorMap.Remove(Handle);
}

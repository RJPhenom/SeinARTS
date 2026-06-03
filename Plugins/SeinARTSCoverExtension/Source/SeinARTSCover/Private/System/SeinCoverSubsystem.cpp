/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverSubsystem.cpp
 */

#include "System/SeinCoverSubsystem.h"
#include "System/SeinCoverSystem.h"
#include "System/SeinCoverDefault.h"

#include "Components/SeinCoverComponent.h"
#include "Settings/SeinARTSCoverSettings.h"
#include "Simulation/SeinWorldSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinCoverSubsystem, Log, All);

void USeinCoverSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Resolve the configured class. FSoftClassPath drives the picker — same
	// pattern as NavigationClass / FogOfWarClass / RelayActorClass — so this
	// module doesn't need to be loaded for the settings to resolve from disk,
	// and game teams can swap in a custom impl without touching the framework.
	TSubclassOf<USeinCoverSystem> CoverClass;
	if (const USeinARTSCoverSettings* Settings = GetDefault<USeinARTSCoverSettings>())
	{
		if (Settings->CoverSystemClass.IsValid())
		{
			CoverClass = Settings->CoverSystemClass.TryLoadClass<USeinCoverSystem>();
		}
	}
	if (!CoverClass || CoverClass->HasAnyClassFlags(CLASS_Abstract))
	{
		CoverClass = USeinCoverDefault::StaticClass();
	}

	CoverSystem = NewObject<USeinCoverSystem>(this, CoverClass, NAME_None, RF_Transient);
	if (!CoverSystem)
	{
		UE_LOG(LogSeinCoverSubsystem, Warning,
			TEXT("Initialize: failed to instantiate cover system class %s"),
			*GetNameSafe(CoverClass));
		return;
	}

	USeinWorldSubsystem* WorldSub = GetWorld() ? GetWorld()->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	CoverSystem->OnCoverSystemInitialized(WorldSub);

	// Hook entity spawn/destroy events — auto-registers entities with
	// FSeinCoverComponent in storage as cover providers. Replaces the
	// pre-Phase-5 USeinCoverProviderComponent AC's OnEntitySpawnedNative
	// hook (the AC is gone; events are how render-side systems learn about
	// sim-side component changes now).
	HookSimWorldEvents();

	UE_LOG(LogSeinCoverSubsystem, Log,
		TEXT("USeinCoverSubsystem initialized — active cover system: %s"),
		*GetNameSafe(CoverSystem));
}

void USeinCoverSubsystem::Deinitialize()
{
	if (CachedSimWorld)
	{
		if (SpawnedHandle.IsValid())   CachedSimWorld->OnEntitySpawned.Remove(SpawnedHandle);
		if (DestroyedHandle.IsValid()) CachedSimWorld->OnEntityDestroyed.Remove(DestroyedHandle);
		SpawnedHandle.Reset();
		DestroyedHandle.Reset();
		CachedSimWorld = nullptr;
	}

	if (CoverSystem)
	{
		CoverSystem->OnCoverSystemDeinitialized();
		CoverSystem = nullptr;
	}
	Super::Deinitialize();
}

void USeinCoverSubsystem::HookSimWorldEvents()
{
	UWorld* World = GetWorld();
	if (!World) return;
	USeinWorldSubsystem* WorldSub = World->GetSubsystem<USeinWorldSubsystem>();
	if (!WorldSub)
	{
		// Sim subsystem may not be up yet at our Initialize. Retry on the
		// next world begin-play tick.
		UE_LOG(LogSeinCoverSubsystem, Verbose,
			TEXT("HookSimWorldEvents: sim subsystem not ready; will rebind in OnWorldBeginPlay"));
		return;
	}

	CachedSimWorld = WorldSub;
	SpawnedHandle = WorldSub->OnEntitySpawned.AddUObject(
		this, &USeinCoverSubsystem::HandleEntitySpawned);
	DestroyedHandle = WorldSub->OnEntityDestroyed.AddUObject(
		this, &USeinCoverSubsystem::HandleEntityDestroyed);

	// Authoritative-destination resolver: tell the sim's path/movement layer that a
	// cover slot is a valid destination that OVERRULES the coarse nav bake (root
	// CLAUDE.md invariant #6 — the destination is an INPUT, not an opinion nav may
	// relocate). No FoW gating (invalid observer) — a cover slot is a valid standing
	// spot regardless of who can see it. Tiny radius = "is this exact position a
	// registered slot?" (the cover snap dispatches the exact slot world position).
	WorldSub->AuthoritativeDestinationResolver.BindWeakLambda(this,
		[this](const FFixedVector& WorldPos) -> bool
		{
			if (!CoverSystem) return false;
			const FFixedPoint Eps = FFixedPoint::FromInt(10); // 10cm — exact-ish match
			return CoverSystem->FindNearbySlots(WorldPos, Eps, FSeinPlayerID()).Num() > 0;
		});

	UE_LOG(LogSeinCoverSubsystem, Log,
		TEXT("HookSimWorldEvents: subscribed to OnEntitySpawned + OnEntityDestroyed"));
}

void USeinCoverSubsystem::HandleEntitySpawned(FSeinEntityHandle Handle)
{
	if (!CoverSystem || !CachedSimWorld) return;
	// Only register entities that actually have a cover component in storage —
	// the bridge's InjectAuthoredComponents puts it there if the designer
	// authored an FSeinCoverComponent entry on the ComponentData array.
	if (CachedSimWorld->GetComponent<FSeinCoverComponent>(Handle) != nullptr)
	{
		CoverSystem->RegisterProvider(Handle);
		UE_LOG(LogSeinCoverSubsystem, Verbose,
			TEXT("HandleEntitySpawned: registered cover provider %s"), *Handle.ToString());
	}
}

void USeinCoverSubsystem::HandleEntityDestroyed(FSeinEntityHandle Handle)
{
	// Always call Unregister — the cover system's impl is idempotent
	// (Default impl's Find returns INDEX_NONE for unregistered handles and
	// no-ops). Lets us skip the per-entity "is this a provider?" check on
	// the hot destroy path.
	if (CoverSystem)
	{
		CoverSystem->UnregisterProvider(Handle);
	}
}

USeinCoverSystem* USeinCoverSubsystem::GetCoverSystemForWorld(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull)
		: nullptr;
	if (!World) return nullptr;
	const USeinCoverSubsystem* Sub = World->GetSubsystem<USeinCoverSubsystem>();
	return Sub ? Sub->GetCoverSystem() : nullptr;
}

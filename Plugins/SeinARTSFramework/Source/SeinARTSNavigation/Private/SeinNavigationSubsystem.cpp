/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationSubsystem.cpp
 */

#include "SeinNavigationSubsystem.h"
#include "SeinNavigation.h"
#include "SeinNavigationAsset.h"
#include "SeinNavigationAStar.h"
#include "Volumes/SeinNavVolume.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/Systems/SeinNavBlockerStampSystem.h"
#include "SeinLevelData.h"
#include "SeinLevelDataSubsystem.h"
#include "SeinLevelLayerProvider.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"

#include "SeinARTSNavigationLog.h"

void USeinNavigationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Resolve the configured nav class. Fall back to the shipped A* default
	// if the setting is empty or points to a stale class.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	UClass* NavClass = nullptr;
	if (Settings && Settings->NavigationClass.IsValid())
	{
		NavClass = Settings->NavigationClass.TryLoadClass<USeinNavigation>();
	}
	if (!NavClass || NavClass->HasAnyClassFlags(CLASS_Abstract))
	{
		NavClass = USeinNavigationAStar::StaticClass();
	}

	Navigation = NewObject<USeinNavigation>(this, NavClass, TEXT("SeinNavigation"));
	if (Navigation)
	{
		Navigation->OnNavigationInitialized(GetWorld());
	}
	else
	{
		UE_LOG(LogSeinNavSubsystem, Error, TEXT("Failed to instantiate nav class %s"),
			NavClass ? *NavClass->GetName() : TEXT("<null>"));
	}

	// CP1.1 unified level-data pipeline. Force the substrate subsystem up first
	// (InitializeDependency → it exists in editor + PIE for the bake button path),
	// then — if this nav participates (returns a provider face) — register it as the
	// "Nav" layer provider and subscribe to rebake/reload so the runtime grid tracks
	// the shared bake. Non-participating navs (the default base) skip all of this.
	Collection.InitializeDependency(USeinLevelDataSubsystem::StaticClass());
	if (Navigation)
	{
		if (ISeinLevelLayerProvider* Provider = Navigation->GetLevelDataProvider())
		{
			if (USeinLevelData* Substrate = USeinLevelDataSubsystem::GetLevelDataForWorld(GetWorld()))
			{
				LevelData = Substrate;
				Substrate->RegisterLayerProvider(Provider);
				LevelDataMutatedHandle = Substrate->OnLevelDataMutated.AddUObject(
					this, &USeinNavigationSubsystem::OnLevelDataChanged);
			}
		}
	}
}

void USeinNavigationSubsystem::Deinitialize()
{
	// Unhook from the shared substrate (CP1.1) before Navigation is torn down — we
	// reference Navigation->GetLevelDataProvider() to unregister.
	if (USeinLevelData* Substrate = LevelData.Get())
	{
		if (LevelDataMutatedHandle.IsValid())
		{
			Substrate->OnLevelDataMutated.Remove(LevelDataMutatedHandle);
			LevelDataMutatedHandle.Reset();
		}
		if (Navigation)
		{
			if (ISeinLevelLayerProvider* Provider = Navigation->GetLevelDataProvider())
			{
				Substrate->UnregisterLayerProvider(Provider);
			}
		}
	}
	LevelData = nullptr;

	// Tear down the stamping system before nulling Navigation — the system
	// holds a weak nav ref but would still leak its own memory if dropped
	// without unregister-from-world.
	if (NavBlockerStampSystem)
	{
		if (UWorld* World = GetWorld())
		{
			if (USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>())
			{
				Sim->UnregisterSystem(NavBlockerStampSystem);
			}
		}
		delete NavBlockerStampSystem;
		NavBlockerStampSystem = nullptr;
	}

	if (Navigation)
	{
		Navigation->OnNavigationDeinitialized();
	}
	Navigation = nullptr;
	Super::Deinitialize();
}

void USeinNavigationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	LoadBakedAssetIntoNav(InWorld);
	BindSimDelegates(InWorld);
}

void USeinNavigationSubsystem::LoadBakedAssetIntoNav(UWorld& World)
{
	if (!Navigation) return;

	// Unified pipeline first (CP1.1): if the shared substrate carries a baked grid
	// + a "Nav" channel, adopt it and we're done (substrate-driven path). If the
	// substrate isn't loaded yet (subsystem begin-play order isn't guaranteed), our
	// OnLevelDataMutated subscription re-adopts it the moment it loads.
	if (USeinLevelData* Substrate = LevelData.Get())
	{
		if (Substrate->HasRuntimeData() && Navigation->LoadFromSubstrate(*Substrate))
		{
			UE_LOG(LogSeinNavSubsystem, Log,
				TEXT("Nav: loaded grid from the unified level-data substrate (CP1.1 substrate path)."));
			return;
		}
	}

	// Legacy / A-B baseline path: load nav's own baked asset from a NavVolume.
	for (TActorIterator<ASeinNavVolume> It(&World); It; ++It)
	{
		if (USeinNavigationAsset* Asset = It->BakedAsset.LoadSynchronous())
		{
			Navigation->LoadFromAsset(Asset);
			UE_LOG(LogSeinNavSubsystem, Log,
				TEXT("Nav: loaded grid from legacy NavVolume asset '%s' (substrate carried no nav data)."),
				*Asset->GetName());
			return;
		}
	}
	UE_LOG(LogSeinNavSubsystem, Log,
		TEXT("Nav: no baked data — substrate empty AND no NavVolume asset. FindPath returns no-path."));
}

void USeinNavigationSubsystem::OnLevelDataChanged()
{
	// Shared substrate rebaked / swapped — re-adopt its grid if it now carries nav
	// data. If not (empty bake / no nav channel), LoadFromSubstrate returns false and
	// leaves nav as-is, so a prior legacy load (or the next one) still stands.
	if (!Navigation) return;
	if (USeinLevelData* Substrate = LevelData.Get())
	{
		if (Navigation->LoadFromSubstrate(*Substrate))
		{
			UE_LOG(LogSeinNavSubsystem, Log,
				TEXT("Nav: re-adopted the unified level-data substrate (OnLevelDataMutated)."));
		}
	}
}

void USeinNavigationSubsystem::BindSimDelegates(UWorld& World)
{
	USeinWorldSubsystem* Sim = World.GetSubsystem<USeinWorldSubsystem>();
	if (!Sim || !Navigation) return;

	TWeakObjectPtr<USeinNavigation> NavWeak = Navigation;

	Sim->PathableTargetResolver.BindWeakLambda(this,
		[NavWeak](const FFixedVector& FromWorld, const FFixedVector& ToWorld, const FGameplayTagContainer& AgentTags) -> bool
		{
			USeinNavigation* Nav = NavWeak.Get();
			if (!Nav || !Nav->HasRuntimeData()) return true; // no data = permit (tests, nav-less games)
			return Nav->IsReachable(FromWorld, ToWorld, AgentTags);
		});

	// Footprint placement resolver — gates abilities that have bRequiresFreeFootprint
	// set (typically building-placement abilities). Same permit-on-no-data
	// fallback as the pathable resolver so tests / nav-less games don't block
	// placement spuriously. The nav's IsPlacementValid does the actual cell
	// check (default impl is a center-only sample; subclasses rasterize).
	Sim->FootprintPlacementResolver.BindWeakLambda(this,
		[NavWeak](const FFixedVector& CenterWorld, const FFixedPoint& YawDegrees,
			const FSeinExtentsShape& Shape, uint8 AgentLayerMask) -> bool
		{
			USeinNavigation* Nav = NavWeak.Get();
			if (!Nav || !Nav->HasRuntimeData()) return true;
			return Nav->IsPlacementValid(CenterWorld, YawDegrees, Shape, AgentLayerMask);
		});

	// Per-cell passability resolver — used by hot-path systems (penetration
	// resolution, etc.) to gate proposed positions against nav blockers.
	// Same permit-on-no-data fallback so tests / nav-less games don't gate
	// behavior unexpectedly.
	Sim->PassableResolver.BindWeakLambda(this,
		[NavWeak](const FFixedVector& WorldPos) -> bool
		{
			USeinNavigation* Nav = NavWeak.Get();
			if (!Nav || !Nav->HasRuntimeData()) return true;
			return Nav->IsPassable(WorldPos);
		});

	// Dynamic-aware per-cell passability — like PassableResolver, but ALSO rejects
	// cells under runtime dynamic blockers (bBlocksNav). Cover slot selection uses
	// this so units aren't dispatched onto a cell a dynamically-blocking wall sits
	// on (PassableResolver, static-only, misses those entirely). Default ground
	// agent layer (0x01). Same permit-on-no-data fallback.
	Sim->DynamicPassableResolver.BindWeakLambda(this,
		[NavWeak](const FFixedVector& WorldPos) -> bool
		{
			USeinNavigation* Nav = NavWeak.Get();
			if (!Nav || !Nav->HasRuntimeData()) return true;
			return Nav->IsWorldPositionClear(WorldPos, /*AgentNavLayerMask=*/ 0x01);
		});

	// Ground-height resolver — used by penetration resolution's step-height
	// gate. Walkable-only ON so impassable cells don't produce phantom height
	// reads. No-data fallback = false (height unknown → callers skip check).
	Sim->HeightResolver.BindWeakLambda(this,
		[NavWeak](const FFixedVector& WorldPos, FFixedPoint& OutZ) -> bool
		{
			USeinNavigation* Nav = NavWeak.Get();
			if (!Nav || !Nav->HasRuntimeData()) return false;
			return Nav->GetCellHeightAt(WorldPos, OutZ, /*bWalkableOnly=*/ true);
		});

	// Nav projection resolver — used by formation resolvers to snap slot
	// positions onto walkable terrain. No-op (identity + true) when no nav
	// data is loaded; otherwise routes through Nav->ProjectPointToNavOnElevation
	// so projection is Z-biased (prefers cells whose stored height matches
	// the input's Z). Without the elevation bias, slots that fan over the
	// edge of a raised platform would project to the floor below — producing
	// "stragglers running off cliffsides" when a click on a platform top
	// places some destinations off the platform.
	//
	// No early-out for "already-passable input" — `IsPassable` checks only
	// the cell's XY (cells are XY-indexed), so a slot landing on a floor-
	// level cell IS reported passable even when the click target is on a
	// platform overhead. Always routing through `ProjectPointToNavOnElevation`
	// lets the impl's quick-check (cell-at-input-XY-on-matching-elevation)
	// short-circuit when appropriate, and ring-scans for an elevation-
	// matching cell otherwise.
	Sim->NavProjectResolver.BindWeakLambda(this,
		[NavWeak](const FFixedVector& InWorld, FFixedVector& OutPassable) -> bool
		{
			USeinNavigation* Nav = NavWeak.Get();
			if (!Nav || !Nav->HasRuntimeData())
			{
				OutPassable = InWorld;
				return true;
			}
			return Nav->ProjectPointToNavOnElevation(InWorld, OutPassable);
		});

	// Hand the dynamic-blocker stamping over to the sim's tick loop. PreTick
	// priority 7 → after spatial-hash rebuild, before AbilityExecution where
	// MoveToAction's TickAction calls FindPath. Re-register-safe: unregister
	// any prior instance first so a level reload doesn't leak.
	if (NavBlockerStampSystem)
	{
		Sim->UnregisterSystem(NavBlockerStampSystem);
		delete NavBlockerStampSystem;
		NavBlockerStampSystem = nullptr;
	}
	NavBlockerStampSystem = new FSeinNavBlockerStampSystem(Navigation);
	Sim->RegisterSystem(NavBlockerStampSystem);

	// Reset the path budget tracker. Self-checking reset in RequestPath
	// handles the per-tick boundary going forward; we just zero state here
	// at world-begin so the first tick after world load starts clean
	// regardless of any leftover counter from a prior PIE session.
	PathRequestsThisTick = 0;
	LastResetTick = -1;
}

ESeinPathResult USeinNavigationSubsystem::RequestPath(const FSeinPathRequest& Request, FSeinPath& OutPath)
{
	if (!Navigation)
	{
		OutPath.Clear();
		return ESeinPathResult::NoNavigation;
	}

	// Self-checking per-tick reset. Read the sim tick from the world
	// subsystem; if it advanced since our last reset, clear the budget.
	// Bulletproof against subscription-failure scenarios (live coding,
	// world reload edge cases) that the previous OnSimTickCompleted
	// binding could miss. Cost is one TMap<UClass*, T> lookup +
	// int compare per call — negligible.
	int32 CurrentSimTick = -1;
	if (UWorld* World = GetWorld())
	{
		if (USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>())
		{
			CurrentSimTick = Sim->GetCurrentTick();
		}
	}
	if (CurrentSimTick != LastResetTick)
	{
#if !UE_BUILD_SHIPPING
		// First-reset confirmation logs at Log level so designers can verify
		// the budget tracker is wired without enabling Verbose. Per-tick
		// resets log at Verbose only — flip with `Log LogSeinNavSubsystem
		// Verbose` when investigating budget pressure.
		if (LastResetTick == -1)
		{
			UE_LOG(LogSeinNavSubsystem, Log,
				TEXT("Path budget tracker initialized — first reset at tick %d (used %d/%d in pre-init)"),
				CurrentSimTick, PathRequestsThisTick,
				GetDefault<USeinARTSCoreSettings>() ? GetDefault<USeinARTSCoreSettings>()->PathRequestsPerTickBudget : 32);
		}
		else if (PathRequestsThisTick > 0)
		{
			UE_LOG(LogSeinNavSubsystem, Verbose,
				TEXT("Path budget reset (tick %d → %d, used %d last tick)"),
				LastResetTick, CurrentSimTick, PathRequestsThisTick);
		}
#endif
		PathRequestsThisTick = 0;
		LastResetTick = CurrentSimTick;
	}

	// Read budget per-call rather than caching — settings can change at
	// dev-time and should take effect on the next request without restart.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const int32 Budget = Settings ? Settings->PathRequestsPerTickBudget : 32;

	if (PathRequestsThisTick >= Budget)
	{
		OutPath.Clear();
#if !UE_BUILD_SHIPPING
		// Verbose by default — only loud when investigating budget pressure.
		UE_LOG(LogSeinNavSubsystem, Verbose,
			TEXT("RequestPath throttled at tick %d (this tick: %d / budget: %d, requester %s)"),
			CurrentSimTick, PathRequestsThisTick, Budget,
			*Request.Requester.ToString());
#endif
		return ESeinPathResult::Throttled;
	}

	// Budget consumed regardless of FindPath success — both Found and NotFound
	// represent "A* ran this tick." Throttled is the only outcome that doesn't
	// consume budget (it short-circuits before A*).
	++PathRequestsThisTick;

	if (Navigation->FindPath(Request, OutPath) && OutPath.bIsValid)
	{
		return ESeinPathResult::Found;
	}
	OutPath.Clear();
	return ESeinPathResult::NotFound;
}

USeinNavigation* USeinNavigationSubsystem::GetNavigationForWorld(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	if (UWorld* World = WorldContextObject->GetWorld())
	{
		if (USeinNavigationSubsystem* Sub = World->GetSubsystem<USeinNavigationSubsystem>())
		{
			return Sub->Navigation;
		}
	}
	return nullptr;
}

bool USeinNavigationSubsystem::BeginBake(UWorld* World)
{
	if (!World) return false;
	USeinNavigationSubsystem* Sub = World->GetSubsystem<USeinNavigationSubsystem>();
	if (!Sub || !Sub->Navigation) return false;
	return Sub->Navigation->BeginBake(World);
}

bool USeinNavigationSubsystem::IsBaking(UWorld* World)
{
	if (!World) return false;
	USeinNavigationSubsystem* Sub = World->GetSubsystem<USeinNavigationSubsystem>();
	if (!Sub || !Sub->Navigation) return false;
	return Sub->Navigation->IsBaking();
}

void USeinNavigationSubsystem::RequestCancelBake(UWorld* World)
{
	if (!World) return;
	USeinNavigationSubsystem* Sub = World->GetSubsystem<USeinNavigationSubsystem>();
	if (Sub && Sub->Navigation) Sub->Navigation->RequestCancelBake();
}

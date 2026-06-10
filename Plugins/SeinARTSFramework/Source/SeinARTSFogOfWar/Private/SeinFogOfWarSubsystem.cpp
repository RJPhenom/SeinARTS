/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarSubsystem.cpp
 */

#include "SeinFogOfWarSubsystem.h"
#include "SeinFogOfWar.h"
#include "SeinARTSFogOfWarLog.h"
#include "Default/SeinFogOfWarDefault.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "SeinLevelData.h"
#include "SeinLevelDataSubsystem.h"
#include "SeinLevelLayerProvider.h"

#include "Engine/World.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"

// LogSeinFogOfWarSubsystem is module-declared (SeinARTSFogOfWarLog.h) so it is
// reliably filterable in the Output Log — do not re-introduce a _STATIC define here.

void USeinFogOfWarSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Resolve the configured fog class. Fall back to the shipped default if
	// the setting is empty or points to a stale / abstract class.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	UClass* FogClass = nullptr;
	if (Settings && Settings->FogOfWarClass.IsValid())
	{
		FogClass = Settings->FogOfWarClass.TryLoadClass<USeinFogOfWar>();
	}
	if (!FogClass || FogClass->HasAnyClassFlags(CLASS_Abstract))
	{
		FogClass = USeinFogOfWarDefault::StaticClass();
	}

	FogOfWar = NewObject<USeinFogOfWar>(this, FogClass, TEXT("SeinFogOfWar"));
	if (FogOfWar)
	{
		FogOfWar->OnFogOfWarInitialized(GetWorld());
	}
	else
	{
		UE_LOG(LogSeinFogOfWarSubsystem, Error, TEXT("Failed to instantiate fog class %s"),
			FogClass ? *FogClass->GetName() : TEXT("<null>"));
	}

	// CP1.1 unified level-data pipeline. Force the substrate subsystem up first
	// (InitializeDependency → it exists in editor + PIE for the bake-button path),
	// then — if this fog participates (returns a provider face) — register it as
	// the "FogOfWar" layer provider and subscribe to rebake/reload so the runtime
	// grid tracks the shared bake. Non-participating fogs (the base) skip all of this.
	Collection.InitializeDependency(USeinLevelDataSubsystem::StaticClass());
	if (FogOfWar)
	{
		if (ISeinLevelLayerProvider* Provider = FogOfWar->GetLevelDataProvider())
		{
			if (USeinLevelData* Substrate = USeinLevelDataSubsystem::GetLevelDataForWorld(GetWorld()))
			{
				LevelData = Substrate;
				Substrate->RegisterLayerProvider(Provider);
				LevelDataMutatedHandle = Substrate->OnLevelDataMutated.AddUObject(
					this, &USeinFogOfWarSubsystem::OnLevelDataChanged);
			}
		}
	}
}

void USeinFogOfWarSubsystem::Deinitialize()
{
	// Unhook from the shared substrate (CP1.1) before FogOfWar is torn down — we
	// reference FogOfWar->GetLevelDataProvider() to unregister.
	if (USeinLevelData* Substrate = LevelData.Get())
	{
		if (LevelDataMutatedHandle.IsValid())
		{
			Substrate->OnLevelDataMutated.Remove(LevelDataMutatedHandle);
			LevelDataMutatedHandle.Reset();
		}
		if (FogOfWar)
		{
			if (ISeinLevelLayerProvider* Provider = FogOfWar->GetLevelDataProvider())
			{
				Substrate->UnregisterLayerProvider(Provider);
			}
		}
	}
	LevelData = nullptr;

	if (SimTickHandle.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			if (USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>())
			{
				Sim->OnSimTickCompleted.Remove(SimTickHandle);
			}
		}
		SimTickHandle.Reset();
	}
	if (FogOfWar)
	{
		FogOfWar->OnFogOfWarDeinitialized();
	}
	FogOfWar = nullptr;
	Super::Deinitialize();
}

void USeinFogOfWarSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	LoadBakedAssetIntoFogOfWar(InWorld);
	InitGridIfUnbaked(InWorld);
	BindSimDelegates(InWorld);
	BindStampTick(InWorld);
}

void USeinFogOfWarSubsystem::LoadBakedAssetIntoFogOfWar(UWorld& World)
{
	if (!FogOfWar) return;

	// Unified pipeline (CP1.1): if the shared substrate carries a baked grid
	// + a "FogOfWar" channel, adopt it and we're done. If the substrate isn't
	// loaded yet (subsystem begin-play order isn't guaranteed), our
	// OnLevelDataMutated subscription re-adopts it the moment it loads.
	if (USeinLevelData* Substrate = LevelData.Get())
	{
		if (Substrate->HasRuntimeData() && FogOfWar->LoadFromSubstrate(*Substrate))
		{
			UE_LOG(LogSeinFogOfWarSubsystem, Log,
				TEXT("FoW: loaded grid from the unified level-data substrate (CP1.1 substrate path)."));
			return;
		}
	}

	UE_LOG(LogSeinFogOfWarSubsystem, Log,
		TEXT("FoW: no baked level data — grid auto-init may follow."));
}

void USeinFogOfWarSubsystem::OnLevelDataChanged()
{
	// Shared substrate rebaked / swapped — re-adopt its fog channel if present. If
	// not (empty bake / no fog channel), LoadFromSubstrate returns false and leaves
	// fog as-is, so any previously-adopted (or auto-initialized) grid still stands.
	if (!FogOfWar) return;
	if (USeinLevelData* Substrate = LevelData.Get())
	{
		if (FogOfWar->LoadFromSubstrate(*Substrate))
		{
			UE_LOG(LogSeinFogOfWarSubsystem, Log,
				TEXT("FoW: re-adopted the unified level-data substrate (OnLevelDataMutated)."));
		}
	}
}

void USeinFogOfWarSubsystem::InitGridIfUnbaked(UWorld& World)
{
	if (!FogOfWar) return;
	if (FogOfWar->HasRuntimeData()) return; // bake already loaded — skip auto-init
	FogOfWar->InitGridFromVolumes(&World);
}

void USeinFogOfWarSubsystem::BindStampTick(UWorld& World)
{
	if (!FogOfWar) return;
	USeinWorldSubsystem* Sim = World.GetSubsystem<USeinWorldSubsystem>();
	if (!Sim) return;
	if (SimTickHandle.IsValid())
	{
		Sim->OnSimTickCompleted.Remove(SimTickHandle);
		SimTickHandle.Reset();
	}
	SimTickHandle = Sim->OnSimTickCompleted.AddUObject(this, &USeinFogOfWarSubsystem::HandleSimTickCompleted);
}

void USeinFogOfWarSubsystem::HandleSimTickCompleted(int32 CurrentTick)
{
	if (!FogOfWar) return;

	// VisionTickInterval = N → recompute every Nth sim tick (e.g. 3 @ 30Hz
	// sim = 10Hz stamps). All clients hit the same tick boundary, stamp the
	// same source snapshot, produce the same bits — no wall-clock drift.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const int32 Interval = (Settings && Settings->VisionTickInterval > 0) ? Settings->VisionTickInterval : 1;
	if ((CurrentTick % Interval) != 0) return;

	FogOfWar->TickStamps(GetWorld());
}

void USeinFogOfWarSubsystem::BindSimDelegates(UWorld& World)
{
	USeinWorldSubsystem* Sim = World.GetSubsystem<USeinWorldSubsystem>();
	if (!Sim || !FogOfWar) return;

	TWeakObjectPtr<USeinFogOfWar> FogWeak = FogOfWar;

	// Note: TargetWorld is FFixedVector end-to-end — sim callers pass fixed
	// point directly; no FVector round-trip at the boundary.
	Sim->LineOfSightResolver.BindWeakLambda(this,
		[FogWeak](FSeinPlayerID ObserverPlayer, const FFixedVector& TargetWorld) -> bool
		{
			USeinFogOfWar* Fog = FogWeak.Get();
			if (!Fog || !Fog->HasRuntimeData()) return true; // no data = permit (tests, fog-less games)
			return Fog->IsCellVisible(ObserverPlayer, TargetWorld, SEIN_FOW_BIT_NORMAL);
		});
}

USeinFogOfWar* USeinFogOfWarSubsystem::GetFogOfWarForWorld(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	if (UWorld* World = WorldContextObject->GetWorld())
	{
		if (USeinFogOfWarSubsystem* Sub = World->GetSubsystem<USeinFogOfWarSubsystem>())
		{
			return Sub->FogOfWar;
		}
	}
	return nullptr;
}

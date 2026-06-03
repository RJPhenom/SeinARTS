/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarSubsystem.cpp
 */

#include "SeinFogOfWarSubsystem.h"
#include "SeinFogOfWar.h"
#include "SeinFogOfWarAsset.h"
#include "Default/SeinFogOfWarDefault.h"
#include "Volumes/SeinFogOfWarVolume.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinFogOfWarSubsystem, Log, All);

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
}

void USeinFogOfWarSubsystem::Deinitialize()
{
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

	for (TActorIterator<ASeinFogOfWarVolume> It(&World); It; ++It)
	{
		if (USeinFogOfWarAsset* Asset = It->BakedAsset)
		{
			FogOfWar->LoadFromAsset(Asset);
			return;
		}
	}
	// No baked asset — fog stays empty; queries return no-visibility.
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

bool USeinFogOfWarSubsystem::BeginBake(UWorld* World)
{
	if (!World) return false;
	USeinFogOfWarSubsystem* Sub = World->GetSubsystem<USeinFogOfWarSubsystem>();
	if (!Sub || !Sub->FogOfWar) return false;
	return Sub->FogOfWar->BeginBake(World);
}

bool USeinFogOfWarSubsystem::IsBaking(UWorld* World)
{
	if (!World) return false;
	USeinFogOfWarSubsystem* Sub = World->GetSubsystem<USeinFogOfWarSubsystem>();
	if (!Sub || !Sub->FogOfWar) return false;
	return Sub->FogOfWar->IsBaking();
}

void USeinFogOfWarSubsystem::RequestCancelBake(UWorld* World)
{
	if (!World) return;
	USeinFogOfWarSubsystem* Sub = World->GetSubsystem<USeinFogOfWarSubsystem>();
	if (Sub && Sub->FogOfWar) Sub->FogOfWar->RequestCancelBake();
}

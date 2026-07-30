/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLevelDataSubsystem.cpp
 */

#include "SeinLevelDataSubsystem.h"
#include "SeinLevelData.h"
#include "SeinLevelDataDefault.h"
#include "SeinLevelDataAsset.h"
#include "Volumes/SeinLevelVolume.h"
#include "Settings/PluginSettings.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "SeinARTSLevelDataLog.h"

void USeinLevelDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bInitialRuntimeDataPrepared = false;

	// Resolve the configured level-data class; fall back to the shipped default
	// if the setting is empty or points to a stale / abstract class.
	// WYSIWYG. None/empty => the level substrate is intentionally OFF (LevelData stays null; the Bake
	// Level Data button does nothing and nav / baked fog occluders / minimap get no data). A
	// set-but-unloadable/abstract class is a mistake, not an off-switch: it falls back to the shipped
	// default with a logged error. Every consumer (and BeginBake) already null-guards LevelData.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	UClass* LevelDataClass = nullptr;
	if (Settings && !Settings->LevelDataClass.IsNull())
	{
		LevelDataClass = Settings->LevelDataClass.TryLoadClass<USeinLevelData>();
		if (!LevelDataClass || LevelDataClass->HasAnyClassFlags(CLASS_Abstract))
		{
			UE_LOG(LogSeinLevelDataSubsystem, Error,
				TEXT("LevelDataClass '%s' could not be loaded or is abstract — falling back to the shipped default."),
				*Settings->LevelDataClass.ToString());
			LevelDataClass = USeinLevelDataDefault::StaticClass();
		}
	}

	if (LevelDataClass)
	{
		LevelData = NewObject<USeinLevelData>(this, LevelDataClass, TEXT("SeinLevelData"));
		if (LevelData)
		{
			LevelData->InitializeForWorld(GetWorld());
		}
		else
		{
			UE_LOG(LogSeinLevelDataSubsystem, Error, TEXT("Failed to instantiate level-data class %s"),
				*LevelDataClass->GetName());
		}
	}
	else
	{
		USeinARTSCoreSettings::ReportDisabledSystem(TEXT("Level Data"),
			TEXT("The level bake is disabled; navigation, baked fog occluders, and the minimap have no data."), /*bHighSeverity*/ true);
	}
}

void USeinLevelDataSubsystem::Deinitialize()
{
	ReleaseModuleOwnedStateForModuleUnload();
	Super::Deinitialize();
}

void USeinLevelDataSubsystem::ReleaseModuleOwnedStateForModuleUnload()
{
	check(IsInGameThread());
	OnInitialLevelDataPrepared.Clear();
	if (LevelData)
	{
		LevelData->RequestCancelBake();
		LevelData->DeinitializeFromWorld();
	}
	LevelData = nullptr;
	bInitialRuntimeDataPrepared = false;
}

bool USeinLevelDataSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Game + PIE for runtime; Editor so the "Bake Level Data" button works pre-PIE.
	return WorldType == EWorldType::Game
		|| WorldType == EWorldType::PIE
		|| WorldType == EWorldType::Editor;
}

void USeinLevelDataSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	EnsureInitialRuntimeDataPrepared(InWorld);
}

bool USeinLevelDataSubsystem::EnsureInitialRuntimeDataPrepared(UWorld& World)
{
	check(IsInGameThread());
	if (bInitialRuntimeDataPrepared)
	{
		return true;
	}
	if (&World != GetWorld())
	{
		UE_LOG(LogSeinLevelDataSubsystem, Error,
			TEXT("Initial level-data preparation targeted a different world."));
		return false;
	}

	if (!LoadBakedAsset(World))
	{
		return false;
	}
	bInitialRuntimeDataPrepared = true;
	OnInitialLevelDataPrepared.Broadcast();
	return true;
}

bool USeinLevelDataSubsystem::LoadBakedAsset(UWorld& World)
{
	if (!LevelData)
	{
		return true;
	}

	for (TActorIterator<ASeinLevelVolume> It(&World); It; ++It)
	{
		if (It->BakedAsset.IsNull())
		{
			continue;
		}
		USeinLevelDataAsset* Asset = It->BakedAsset.LoadSynchronous();
		if (!Asset)
		{
			UE_LOG(LogSeinLevelDataSubsystem, Error,
				TEXT("Initial baked level-data asset '%s' could not be loaded."),
				*It->BakedAsset.ToString());
			return false;
		}
		if (!LevelData->LoadFromAsset(Asset))
		{
			UE_LOG(LogSeinLevelDataSubsystem, Error,
				TEXT("Configured level-data implementation rejected initial asset '%s'."),
				*Asset->GetPathName());
			return false;
		}
		return true;
	}
	// No baked asset — substrate stays empty (HasRuntimeData() false).
	return true;
}

USeinLevelData* USeinLevelDataSubsystem::GetLevelDataForWorld(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	if (UWorld* World = WorldContextObject->GetWorld())
	{
		if (USeinLevelDataSubsystem* Sub = World->GetSubsystem<USeinLevelDataSubsystem>())
		{
			return Sub->LevelData;
		}
	}
	return nullptr;
}

bool USeinLevelDataSubsystem::BeginBake(UWorld* World)
{
	if (!World) return false;
	USeinLevelDataSubsystem* Sub = World->GetSubsystem<USeinLevelDataSubsystem>();
	if (!Sub) return false;
	if (!Sub->LevelData)
	{
		// Level substrate is OFF (LevelDataClass = None). Tell the designer why the bake did nothing
		// instead of silently returning — this is the trap the WYSIWYG warning exists to close.
		USeinARTSCoreSettings::ReportDisabledSystem(TEXT("Level Data"),
			TEXT("Bake Level Data did nothing because the Level Data class is None."), /*bHighSeverity*/ true);
		return false;
	}
	return Sub->LevelData->BeginBake(World);
}

bool USeinLevelDataSubsystem::IsBaking(UWorld* World)
{
	if (!World) return false;
	USeinLevelDataSubsystem* Sub = World->GetSubsystem<USeinLevelDataSubsystem>();
	return Sub && Sub->LevelData && Sub->LevelData->IsBaking();
}

void USeinLevelDataSubsystem::RequestCancelBake(UWorld* World)
{
	if (!World) return;
	USeinLevelDataSubsystem* Sub = World->GetSubsystem<USeinLevelDataSubsystem>();
	if (Sub && Sub->LevelData) Sub->LevelData->RequestCancelBake();
}

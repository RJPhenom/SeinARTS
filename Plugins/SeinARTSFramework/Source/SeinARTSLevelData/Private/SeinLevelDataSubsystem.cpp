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
			LevelData->OnInitialized(GetWorld());
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
	if (LevelData)
	{
		LevelData->RequestCancelBake();
		LevelData->OnDeinitialized();
	}
	LevelData = nullptr;
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
	LoadBakedAsset(InWorld);
}

void USeinLevelDataSubsystem::LoadBakedAsset(UWorld& World)
{
	if (!LevelData) return;

	for (TActorIterator<ASeinLevelVolume> It(&World); It; ++It)
	{
		if (USeinLevelDataAsset* Asset = It->BakedAsset.LoadSynchronous())
		{
			LevelData->LoadFromAsset(Asset);
			return;
		}
	}
	// No baked asset — substrate stays empty (HasRuntimeData() false).
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

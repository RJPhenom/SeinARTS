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
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	UClass* LevelDataClass = nullptr;
	if (Settings && Settings->LevelDataClass.IsValid())
	{
		LevelDataClass = Settings->LevelDataClass.TryLoadClass<USeinLevelData>();
	}
	if (!LevelDataClass || LevelDataClass->HasAnyClassFlags(CLASS_Abstract))
	{
		LevelDataClass = USeinLevelDataDefault::StaticClass();
	}

	LevelData = NewObject<USeinLevelData>(this, LevelDataClass, TEXT("SeinLevelData"));
	if (LevelData)
	{
		LevelData->OnInitialized(GetWorld());
	}
	else
	{
		UE_LOG(LogSeinLevelDataSubsystem, Error, TEXT("Failed to instantiate level-data class %s"),
			LevelDataClass ? *LevelDataClass->GetName() : TEXT("<null>"));
	}
}

void USeinLevelDataSubsystem::Deinitialize()
{
	if (LevelData)
	{
		LevelData->OnDeinitialized();
	}
	LevelData = nullptr;
	Super::Deinitialize();
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
	if (!Sub || !Sub->LevelData) return false;
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

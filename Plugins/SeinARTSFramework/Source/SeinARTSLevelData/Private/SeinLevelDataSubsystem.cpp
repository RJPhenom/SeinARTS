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
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Simulation/SeinWorldSubsystem.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "SeinARTSLevelDataLog.h"

namespace
{
	constexpr int32 MaxLevelDataStateContributors = 64;

	void AppendFramed(FString& Out, const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		Out += FString::Printf(TEXT("%d:"), Utf8.Length());
		Out += Value;
		Out += TEXT("\n");
	}

	bool BuildDisabledStaticEnvironmentDigest(
		FGuid& OutDigest,
		FString& OutError)
	{
		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.LevelData.Disabled.StaticEnvironment"), 1);
		return Writer.WriteBool(false)
			&& Writer.Finalize(OutDigest, OutError);
	}
}

void USeinLevelDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bInitialRuntimeDataPrepared = false;
	bStateBindingFrozen = false;
	FrozenStateBindingFrame.Reset();
	FrozenStaticEnvironmentDigest.Invalidate();
	FrozenStaticEnvironmentGeneration = 0;

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
	bStateBindingFrozen = false;
	FrozenStateBindingFrame.Reset();
	FrozenStaticEnvironmentDigest.Invalidate();
	FrozenStaticEnvironmentGeneration = 0;
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

bool USeinLevelDataSubsystem::FreezeCanonicalStateBinding(
	bool bCommit,
	FString& OutFrame,
	FString& OutError)
{
	OutFrame.Reset();
	OutError.Reset();
	UWorld* World = GetWorld();
	const USeinWorldSubsystem* Sim = World
		? World->GetSubsystem<USeinWorldSubsystem>()
		: nullptr;
	if (!World || !Sim)
	{
		OutError =
			TEXT("Level Data StateContract binding requires its owning Core world.");
		return false;
	}

	FString CandidateFrame =
		TEXT("SeinARTS.LevelData.WorldBinding\n");
	AppendFramed(CandidateFrame, TEXT("1"));
	FGuid StaticDigest;
	uint64 CurrentGeneration = 0;
	if (!LevelData)
	{
		if (!BuildDisabledStaticEnvironmentDigest(
			StaticDigest, OutError))
		{
			return false;
		}
		AppendFramed(CandidateFrame, TEXT("disabled"));
		AppendFramed(CandidateFrame, TEXT("<disabled>"));
		AppendFramed(
			CandidateFrame, TEXT("seinarts.level-data.disabled"));
		AppendFramed(CandidateFrame, TEXT("1"));
		AppendFramed(CandidateFrame, TEXT("1"));
		AppendFramed(CandidateFrame, TEXT("stateless"));
		AppendFramed(CandidateFrame, TEXT("0"));
	}
	else
	{
		if (!LevelData->ComputeStaticEnvironmentDigest(
			StaticDigest, OutError)
			|| !StaticDigest.IsValid())
		{
			if (OutError.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Level Data implementation '%s' returned an invalid static-environment digest."),
					*LevelData->GetClass()->GetPathName());
			}
			return false;
		}

		FSeinLevelDataStateCoverageClaim Claim;
		if (!LevelData->ComputeStateCoverageClaim(Claim, OutError)
			|| Claim.StableImplementationId.IsEmpty()
			|| Claim.StableImplementationId
				!= Claim.StableImplementationId.TrimStartAndEnd()
			|| Claim.BehaviorRevision == 0
			|| Claim.CoverageRevision == 0)
		{
			if (OutError.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Level Data implementation '%s' returned an invalid exact-state coverage claim."),
					*LevelData->GetClass()->GetPathName());
			}
			return false;
		}

		FString CoverageKind;
		TArray<FString> CanonicalRequiredKeys;
		switch (Claim.StateCoverage)
		{
		case ESeinLevelDataStateCoverage::Stateless:
			CoverageKind = TEXT("stateless");
			if (!Claim.RequiredCanonicalStateContributors.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Stateless Level Data implementation '%s' names supplemental canonical-state contributors."),
					*LevelData->GetClass()->GetPathName());
				return false;
			}
			break;

		case ESeinLevelDataStateCoverage::CanonicalStateContributors:
			CoverageKind = TEXT("canonical-state-contributors");
			if (Claim.RequiredCanonicalStateContributors.IsEmpty()
				|| Claim.RequiredCanonicalStateContributors.Num()
					> MaxLevelDataStateContributors)
			{
				OutError = FString::Printf(
					TEXT("Stateful Level Data implementation '%s' names an invalid supplemental canonical-state contributor count."),
					*LevelData->GetClass()->GetPathName());
				return false;
			}
			for (const FSeinCanonicalStateKey& Required :
				Claim.RequiredCanonicalStateContributors)
			{
				const FString CanonicalKey =
					FSeinCanonicalStateRegistry::CanonicalKey(Required);
				if (CanonicalKey.IsEmpty()
					|| !Sim->HasFrozenCanonicalStateContributor(
						Required,
						ESeinCanonicalStateRole::Authoritative))
				{
					OutError = FString::Printf(
						TEXT("Level Data implementation '%s' requires missing authoritative canonical-state contributor '%s'."),
						*LevelData->GetClass()->GetPathName(),
						*CanonicalKey);
					return false;
				}
				CanonicalRequiredKeys.Add(CanonicalKey);
			}
			CanonicalRequiredKeys.Sort();
			for (int32 Index = 1;
				Index < CanonicalRequiredKeys.Num(); ++Index)
			{
				if (CanonicalRequiredKeys[Index - 1]
					== CanonicalRequiredKeys[Index])
				{
					OutError = FString::Printf(
						TEXT("Level Data implementation '%s' names duplicate canonical-state contributor '%s'."),
						*LevelData->GetClass()->GetPathName(),
						*CanonicalRequiredKeys[Index]);
					return false;
				}
			}
			break;

		case ESeinLevelDataStateCoverage::Unspecified:
		default:
			OutError = FString::Printf(
				TEXT("Level Data implementation '%s' did not declare whether its mutable state is stateless or restored by canonical contributors."),
				*LevelData->GetClass()->GetPathName());
			return false;
		}

		AppendFramed(CandidateFrame, TEXT("enabled"));
		AppendFramed(
			CandidateFrame, LevelData->GetClass()->GetPathName());
		AppendFramed(CandidateFrame, Claim.StableImplementationId);
		AppendFramed(
			CandidateFrame, LexToString(Claim.BehaviorRevision));
		AppendFramed(
			CandidateFrame, LexToString(Claim.CoverageRevision));
		AppendFramed(CandidateFrame, CoverageKind);
		AppendFramed(
			CandidateFrame,
			LexToString(CanonicalRequiredKeys.Num()));
		for (const FString& RequiredKey : CanonicalRequiredKeys)
		{
			AppendFramed(CandidateFrame, RequiredKey);
		}
		CurrentGeneration =
			LevelData->GetStaticEnvironmentGeneration();
	}
	AppendFramed(
		CandidateFrame,
		StaticDigest.ToString(EGuidFormats::Digits));

	if (bStateBindingFrozen
		&& (FrozenStateBindingFrame != CandidateFrame
			|| FrozenStaticEnvironmentDigest != StaticDigest))
	{
		OutError =
			TEXT("Level Data implementation or static substrate changed after the match StateContract froze.");
		return false;
	}
	if (bStateBindingFrozen
		&& FrozenStaticEnvironmentGeneration != CurrentGeneration)
	{
		OutError =
			TEXT("Level Data static substrate mutated in place after the match StateContract froze.");
		return false;
	}
	if (bCommit)
	{
		bStateBindingFrozen = true;
		FrozenStateBindingFrame = CandidateFrame;
		FrozenStaticEnvironmentDigest = StaticDigest;
		FrozenStaticEnvironmentGeneration = CurrentGeneration;
	}
	OutFrame = MoveTemp(CandidateFrame);
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

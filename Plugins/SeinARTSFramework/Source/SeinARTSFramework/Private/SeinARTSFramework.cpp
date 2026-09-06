// Copyright Epic Games, Inc. All Rights Reserved.

#include "SeinARTSFramework.h"

#include "Debug/SeinCommandLogSubsystem.h"
#include "Debug/SeinSteeringDebugSelection.h"
#include "Player/SeinPlayerController.h"
#include "Actor/SeinActor.h"
#include "GameMode/SeinGameMode.h"
#include "GameMode/SeinMatchBootstrapSubsystem.h"
#include "GameMode/SeinPlayerStart.h"
#include "GameMode/SeinWorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Player/SeinCameraSnapshotSubsystem.h"
#include "Player/SeinTargeterSubsystem.h"
#include "Preview/SeinFormationPreviewSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "FSeinARTSFrameworkModule"

DEFINE_LOG_CATEGORY_STATIC(LogSeinARTSFrameworkModule, Log, All);

namespace
{
#if UE_ENABLE_DEBUG_DRAWING
	FDelegateHandle SteeringSelectionHandle;
#endif
	const FName PIESeamlessTravelOverrideTag(
		TEXT("SeinARTSFramework"));

	FSeinSimulationContentDiscoveryRoot MakePackageDiscoveryRoot(
		const UClass* RootClass)
	{
		check(RootClass);

		FSeinSimulationContentDiscoveryRoot Root;
		Root.RootClassPath = RootClass->GetPathName();
		Root.StableRecordKindId =
			FSeinSimulationContentManifestCodec::GetCurrentRecordKindId();
		Root.RecordRevision =
			FSeinSimulationContentManifestCodec::CurrentRecordRevision;
		return Root;
	}
}

void FSeinARTSFrameworkModule::StartupModule()
{
#if UE_ENABLE_DEBUG_DRAWING
	SteeringSelectionHandle = UE::SeinARTSMovement::SteeringDebugSelectionQuery().AddLambda(
		[](APlayerController* Player, TArray<FSeinEntityHandle>& Out)
		{
			if (const auto* Controller = Cast<ASeinPlayerController>(Player))
				for (const auto& WeakActor : Controller->SelectedActors)
					if (const ASeinActor* Actor = WeakActor.Get()) Out.AddUnique(Actor->GetEntityHandle());
		});
#endif
	SimulationContentRegistrationHandle.Reset();

#if WITH_EDITOR
	// PIE disables seamless travel by default. UE forces non-seamless
	// `ServerTravel` in PIE unless `net.AllowPIESeamlessTravel=1` is set,
	// even when the GameMode opts in via `bUseSeamlessTravel = true`.
	// Non-seamless travel inherits the current GameMode class as a
	// `?game=` URL parameter (lobby's MainMenu GameMode forces itself
	// onto the gameplay map) AND tears the NetDriver down mid-travel
	// (clients get refused on reconnect, OnLogout fires, the lobby
	// nukes their bReady, and only the host actually travels). Forcing
	// the CVar on at module startup makes the editor's PIE behavior
	// match shipped — no per-project ini edits required.
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("net.AllowPIESeamlessTravel")))
	{
		CVar->Set(
			1,
			IConsoleVariable::FSetContext(
				ECVF_SetByGameOverride,
				PIESeamlessTravelOverrideTag));
	}
#endif

	FSeinSimulationContentContributorDescriptor ContentDescriptor;
	ContentDescriptor.StableContributorId = TEXT("seinarts.framework");
	ContentDescriptor.ContributorRevision = 1;
	ContentDescriptor.DiscoveryRoots = {
		MakePackageDiscoveryRoot(ASeinGameMode::StaticClass()),
		MakePackageDiscoveryRoot(ASeinWorldSettings::StaticClass()),
		MakePackageDiscoveryRoot(ASeinPlayerStart::StaticClass()),
	};

	FString ContentRegistrationError;
	SimulationContentRegistrationHandle =
		FSeinSimulationContentRegistry::RegisterContributor(
			ContentDescriptor,
			&ContentRegistrationError);
	if (!SimulationContentRegistrationHandle.IsValid())
	{
		UE_LOG(
			LogSeinARTSFrameworkModule,
			Error,
			TEXT("Simulation-content contributor '%s' failed to register: %s"),
			*ContentDescriptor.StableContributorId,
			*ContentRegistrationError);
	}
}

void FSeinARTSFrameworkModule::PreUnloadCallback()
{
	ReleaseModuleOwnedState();
}

void FSeinARTSFrameworkModule::ShutdownModule()
{
	ReleaseModuleOwnedState();
}

void FSeinARTSFrameworkModule::ReleaseModuleOwnedState()
{
	check(IsInGameThread());
#if UE_ENABLE_DEBUG_DRAWING
	UE::SeinARTSMovement::SteeringDebugSelectionQuery().Remove(SteeringSelectionHandle);
	SteeringSelectionHandle.Reset();
#endif

	// Core owns the deterministic callbacks and payloads that can point into
	// this module. Fail the live topology and drop those roots first.
	for (TObjectIterator<USeinWorldSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->TerminateAndReleaseForModuleUnload(
				TEXT("SeinARTSFramework"),
				TEXT("match bootstrap and gameplay-shell implementations are unloading"));
		}
	}

	for (TObjectIterator<USeinMatchBootstrapSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->ReleaseModuleOwnedStateForModuleUnload();
		}
	}
	for (TObjectIterator<USeinCameraSnapshotSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->ReleaseModuleOwnedStateForModuleUnload();
		}
	}
	for (TObjectIterator<USeinCommandLogSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->ReleaseModuleOwnedStateForModuleUnload();
		}
	}
	for (TObjectIterator<USeinFormationPreviewSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->ReleaseModuleOwnedStateForModuleUnload();
		}
	}
	for (TObjectIterator<USeinTargeterSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->ReleaseModuleOwnedStateForModuleUnload();
		}
	}

#if WITH_EDITOR
	if (IConsoleVariable* CVar =
		IConsoleManager::Get().FindConsoleVariable(
			TEXT("net.AllowPIESeamlessTravel")))
	{
		CVar->Unset(
			ECVF_SetByGameOverride,
			PIESeamlessTravelOverrideTag);
	}
#endif

	SimulationContentRegistrationHandle.Reset();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSeinARTSFrameworkModule, SeinARTSFramework)

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSCoverSquadModule.cpp
 */

#include "SeinARTSCoverSquadModule.h"
#include "SeinCoverAwareSquadDispatchResolver.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinARTSCoverSquad, Log, All);

namespace
{
	const FName GCoverSquadPoolCodecOwner(
		TEXT("seinartscoversquad"));
}

void FSeinARTSCoverSquadModule::StartupModule()
{
	SimulationContentRegistrationHandle.Reset();
	PoolObjectCodecHandle.Reset();

	FSeinPoolObjectCodecDescriptor PoolDescriptor;
	PoolDescriptor.NativeAnchor =
		USeinCoverAwareSquadDispatchResolver::StaticClass();
	PoolDescriptor.Kind =
		ESeinPoolObjectKind::CommandBrokerResolver;
	PoolDescriptor.StableProviderId =
		TEXT("seinarts.coversquad.pool.dispatch-resolver.reflection");
	PoolDescriptor.StateSchemaVersion = 1;
	PoolDescriptor.BehaviorRevision = 1;
	PoolDescriptor.CodecRevision = 2;
	PoolDescriptor.MaxStateBytes =
		FSeinPoolObjectCodecRegistry::MaxStateBytes;
	PoolDescriptor.bAllowBlueprintChildren = true;
	FString PoolCodecError;
	PoolObjectCodecHandle =
		FSeinPoolObjectCodecRegistry::Register(
			GCoverSquadPoolCodecOwner,
			PoolDescriptor,
			FSeinPoolObjectCodecRegistry::MakeReflectedOps(),
			&PoolCodecError);
	if (!PoolObjectCodecHandle.IsValid())
	{
		UE_LOG(LogSeinARTSCoverSquad, Error,
			TEXT("Cover-Squad pool-object codec failed to register: %s"),
			*PoolCodecError);
	}

	FSeinSimulationContentContributorDescriptor ContentDescriptor;
	ContentDescriptor.StableContributorId = TEXT("seinarts.coversquad");
	ContentDescriptor.ContributorRevision = 1;

	FString ContentRegistrationError;
	SimulationContentRegistrationHandle =
		FSeinSimulationContentRegistry::RegisterContributor(
			ContentDescriptor,
			&ContentRegistrationError);
	if (!SimulationContentRegistrationHandle.IsValid())
	{
		UE_LOG(
			LogSeinARTSCoverSquad,
			Error,
			TEXT("Simulation-content contributor '%s' failed to register: %s"),
			*ContentDescriptor.StableContributorId,
			*ContentRegistrationError);
	}

	UE_LOG(LogSeinARTSCoverSquad, Log, TEXT("SeinARTSCoverSquad module started."));
}

void FSeinARTSCoverSquadModule::PreUnloadCallback()
{
	check(IsInGameThread());
	for (TObjectIterator<USeinWorldSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->TerminateAndReleaseForModuleUnload(
				TEXT("SeinARTSCoverSquad"),
				TEXT("the cover-squad dispatch bridge is unloading"));
		}
	}

	SimulationContentRegistrationHandle.Reset();
	PoolObjectCodecHandle.Reset();
}

void FSeinARTSCoverSquadModule::ShutdownModule()
{
	PoolObjectCodecHandle.Reset();
	SimulationContentRegistrationHandle.Reset();
	UE_LOG(LogSeinARTSCoverSquad, Log, TEXT("SeinARTSCoverSquad module shut down."));
}

IMPLEMENT_MODULE(FSeinARTSCoverSquadModule, SeinARTSCoverSquad)

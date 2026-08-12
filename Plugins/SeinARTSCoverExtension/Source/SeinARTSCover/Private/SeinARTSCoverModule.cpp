/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSCoverModule.cpp
 */

#include "SeinARTSCoverModule.h"
#include "Serialization/SeinCoverCanonicalStateProvider.h"
#include "Settings/SeinARTSCoverSettings.h"
#include "Settings/SeinConfigFingerprintRegistry.h"
#include "Resolvers/SeinCoverAwareDefaultBrokerResolver.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "System/SeinCoverSubsystem.h"
#include "System/SeinCoverSystem.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinARTSCover, Log, All);

namespace
{
	// Frozen cross-client identifier: renaming it changes lockstep compatibility.
	const FName GCoverFingerprintId(TEXT("CoverExtension"));
	const FName GCoverPoolCodecOwner(TEXT("seinartscover"));

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

void FSeinARTSCoverModule::StartupModule()
{
	ConfigFingerprintRegistrationHandle.Reset();
	SimulationContentRegistrationHandle.Reset();
	PoolObjectCodecHandle.Reset();
	CanonicalStateRegistrationHandle.Reset();

	FSeinPoolObjectCodecDescriptor PoolDescriptor;
	PoolDescriptor.NativeAnchor =
		USeinCoverAwareDefaultBrokerResolver::StaticClass();
	PoolDescriptor.Kind =
		ESeinPoolObjectKind::CommandBrokerResolver;
	PoolDescriptor.StableProviderId =
		TEXT("seinarts.cover.pool.default-resolver.reflection");
	PoolDescriptor.StateSchemaVersion = 1;
	PoolDescriptor.BehaviorRevision = 2;
	PoolDescriptor.CodecRevision = 2;
	PoolDescriptor.MaxStateBytes =
		FSeinPoolObjectCodecRegistry::MaxStateBytes;
	PoolDescriptor.bAllowBlueprintChildren = true;
	FString PoolCodecError;
	PoolObjectCodecHandle =
		FSeinPoolObjectCodecRegistry::Register(
			GCoverPoolCodecOwner,
			PoolDescriptor,
			FSeinPoolObjectCodecRegistry::MakeReflectedOps(),
			&PoolCodecError);
	if (!PoolObjectCodecHandle.IsValid())
	{
		UE_LOG(LogSeinARTSCover, Error,
			TEXT("Cover pool-object codec failed to register: %s"),
			*PoolCodecError);
	}

	ConfigFingerprintRegistrationHandle =
		FSeinConfigFingerprintRegistry::RegisterContributor(
		GCoverFingerprintId,
		GetDefault<USeinARTSCoverSettings>(),
		{
			GET_MEMBER_NAME_CHECKED(USeinARTSCoverSettings, CoverSystemClass),
			GET_MEMBER_NAME_CHECKED(USeinARTSCoverSettings, CoverSnapRadius),
			GET_MEMBER_NAME_CHECKED(USeinARTSCoverSettings, TerrainCoverQuality),
		});
	if (!ConfigFingerprintRegistrationHandle.IsValid())
	{
		UE_LOG(LogSeinARTSCover, Fatal,
			TEXT("Cover's lockstep config-fingerprint schema failed to register."));
	}

	FSeinSimulationContentContributorDescriptor ContentDescriptor;
	ContentDescriptor.StableContributorId = TEXT("seinarts.cover");
	ContentDescriptor.ContributorRevision = 1;
	ContentDescriptor.DiscoveryRoots = {
		MakePackageDiscoveryRoot(USeinCoverSystem::StaticClass()),
	};

	FString ContentRegistrationError;
	SimulationContentRegistrationHandle =
		FSeinSimulationContentRegistry::RegisterContributor(
			ContentDescriptor,
			&ContentRegistrationError);
	if (!SimulationContentRegistrationHandle.IsValid())
	{
		UE_LOG(
			LogSeinARTSCover,
			Error,
			TEXT("Simulation-content contributor '%s' failed to register: %s"),
			*ContentDescriptor.StableContributorId,
			*ContentRegistrationError);
	}

	FString StateRegistrationError;
	CanonicalStateRegistrationHandle =
		SeinRegisterCoverCanonicalStateProvider(
			StateRegistrationError);
	if (!CanonicalStateRegistrationHandle.IsValid())
	{
		UE_LOG(
			LogSeinARTSCover,
			Error,
			TEXT("Canonical-state contributor 'seinarts.cover.system-binding' failed to register: %s"),
			*StateRegistrationError);
	}

	UE_LOG(LogSeinARTSCover, Log, TEXT("SeinARTSCover module started."));
}

void FSeinARTSCoverModule::PreUnloadCallback()
{
	check(IsInGameThread());
	for (TObjectIterator<USeinWorldSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->TerminateAndReleaseForModuleUnload(
				TEXT("SeinARTSCover"),
				TEXT("cover systems and broker resolvers are unloading"));
		}
	}

	for (TObjectIterator<USeinCoverSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->ReleaseModuleOwnedStateForModuleUnload();
		}
	}
	PoolObjectCodecHandle.Reset();

	// Frozen worlds retain only tokens, while the process registry owns
	// module TFunctions. Withdraw this exact generation before code unload.
	CanonicalStateRegistrationHandle.Reset();
}

void FSeinARTSCoverModule::ShutdownModule()
{
	PoolObjectCodecHandle.Reset();
	CanonicalStateRegistrationHandle.Reset();
	SimulationContentRegistrationHandle.Reset();
	ConfigFingerprintRegistrationHandle.Reset();
	UE_LOG(LogSeinARTSCover, Log, TEXT("SeinARTSCover module shut down."));
}

IMPLEMENT_MODULE(FSeinARTSCoverModule, SeinARTSCover)

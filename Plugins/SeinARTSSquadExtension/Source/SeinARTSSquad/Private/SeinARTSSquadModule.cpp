/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 */

#include "SeinARTSSquadModule.h"
#include "SeinARTSSquadSettings.h"
#include "SeinAbility_SquadReinforce.h"
#include "SeinSquadDispatchResolver.h"
#include "SeinSquadSubsystem.h"
#include "Settings/SeinConfigFingerprintRegistry.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"

IMPLEMENT_MODULE(FSeinARTSSquadModule, SeinARTSSquad)

DEFINE_LOG_CATEGORY_STATIC(LogSeinARTSSquad, Log, All);

namespace
{
	// FROZEN cross-client wire identifier — it drives the deterministic fingerprint
	// fold order. NEVER rename it: a different id sorts to a different position and
	// changes every client's fingerprint.
	const FName GSquadFingerprintId(TEXT("SquadExtension"));
	const FName GSquadPoolCodecOwner(TEXT("seinartssquad"));

	bool RegisterSquadPoolCodecs(
		TArray<FSeinPoolObjectCodecRegistrationHandle>& OutHandles,
		FString& OutError)
	{
		OutHandles.Reset();
		struct FSpec
		{
			const UClass* Anchor;
			ESeinPoolObjectKind Kind;
			const TCHAR* StableProviderId;
		};
		const FSpec Specs[] = {
			{
				USeinAbility_SquadReinforce::StaticClass(),
				ESeinPoolObjectKind::Ability,
				TEXT("seinarts.squad.pool.reinforce.reflection"),
			},
			{
				USeinSquadDispatchResolver::StaticClass(),
				ESeinPoolObjectKind::CommandBrokerResolver,
				TEXT("seinarts.squad.pool.dispatch-resolver.reflection"),
			},
		};
		for (const FSpec& Spec : Specs)
		{
			FSeinPoolObjectCodecDescriptor Descriptor;
			Descriptor.NativeAnchor = Spec.Anchor;
			Descriptor.Kind = Spec.Kind;
			Descriptor.StableProviderId = Spec.StableProviderId;
			Descriptor.StateSchemaVersion = 1;
			Descriptor.BehaviorRevision = 1;
			Descriptor.CodecRevision = 2;
			Descriptor.MaxStateBytes =
				FSeinPoolObjectCodecRegistry::MaxStateBytes;
			Descriptor.bAllowBlueprintChildren = true;
			FSeinPoolObjectCodecRegistrationHandle Handle =
				FSeinPoolObjectCodecRegistry::Register(
					GSquadPoolCodecOwner,
					Descriptor,
					FSeinPoolObjectCodecRegistry::MakeReflectedOps(),
					&OutError);
			if (!Handle.IsValid())
			{
				OutHandles.Reset();
				return false;
			}
			OutHandles.Add(MoveTemp(Handle));
		}
		return true;
	}
}

void FSeinARTSSquadModule::StartupModule()
{
	ConfigFingerprintRegistrationHandle.Reset();
	SimulationContentRegistrationHandle.Reset();
	PoolObjectCodecHandles.Reset();

	FString PoolCodecError;
	if (!RegisterSquadPoolCodecs(
		PoolObjectCodecHandles, PoolCodecError))
	{
		UE_LOG(LogSeinARTSSquad, Error,
			TEXT("Squad pool-object codecs failed to register: %s"),
			*PoolCodecError);
	}

	// Register this extension's SIM-AFFECTING settings into the lockstep config-parity
	// fingerprint, so two clients differing on them (or one missing this plugin) are
	// rejected at join instead of silently desyncing. Compile-time member checks and
	// registry validation keep schema mistakes from degrading to empty values.
	ConfigFingerprintRegistrationHandle =
		FSeinConfigFingerprintRegistry::RegisterContributor(
		GSquadFingerprintId,
		GetDefault<USeinARTSSquadSettings>(),
		{
			GET_MEMBER_NAME_CHECKED(USeinARTSSquadSettings, bPaceSquadsTogether),
			GET_MEMBER_NAME_CHECKED(USeinARTSSquadSettings, DefaultSquadDispatchResolverClass),
		});
	if (!ConfigFingerprintRegistrationHandle.IsValid())
	{
		UE_LOG(LogSeinARTSSquad, Fatal,
			TEXT("Squad's lockstep config-fingerprint schema failed to register."));
	}

	FSeinSimulationContentContributorDescriptor ContentDescriptor;
	ContentDescriptor.StableContributorId = TEXT("seinarts.squad");
	ContentDescriptor.ContributorRevision = 1;

	FString ContentRegistrationError;
	SimulationContentRegistrationHandle =
		FSeinSimulationContentRegistry::RegisterContributor(
			ContentDescriptor,
			&ContentRegistrationError);
	if (!SimulationContentRegistrationHandle.IsValid())
	{
		UE_LOG(
			LogSeinARTSSquad,
			Error,
			TEXT("Simulation-content contributor '%s' failed to register: %s"),
			*ContentDescriptor.StableContributorId,
			*ContentRegistrationError);
	}
}

void FSeinARTSSquadModule::PreUnloadCallback()
{
	check(IsInGameThread());
	for (TObjectIterator<USeinWorldSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->TerminateAndReleaseForModuleUnload(
				TEXT("SeinARTSSquad"),
				TEXT("squad systems and dispatch resolvers are unloading"));
		}
	}

	for (TObjectIterator<USeinSquadSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->ReleaseHostedSystemsForModuleUnload();
		}
	}
	PoolObjectCodecHandles.Reset();
}

void FSeinARTSSquadModule::ShutdownModule()
{
	PoolObjectCodecHandles.Reset();
	SimulationContentRegistrationHandle.Reset();
	ConfigFingerprintRegistrationHandle.Reset();
}

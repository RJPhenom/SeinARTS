/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLevelDataCanonicalStateProvider.cpp
 * @brief   Binding-only StateContract contributor for the pluggable substrate.
 */

#include "Serialization/SeinLevelDataCanonicalStateProvider.h"

#include "SeinLevelDataSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"

namespace
{
	const FName OwnerModuleId(TEXT("SeinARTSLevelData"));

	USeinLevelDataSubsystem* ResolveSubsystem(
		const USeinWorldSubsystem& Services)
	{
		UWorld* World = Services.GetWorld();
		return World
			? World->GetSubsystem<USeinLevelDataSubsystem>()
			: nullptr;
	}

	bool PrepareWorldBinding(
		const FSeinCanonicalStateWorldBindingContext& Context,
		FString& OutError)
	{
		USeinLevelDataSubsystem* Subsystem =
			ResolveSubsystem(Context.Services);
		UWorld* World = Context.Services.GetWorld();
		if (!Subsystem || !World)
		{
			OutError =
				TEXT("Level Data canonical state could not resolve its world subsystem.");
			return false;
		}
		return Subsystem->EnsureInitialRuntimeDataPrepared(*World);
	}

	bool FreezeWorldBinding(
		const FSeinCanonicalStateWorldBindingContext& Context,
		FString& OutFrame,
		FString& OutError)
	{
		USeinLevelDataSubsystem* Subsystem =
			ResolveSubsystem(Context.Services);
		if (!Subsystem)
		{
			OutError =
				TEXT("Level Data canonical state could not resolve its world subsystem.");
			return false;
		}
		return Subsystem->FreezeCanonicalStateBinding(
			Context.BindingDisposition
				== ESeinCanonicalStateWorldBindingDisposition::
					BootstrapCommit,
			OutFrame,
			OutError);
	}
}

FSeinCanonicalStateRegistrationHandle
SeinRegisterLevelDataCanonicalStateProvider(FString& OutError)
{
	FSeinCanonicalStateDescriptor Descriptor;
	Descriptor.Key.StableDomainId = TEXT("seinarts.level-data");
	Descriptor.Key.StableContributorId = TEXT("substrate-binding");
	Descriptor.SchemaVersion = 1;
	Descriptor.ImplementationRevision = 1;
	Descriptor.Role = ESeinCanonicalStateRole::DerivedCache;
	Descriptor.Limits.MaxRecursionDepth = 8;
	Descriptor.Limits.MaxEncodedBytes = 64 * 1024;
	Descriptor.Limits.MaxAggregateElements = 1024;
	// Level Data is a world subsystem service, not a ticked simulation system.
	Descriptor.bExternallyOwned = true;

	FSeinCanonicalStateContributorOps Ops;
	Ops.PrepareWorldBinding = &PrepareWorldBinding;
	Ops.FreezeWorldBinding = &FreezeWorldBinding;
	Ops.StageDerived = [](
		const FSeinCanonicalStateStageContext&,
		TUniquePtr<ISeinCanonicalStateRestoreStage>&,
		FString&)
		{
			return true;
		};
	Ops.CommitDerived = [](
		FSeinCanonicalStateCommitContext&,
		TUniquePtr<ISeinCanonicalStateRestoreStage>&&)
		{
		};
	return FSeinCanonicalStateRegistry::Register(
		OwnerModuleId, Descriptor, MoveTemp(Ops), &OutError);
}

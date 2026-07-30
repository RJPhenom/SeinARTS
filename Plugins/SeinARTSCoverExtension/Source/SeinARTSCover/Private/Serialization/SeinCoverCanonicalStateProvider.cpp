/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverCanonicalStateProvider.cpp
 * @brief   Binding-only canonical contributor for the active cover system.
 *
 *          Cover keeps no persistent canonical payload of its own — the
 *          derived provider registry is rebuilt from authoritative entities
 *          on restore. This contributor exists so the ACTIVE cover
 *          implementation's exact-state coverage claim is validated
 *          fail-closed at bootstrap and folded into the per-tick-revalidated
 *          match StateContract: a custom USeinCoverSystem subclass with
 *          undeclared future-affecting mutable state can no longer proceed
 *          silently through snapshot capture/restore.
 */

#include "Serialization/SeinCoverCanonicalStateProvider.h"

#include "System/SeinCoverSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"

namespace
{
	const FName OwnerModuleId(TEXT("SeinARTSCover"));

	USeinCoverSubsystem* ResolveSubsystem(
		const USeinWorldSubsystem& Services)
	{
		UWorld* World = Services.GetWorld();
		return World
			? World->GetSubsystem<USeinCoverSubsystem>()
			: nullptr;
	}
}

struct FSeinCoverCanonicalStateProvider
{
	static bool FreezeWorldBinding(
		const FSeinCanonicalStateWorldBindingContext& Context,
		FString& OutFrame,
		FString& OutError)
	{
		USeinCoverSubsystem* CoverSubsystem =
			ResolveSubsystem(Context.Services);
		if (!CoverSubsystem)
		{
			OutError =
				TEXT("Cover canonical state could not resolve its world subsystem.");
			return false;
		}
		return CoverSubsystem->FreezeCanonicalStateBinding(
			Context.BindingDisposition
				== ESeinCanonicalStateWorldBindingDisposition::
					BootstrapCommit,
			OutFrame,
			OutError);
	}
};

FSeinCanonicalStateRegistrationHandle
SeinRegisterCoverCanonicalStateProvider(FString& OutError)
{
	FSeinCanonicalStateDescriptor Descriptor;
	Descriptor.Key.StableDomainId = TEXT("seinarts.cover");
	Descriptor.Key.StableContributorId =
		TEXT("system-binding");
	Descriptor.SchemaVersion = 1;
	Descriptor.ImplementationRevision = 1;
	Descriptor.Role = ESeinCanonicalStateRole::DerivedCache;
	// Binding-only: no canonical payload, small defensive bounds.
	Descriptor.Limits.MaxRecursionDepth = 8;
	Descriptor.Limits.MaxEncodedBytes = 64 * 1024;
	Descriptor.Limits.MaxAggregateElements = 1024;
	// The cover subsystem owns this contributor's lifecycle; no registered
	// simulation system claims it, so it must declare external ownership to
	// pass the orphaned-contributor bootstrap gate.
	Descriptor.bExternallyOwned = true;

	FSeinCanonicalStateContributorOps Ops;
	Ops.FreezeWorldBinding =
		&FSeinCoverCanonicalStateProvider::FreezeWorldBinding;
	// DerivedCache with no payload: derived provider bookkeeping is rebuilt
	// by USeinCoverSubsystem's OnAuthoritativeStateRestored reconciliation,
	// so the registry's staged/commit hooks have nothing to do here.
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

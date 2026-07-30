/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionCanonicalStateProvider.cpp
 * @brief   Binding-only canonical-state contributor for the pluggable
 *          collision resolver (STATE-01).
 *
 *          The resolver retains no captured payload of its own — its claim IS
 *          the contract: the active implementation must explicitly declare its
 *          mutable-state coverage (Stateless, or the exact authoritative
 *          contributors that restore its retained state) and digest every
 *          resolution-affecting tuning value. Both are folded into a
 *          world-binding frame the match StateContract freezes at bootstrap
 *          and recaptures at every fixed-tick boundary, so an unclaimed custom
 *          resolver fails bootstrap closed and a post-freeze tuning edit
 *          fail-stops the world instead of silently desyncing peers. Mirrors
 *          the navigation world-binding shape.
 */

#include "Serialization/SeinCollisionCanonicalStateProvider.h"

#include "Collision/SeinCollisionResolver.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace
{
	const FName GCollisionCanonicalStateOwner(TEXT("seinartscoreentity"));

	constexpr int32 MaxCollisionStateContributors = 64;

	void AppendFramed(FString& Out, const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		Out += FString::Printf(TEXT("%d:"), Utf8.Length());
		Out += Value;
		Out += TEXT("\n");
	}

	bool FreezeCollisionWorldBinding(
		const FSeinCanonicalStateWorldBindingContext& Context,
		FString& OutFrame,
		FString& OutError)
	{
		OutFrame.Reset();
		OutError.Reset();

		FString Frame = TEXT("SeinARTS.Collision.WorldBinding\n");
		AppendFramed(Frame, TEXT("1"));

		const USeinCollisionResolver* Resolver =
			Context.Services.GetCollisionResolver();
		if (!Resolver)
		{
			// None = collision resolution intentionally OFF; the binding still
			// declares that explicitly so peers agree it is off.
			AppendFramed(Frame, TEXT("disabled"));
			OutFrame = MoveTemp(Frame);
			return true;
		}

		FSeinCollisionResolverStateCoverageClaim Claim;
		if (!Resolver->ComputeStateCoverageClaim(Claim, OutError)
			|| Claim.StableImplementationId.IsEmpty()
			|| Claim.StableImplementationId
				!= Claim.StableImplementationId.TrimStartAndEnd()
			|| Claim.BehaviorRevision == 0
			|| Claim.CoverageRevision == 0)
		{
			if (OutError.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Collision resolver '%s' returned an invalid exact-state coverage claim."),
					*Resolver->GetClass()->GetPathName());
			}
			return false;
		}

		FString CoverageKind;
		TArray<FString> CanonicalRequiredKeys;
		switch (Claim.StateCoverage)
		{
		case ESeinCollisionResolverStateCoverage::Stateless:
			CoverageKind = TEXT("stateless");
			if (!Claim.RequiredCanonicalStateContributors.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Stateless collision resolver '%s' names supplemental canonical-state contributors."),
					*Resolver->GetClass()->GetPathName());
				return false;
			}
			break;

		case ESeinCollisionResolverStateCoverage::CanonicalStateContributors:
			CoverageKind = TEXT("canonical-state-contributors");
			if (Claim.RequiredCanonicalStateContributors.IsEmpty()
				|| Claim.RequiredCanonicalStateContributors.Num()
					> MaxCollisionStateContributors)
			{
				OutError = FString::Printf(
					TEXT("Stateful collision resolver '%s' names an invalid supplemental canonical-state contributor count."),
					*Resolver->GetClass()->GetPathName());
				return false;
			}
			CanonicalRequiredKeys.Reserve(
				Claim.RequiredCanonicalStateContributors.Num());
			for (const FSeinCanonicalStateKey& Required :
				Claim.RequiredCanonicalStateContributors)
			{
				const FString CanonicalKey =
					FSeinCanonicalStateRegistry::CanonicalKey(Required);
				if (CanonicalKey.IsEmpty())
				{
					OutError = FString::Printf(
						TEXT("Collision resolver '%s' names an invalid supplemental canonical-state contributor."),
						*Resolver->GetClass()->GetPathName());
					return false;
				}
				if (!Context.Services.HasFrozenCanonicalStateContributor(
					Required,
					ESeinCanonicalStateRole::Authoritative))
				{
					OutError = FString::Printf(
						TEXT("Collision resolver '%s' requires missing authoritative canonical-state contributor '%s'."),
						*Resolver->GetClass()->GetPathName(),
						*CanonicalKey);
					return false;
				}
				CanonicalRequiredKeys.Add(CanonicalKey);
			}
			CanonicalRequiredKeys.Sort();
			for (int32 Index = 1;
				Index < CanonicalRequiredKeys.Num();
				++Index)
			{
				if (CanonicalRequiredKeys[Index - 1]
					== CanonicalRequiredKeys[Index])
				{
					OutError = FString::Printf(
						TEXT("Collision resolver '%s' names duplicate canonical-state contributor '%s'."),
						*Resolver->GetClass()->GetPathName(),
						*CanonicalRequiredKeys[Index]);
					return false;
				}
			}
			break;

		case ESeinCollisionResolverStateCoverage::Unspecified:
		default:
			OutError = FString::Printf(
				TEXT("Collision resolver '%s' did not declare whether its mutable state is stateless or restored by canonical contributors."),
				*Resolver->GetClass()->GetPathName());
			return false;
		}

		FGuid ConfigDigest;
		if (!Resolver->ComputeResolutionConfigDigest(ConfigDigest, OutError)
			|| !ConfigDigest.IsValid())
		{
			if (OutError.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Collision resolver '%s' returned an invalid resolution-config digest."),
					*Resolver->GetClass()->GetPathName());
			}
			return false;
		}

		AppendFramed(Frame, TEXT("enabled"));
		AppendFramed(Frame, Resolver->GetClass()->GetPathName());
		AppendFramed(Frame, Claim.StableImplementationId);
		AppendFramed(Frame, LexToString(Claim.BehaviorRevision));
		AppendFramed(Frame, LexToString(Claim.CoverageRevision));
		AppendFramed(Frame, CoverageKind);
		AppendFramed(Frame, LexToString(CanonicalRequiredKeys.Num()));
		for (const FString& RequiredKey : CanonicalRequiredKeys)
		{
			AppendFramed(Frame, RequiredKey);
		}
		AppendFramed(
			Frame, ConfigDigest.ToString(EGuidFormats::Digits));
		OutFrame = MoveTemp(Frame);
		return true;
	}
}

FSeinCanonicalStateRegistrationHandle
SeinRegisterCollisionCanonicalStateProvider(FString& OutError)
{
	FSeinCanonicalStateDescriptor Descriptor;
	Descriptor.Key.StableDomainId = TEXT("seinarts.collision");
	Descriptor.Key.StableContributorId = TEXT("resolver-binding");
	Descriptor.SchemaVersion = 1;
	Descriptor.ImplementationRevision = 1;
	Descriptor.Role = ESeinCanonicalStateRole::DerivedCache;
	Descriptor.Limits.MaxRecursionDepth = 8;
	Descriptor.Limits.MaxEncodedBytes = 64 * 1024;
	Descriptor.Limits.MaxAggregateElements = 1024;
	// The world subsystem owns the resolver's lifecycle; no ticked system
	// claims this binding-only contributor.
	Descriptor.bExternallyOwned = true;

	FSeinCanonicalStateContributorOps Ops;
	Ops.FreezeWorldBinding = &FreezeCollisionWorldBinding;
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
		GCollisionCanonicalStateOwner, Descriptor, MoveTemp(Ops), &OutError);
}

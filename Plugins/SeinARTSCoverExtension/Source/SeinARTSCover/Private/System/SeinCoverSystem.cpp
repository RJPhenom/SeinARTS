/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverSystem.cpp
 */

#include "System/SeinCoverSystem.h"
#include "Simulation/SeinWorldSubsystem.h"

#include "Algo/BinarySearch.h"
#include "Algo/Unique.h"

void USeinCoverSystem::OnCoverSystemInitialized(USeinWorldSubsystem* InWorld)
{
	World = InWorld;
}

void USeinCoverSystem::OnCoverSystemDeinitialized()
{
	AuthoritativeProviderHandles.Reset();
	World = nullptr;
}

void USeinCoverSystem::RegisterAuthoritativeProvider(
	FSeinEntityHandle ProviderHandle)
{
	const int32 InsertIndex = Algo::LowerBound(
		AuthoritativeProviderHandles, ProviderHandle);
	if (AuthoritativeProviderHandles.IsValidIndex(InsertIndex)
		&& AuthoritativeProviderHandles[InsertIndex] == ProviderHandle)
	{
		return;
	}

	if (InsertIndex == AuthoritativeProviderHandles.Num())
	{
		AuthoritativeProviderHandles.Add(ProviderHandle);
		RegisterProvider(ProviderHandle);
		return;
	}

	// Slot reuse can introduce a lower canonical handle after later handles
	// were already registered. Rebuild through the legacy implementation hooks
	// so custom indexes never inherit history-dependent iteration order.
	for (const FSeinEntityHandle ExistingHandle :
		AuthoritativeProviderHandles)
	{
		UnregisterProvider(ExistingHandle);
	}
	AuthoritativeProviderHandles.Insert(ProviderHandle, InsertIndex);
	for (const FSeinEntityHandle CanonicalHandle :
		AuthoritativeProviderHandles)
	{
		RegisterProvider(CanonicalHandle);
	}
}

void USeinCoverSystem::UnregisterAuthoritativeProvider(
	FSeinEntityHandle ProviderHandle)
{
	const int32 ExistingIndex = Algo::LowerBound(
		AuthoritativeProviderHandles, ProviderHandle);
	if (AuthoritativeProviderHandles.IsValidIndex(ExistingIndex)
		&& AuthoritativeProviderHandles[ExistingIndex] == ProviderHandle)
	{
		AuthoritativeProviderHandles.RemoveAt(ExistingIndex);
	}
	UnregisterProvider(ProviderHandle);
}

void USeinCoverSystem::RebuildProviderRegistry(
	const TArray<FSeinEntityHandle>& ProviderHandles)
{
	for (const FSeinEntityHandle ExistingHandle :
		AuthoritativeProviderHandles)
	{
		UnregisterProvider(ExistingHandle);
	}
	AuthoritativeProviderHandles.Reset();

	TArray<FSeinEntityHandle> CanonicalHandles = ProviderHandles;
	CanonicalHandles.Sort();
	CanonicalHandles.SetNum(Algo::Unique(CanonicalHandles));
	for (const FSeinEntityHandle ProviderHandle : CanonicalHandles)
	{
		RegisterAuthoritativeProvider(ProviderHandle);
	}
}

bool USeinCoverSystem::ComputeStateCoverageClaim(
	FSeinCoverStateCoverageClaim& OutClaim,
	FString& OutError) const
{
	OutClaim = {};
	OutError = FString::Printf(
		TEXT("Cover implementation '%s' does not explicitly claim exact mutable-state coverage."),
		*GetClass()->GetPathName());
	return false;
}

TArray<FSeinCoverContext> USeinCoverSystem::QueryCoverAt(FFixedVector /*WorldPoint*/,
	FSeinPlayerID /*Observer*/) const
{
	// Abstract — subclasses MUST override. Returning empty is safe (no cover
	// reported anywhere, callers behave as if every point is open ground).
	return {};
}

FGameplayTag USeinCoverSystem::QueryBestCoverQualityAt(FFixedVector WorldPoint,
	FSeinPlayerID Observer) const
{
	// Default: pick the first context's quality. Subclasses with a designer-
	// configured priority ordering override to walk the array and select by
	// weight. The framework default is deliberately dumb so a project that
	// hasn't set up a priority table still gets a sensible-looking color on
	// the preview decal.
	const TArray<FSeinCoverContext> Contexts = QueryCoverAt(WorldPoint, Observer);
	if (Contexts.Num() == 0) return FGameplayTag();
	return Contexts[0].QualityTag;
}

TArray<FSeinCoverSlotCandidate> USeinCoverSystem::FindNearbySlots(FFixedVector /*Origin*/,
	FFixedPoint /*Radius*/, FSeinPlayerID /*Observer*/) const
{
	// Abstract — subclasses MUST override. Empty result is safe (cover-aware
	// resolvers fall through to default formation positions).
	return {};
}

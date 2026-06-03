/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverSystem.cpp
 */

#include "System/SeinCoverSystem.h"
#include "Simulation/SeinWorldSubsystem.h"

void USeinCoverSystem::OnCoverSystemInitialized(USeinWorldSubsystem* InWorld)
{
	World = InWorld;
}

void USeinCoverSystem::OnCoverSystemDeinitialized()
{
	World = nullptr;
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

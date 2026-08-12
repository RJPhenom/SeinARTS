/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigation.cpp
 */

#include "SeinNavigation.h"

#include "SeinLevelData.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"
#include "SeinARTSNavigationLog.h"
#include "Settings/PluginSettings.h"

void USeinNavigation::InitializeForWorld(UWorld* World)
{
	OwningWorld = World;
	OnNavigationInitialized(World);
}

void USeinNavigation::DeinitializeFromWorld()
{
	OnNavigationDeinitialized();
	OwningWorld.Reset();
}

bool USeinNavigation::CanMutateStaticEnvironment(FString& OutError) const
{
	OutError.Reset();
	UWorld* World = OwningWorld.Get();
	if (!World)
	{
		World = GetWorld();
	}
	const USeinWorldSubsystem* Sim = World
		? World->GetSubsystem<USeinWorldSubsystem>()
		: nullptr;
	if (Sim && Sim->GetCanonicalStateContractDigest().IsValid())
	{
		OutError =
			TEXT("Navigation substrate adoption is not legal after the match StateContract freezes; load or generate static topology before bootstrap, then restart the match/PIE session.");
		return false;
	}
	return true;
}

FSeinStaticEnvironmentAdoptionResult USeinNavigation::LoadFromSubstrate(
	const USeinLevelData& Substrate)
{
	FString Error;
	if (!CanMutateStaticEnvironment(Error))
	{
		UE_LOG(LogSeinNavSubsystem, Error, TEXT("%s"), *Error);
		return FSeinStaticEnvironmentAdoptionResult::Rejected(
			MoveTemp(Error));
	}
	FSeinStaticEnvironmentAdoptionResult Result =
		LoadFromSubstrateImpl(Substrate);
	if (!Result.IsAdopted()
		&& !Result.IsNotApplicable()
		&& !Result.IsRejected())
	{
		Result = FSeinStaticEnvironmentAdoptionResult::Rejected(
			FString::Printf(
				TEXT("Navigation implementation '%s' returned an invalid substrate-adoption outcome."),
				*GetClass()->GetPathName()));
	}
	else if (Result.IsRejected() && Result.Detail.IsEmpty())
	{
		Result.Detail = FString::Printf(
			TEXT("Navigation implementation '%s' rejected the Level Data substrate without a reason."),
			*GetClass()->GetPathName());
	}
	else if (Result.IsNotApplicable()
		&& Substrate.HasRuntimeData()
		&& GetLevelDataProvider())
	{
		Result = FSeinStaticEnvironmentAdoptionResult::Rejected(
			FString::Printf(
				TEXT("Navigation implementation '%s' participates in the Level Data bake but did not adopt the prepared runtime substrate%s%s."),
				*GetClass()->GetPathName(),
				Result.Detail.IsEmpty() ? TEXT("") : TEXT(": "),
				*Result.Detail));
	}
	return Result;
}

bool USeinNavigation::ComputeStaticEnvironmentDigest(
	FGuid& OutDigest,
	FString& OutError) const
{
	OutDigest.Invalidate();
	OutError = FString::Printf(
		TEXT("Navigation implementation '%s' does not provide an explicit exact static-environment digest override."),
		*GetClass()->GetPathName());
	return false;
}

bool USeinNavigation::ComputeStateCoverageClaim(
	FSeinNavigationStateCoverageClaim& OutClaim,
	FString& OutError) const
{
	OutClaim = {};
	OutError = FString::Printf(
		TEXT("Navigation implementation '%s' does not explicitly claim exact mutable-state coverage."),
		*GetClass()->GetPathName());
	return false;
}

bool USeinNavigation::IsReachable(const FFixedVector& From, const FFixedVector& To, const FGameplayTagContainer& AgentTags) const
{
	FSeinNavAgentProfile Agent;
	Agent.AgentTags = AgentTags;
	return IsReachableForAgent(From, To, Agent);
}

bool USeinNavigation::IsReachableForAgent(
	const FFixedVector& From,
	const FFixedVector& To,
	const FSeinNavAgentProfile& Agent) const
{
	FSeinPathRequest Request;
	Request.Start = From;
	Request.End = To;
	Request.Requester = Agent.Requester;
	Request.BlockedTerrainTags = Agent.BlockedTerrainTags;
	Request.AgentNavLayerMask = Agent.AgentNavLayerMask;
	Request.AgentFootprintRadius = Agent.AgentFootprintRadius;
	Request.AgentWallPaddingCells = Agent.AgentWallPaddingCells;
	FSeinPath Path;
	return FindPath(Request, Path)
		&& Path.bIsValid
		&& !Path.bIsPartial;
}

bool USeinNavigation::IsWorldPositionClearForAgent(
	const FFixedVector& WorldPos,
	const FSeinNavAgentProfile& Agent) const
{
	if (!IsWorldPositionClear(
		WorldPos, Agent.AgentNavLayerMask))
	{
		return false;
	}
	if (Agent.BlockedTerrainTags.IsEmpty())
	{
		return true;
	}
	const USeinARTSCoreSettings* Settings =
		GetDefault<USeinARTSCoreSettings>();
	const int32 TerrainType = GetTerrainTypeAt(WorldPos);
	return !Settings
		|| !Settings->TerrainTypes.IsValidIndex(TerrainType - 1)
		|| !Agent.BlockedTerrainTags.HasTag(
			Settings->TerrainTypes[TerrainType - 1].TerrainTag);
}

bool USeinNavigation::IsFootprintClearForAgent(
	const FFixedVector& WorldPos,
	const FSeinNavAgentProfile& Agent) const
{
	if (!IsWorldPositionClearForAgent(WorldPos, Agent))
	{
		return false;
	}
	const FFixedPoint Radius = Agent.AgentFootprintRadius;
	if (Radius <= FFixedPoint::Zero)
	{
		return true;
	}
	static const FFixedPoint RingDiag(3036971375LL);
	static const FFixedVector Ring[8] = {
		FFixedVector(FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::Zero),
		FFixedVector(RingDiag, RingDiag, FFixedPoint::Zero),
		FFixedVector(FFixedPoint::Zero, FFixedPoint::One, FFixedPoint::Zero),
		FFixedVector(-RingDiag, RingDiag, FFixedPoint::Zero),
		FFixedVector(-FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::Zero),
		FFixedVector(-RingDiag, -RingDiag, FFixedPoint::Zero),
		FFixedVector(FFixedPoint::Zero, -FFixedPoint::One, FFixedPoint::Zero),
		FFixedVector(RingDiag, -RingDiag, FFixedPoint::Zero),
	};
	for (const FFixedVector& Offset : Ring)
	{
		const FFixedVector Sample(
			WorldPos.X + Offset.X * Radius,
			WorldPos.Y + Offset.Y * Radius,
			WorldPos.Z);
		if (!IsWorldPositionClearForAgent(Sample, Agent))
		{
			return false;
		}
	}
	return true;
}

bool USeinNavigation::IsFootprintClearForAgentIgnoringDynamicBlockers(
	const FFixedVector& WorldPos,
	const FSeinNavAgentProfile& Agent,
	const TSet<FSeinEntityHandle>& /*IgnoredDynamicBlockerOwners*/) const
{
	return IsFootprintClearForAgent(WorldPos, Agent);
}

bool USeinNavigation::IsAuthoritativeDestinationSafeForAgent(
	const FFixedVector& WorldPos,
	const FSeinNavAgentProfile& Agent) const
{
	if (!HasRuntimeData())
	{
		return false;
	}
	if (Agent.BlockedTerrainTags.IsEmpty())
	{
		return true;
	}
	const USeinARTSCoreSettings* Settings =
		GetDefault<USeinARTSCoreSettings>();
	const int32 TerrainType = GetTerrainTypeAt(WorldPos);
	return !Settings
		|| !Settings->TerrainTypes.IsValidIndex(TerrainType - 1)
		|| !Agent.BlockedTerrainTags.HasTag(
			Settings->TerrainTypes[TerrainType - 1].TerrainTag);
}

bool USeinNavigation::IsAuthoritativeFootprintSafeForAgent(
	const FFixedVector& WorldPos,
	const FSeinNavAgentProfile& Agent) const
{
	if (!IsAuthoritativeDestinationSafeForAgent(WorldPos, Agent))
	{
		return false;
	}
	const FFixedPoint Radius = Agent.AgentFootprintRadius;
	if (Radius <= FFixedPoint::Zero)
	{
		return true;
	}
	static const FFixedPoint RingDiag(3036971375LL);
	static const FFixedVector Ring[8] = {
		FFixedVector(FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::Zero),
		FFixedVector(RingDiag, RingDiag, FFixedPoint::Zero),
		FFixedVector(FFixedPoint::Zero, FFixedPoint::One, FFixedPoint::Zero),
		FFixedVector(-RingDiag, RingDiag, FFixedPoint::Zero),
		FFixedVector(-FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::Zero),
		FFixedVector(-RingDiag, -RingDiag, FFixedPoint::Zero),
		FFixedVector(FFixedPoint::Zero, -FFixedPoint::One, FFixedPoint::Zero),
		FFixedVector(RingDiag, -RingDiag, FFixedPoint::Zero),
	};
	for (const FFixedVector& Offset : Ring)
	{
		const FFixedVector Sample(
			WorldPos.X + Offset.X * Radius,
			WorldPos.Y + Offset.Y * Radius,
			WorldPos.Z);
		if (!IsAuthoritativeDestinationSafeForAgent(Sample, Agent))
		{
			return false;
		}
	}
	return true;
}

bool USeinNavigation::ProjectPointToNavForAgent(
	const FFixedVector& WorldPos,
	const FSeinNavAgentProfile& Agent,
	FFixedVector& OutProjected) const
{
	FFixedVector Candidate;
	if (!ProjectPointToNav(WorldPos, Candidate)
		|| !IsFootprintClearForAgent(Candidate, Agent))
	{
		return false;
	}
	OutProjected = Candidate;
	return true;
}

bool USeinNavigation::ProjectPointToNavFreeForAgent(
	const FFixedVector& WorldPos,
	const FSeinNavAgentProfile& Agent,
	const TArray<FFixedVector>& AvoidCentres,
	const TArray<FFixedPoint>& AvoidRadii,
	FFixedVector& OutProjected) const
{
	FFixedVector Candidate;
	if (ProjectPointToNavFree(
			WorldPos,
			Agent.AgentFootprintRadius,
			AvoidCentres,
			AvoidRadii,
			Candidate)
		&& IsFootprintClearForAgent(Candidate, Agent))
	{
		OutProjected = Candidate;
		return true;
	}
	return ProjectPointToNavForAgent(
		WorldPos, Agent, OutProjected);
}

bool USeinNavigation::ProjectPointToNavFreeForAgentIgnoringDynamicBlockers(
	const FFixedVector& WorldPos,
	const FSeinNavAgentProfile& Agent,
	const TSet<FSeinEntityHandle>& /*IgnoredDynamicBlockerOwners*/,
	const TArray<FFixedVector>& AvoidCentres,
	const TArray<FFixedPoint>& AvoidRadii,
	FFixedVector& OutProjected) const
{
	return ProjectPointToNavFreeForAgent(
		WorldPos, Agent, AvoidCentres, AvoidRadii, OutProjected);
}

FFixedVector USeinNavigation::QueryDirection(const FSeinDirectionQuery& Query) const
{
	// Base default: obstacle-BLIND straight line toward the goal. A safe fallback so any
	// nav answers SOMETHING; obstacle-aware (route-shaped) or field-based (field-shaped)
	// navs override. Zero when already at the goal (degenerate) → "stop".
	FFixedVector Delta = Query.Goal - Query.From;
	Delta.Z = FFixedPoint::Zero;
	if (Delta.SizeSquared() <= FFixedPoint::Epsilon) return FFixedVector::ZeroVector;
	return FFixedVector::GetSafeNormal(Delta);
}

bool USeinNavigation::IsPlacementValid(const FFixedVector& CenterWorld, FFixedPoint /*YawDegrees*/,
	const FSeinExtentsShape& Shape, uint8 /*AgentLayerMask*/) const
{
	// Conservative reject if we have nothing to check against — better to say
	// "can't place" than "always allow" when nav is uninitialized.
	if (!HasRuntimeData()) return false;

	// DEFAULT IMPLEMENTATION CONTRACT:
	// This base implementation samples ONLY the center cell — a single
	// IsPassable call at CenterWorld + Shape.LocalOffset. It does NOT
	// rasterize the full footprint, does NOT account for rotation, and does
	// NOT consult AgentLayerMask. It is intentionally a deterministic scaffold:
	//   - Subclasses (USeinNavigationAStar and game-team replacements)
	//     SHOULD override with proper grid-AABB rasterization that walks the
	//     blocked-cell mask deterministically.
	//   - Float trig (sin/cos) over shape rotation is determinism-fragile
	//     across platforms — the override should use the same fixed-point
	//     primitives the rest of the nav uses.
	//   - The center-only check still catches "placed in middle of a wall" or
	//     "placed off the navmesh" — the easy cases — and lets game teams
	//     ship Phase 3 placement before the override lands.
	const FFixedVector SampleWorld(
		CenterWorld.X + Shape.LocalOffset.X,
		CenterWorld.Y + Shape.LocalOffset.Y,
		CenterWorld.Z);
	return IsPassable(SampleWorld);
}

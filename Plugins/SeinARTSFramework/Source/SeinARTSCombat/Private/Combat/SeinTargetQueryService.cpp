/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinTargetQueryService.cpp
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Implements indexed deterministic acquisition and scorer
 *               policy evaluation.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Combat/SeinTargetQueryService.h"
#include "Combat/SeinCombatMath.h"
#include "Combat/SeinTargetScorer.h"
#include "Components/SeinVitalsComponent.h"
#include "Engine/World.h"
#include "Math/MathLib.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "System/SeinCombatSubsystem.h"
#include "Types/Entity.h"

namespace
{
	const USeinTargetScorer* ResolveScorer(const FSeinTargetQuery& Query)
	{
		if (Query.ScorerClass.IsValid())
		{
			if (UClass* ScorerClass =
				Query.ScorerClass.TryLoadClass<USeinTargetScorer>())
			{
				return GetDefault<USeinTargetScorer>(ScorerClass);
			}
		}
		return GetDefault<USeinTargetScorer>();
	}
}

void FSeinTargetQueryService::FindTargets(
	const USeinWorldSubsystem& World,
	const FSeinTargetQuery& Query,
	TArray<FSeinTargetCandidate>& OutCandidates)
{
	OutCandidates.Reset();
	if (Query.Range <= FFixedPoint::Zero || Query.MaxResults <= 0)
	{
		return;
	}

	FFixedVector Origin = Query.Origin;
	FFixedVector InstigatorForward = FFixedVector::ForwardVector;
	if (Query.Instigator.IsValid())
	{
		const FSeinEntity* InstigatorEntity =
			World.GetEntity(Query.Instigator);
		if (InstigatorEntity)
		{
			if (Origin.IsZero())
			{
				Origin = InstigatorEntity->Transform.GetLocation();
			}
			InstigatorForward =
				InstigatorEntity->Transform.GetRotation().GetForwardVector();
		}
	}

	// Arc gate precompute: dot(forward, delta) >= |delta| * cos(halfAngle)
	// avoids normalizing per-candidate. 180°+ disables the gate.
	const bool bArcGated =
		Query.ArcHalfAngleDegrees < FFixedPoint::FromInt(180)
		&& Query.ArcHalfAngleDegrees >= FFixedPoint::Zero;
	const FFixedPoint CosHalfAngle = bArcGated
		? SeinMath::Cos(Query.ArcHalfAngleDegrees
			* FFixedPoint::Pi / FFixedPoint::FromInt(180))
		: FFixedPoint::Zero;
	FFixedVector PlanarForward = InstigatorForward;
	PlanarForward.Z = FFixedPoint::Zero;

	const FSeinPlayerID ObserverPlayer = Query.Instigator.IsValid()
		? World.GetEntityOwner(Query.Instigator)
		: FSeinPlayerID();
	const USeinTargetScorer* Scorer = ResolveScorer(Query);
	const bool bUsesBuiltInScorer =
		Scorer->GetClass() == USeinTargetScorer::StaticClass();

	TArray<FSeinEntityHandle> PotentialTargets;
	bool bUsedIndex = false;
	if (UWorld* UnrealWorld = World.GetWorld())
	{
		if (const USeinCombatSubsystem* Combat =
			UnrealWorld->GetSubsystem<USeinCombatSubsystem>())
		{
			bUsedIndex = Combat->CollectTargetCandidates(
				World,
				Origin,
				Query.Range,
				Query.Instigator,
				PotentialTargets);
		}
	}
	if (!bUsedIndex)
	{
		PotentialTargets.Reset();
		World.GetEntityPool().ForEachEntity(
			[&](FSeinEntityHandle Handle, const FSeinEntity&)
			{
				if (Handle != Query.Instigator)
				{
					PotentialTargets.Add(Handle);
				}
			});
	}

	for (const FSeinEntityHandle Handle : PotentialTargets)
	{
		if (bUsesBuiltInScorer
			&& Query.Instigator.IsValid()
			&& World.GetEntityOwner(Handle) == ObserverPlayer)
		{
			continue;
		}
		const FSeinEntity* Entity = World.GetEntity(Handle);
		const FSeinVitalsComponent* Vitals =
			World.GetComponent<FSeinVitalsComponent>(Handle);
		if (!Entity || !Vitals || Vitals->Health <= FFixedPoint::Zero)
		{
			continue;
		}
		const FFixedVector Location = Entity->Transform.GetLocation();
		if (!FFixedVector::IsPlanarDistanceWithin(
				Location, Origin, Query.Range))
		{
			continue;
		}
		const FFixedPoint Distance =
			SeinCombatInternal::PlanarDistanceSaturated(Location, Origin);
		if (bArcGated)
		{
			FFixedVector PlanarDelta = Location - Origin;
			PlanarDelta.Z = FFixedPoint::Zero;
			const FFixedPoint Dot =
				PlanarForward.X * PlanarDelta.X
				+ PlanarForward.Y * PlanarDelta.Y;
			if (Dot < Distance * CosHalfAngle)
			{
				continue;
			}
		}
		if (!Query.RequiredTargetTags.IsEmpty())
		{
			bool bHasRequiredTags = true;
			for (const FGameplayTag& Tag : Query.RequiredTargetTags)
			{
				if (!World.HasTag(Handle, Tag))
				{
					bHasRequiredTags = false;
					break;
				}
			}
			if (!bHasRequiredTags)
			{
				continue;
			}
		}
		if (Query.bRequireLineOfSight
			&& World.LineOfSightResolver.IsBound()
			&& !World.LineOfSightResolver.Execute(ObserverPlayer, Location))
		{
			continue;
		}
		if (!bUsesBuiltInScorer
			&& !Scorer->IsValidTarget(&World, Query, Handle))
		{
			continue;
		}

		FSeinTargetCandidate Candidate;
		Candidate.Target = Handle;
		Candidate.Distance = Distance;
		Candidate.Score = bUsesBuiltInScorer
			? Query.Range - Candidate.Distance
			: Scorer->ScoreTarget(&World, Query, Candidate);

		// PotentialTargets is canonical handle order. Inserting after existing
		// equal scores therefore preserves the stable canonical tie-break while
		// bounding storage and sort work to Query.MaxResults.
		int32 InsertAt = 0;
		while (InsertAt < OutCandidates.Num()
			&& OutCandidates[InsertAt].Score >= Candidate.Score)
		{
			++InsertAt;
		}
		if (InsertAt < Query.MaxResults)
		{
			OutCandidates.Insert(Candidate, InsertAt);
			if (OutCandidates.Num() > Query.MaxResults)
			{
				OutCandidates.Pop(EAllowShrinking::No);
			}
		}
	}
}

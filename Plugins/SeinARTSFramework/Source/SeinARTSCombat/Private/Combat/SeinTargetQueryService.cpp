/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTargetQueryService.cpp
 * @brief   Deterministic acquisition sweep + scorer policy evaluation.
 */

#include "Combat/SeinTargetQueryService.h"
#include "Combat/SeinCombatMath.h"
#include "Combat/SeinTargetScorer.h"
#include "Components/SeinVitalsComponent.h"
#include "Math/MathLib.h"
#include "Simulation/SeinWorldSubsystem.h"
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

	World.GetEntityPool().ForEachEntity(
		[&](FSeinEntityHandle Handle, const FSeinEntity& Entity)
		{
			if (Handle == Query.Instigator)
			{
				return;
			}
			const FSeinVitalsComponent* Vitals =
				World.GetComponent<FSeinVitalsComponent>(Handle);
			if (!Vitals || Vitals->Health <= FFixedPoint::Zero)
			{
				return;
			}
			const FFixedVector Location = Entity.Transform.GetLocation();
			if (!FFixedVector::IsPlanarDistanceWithin(
					Location, Origin, Query.Range))
			{
				return;
			}
			const FFixedPoint Distance =
				SeinCombatInternal::PlanarDistanceSaturated(
					Location, Origin);
			if (bArcGated)
			{
				FFixedVector PlanarDelta = Location - Origin;
				PlanarDelta.Z = FFixedPoint::Zero;
				const FFixedPoint Dot =
					PlanarForward.X * PlanarDelta.X
					+ PlanarForward.Y * PlanarDelta.Y;
				if (Dot < Distance * CosHalfAngle)
				{
					return;
				}
			}
			if (!Query.RequiredTargetTags.IsEmpty())
			{
				for (const FGameplayTag& Tag : Query.RequiredTargetTags)
				{
					if (!World.HasTag(Handle, Tag))
					{
						return;
					}
				}
			}
			if (Query.bRequireLineOfSight
				&& World.LineOfSightResolver.IsBound()
				&& !World.LineOfSightResolver.Execute(
					ObserverPlayer, Location))
			{
				return;
			}
			if (!Scorer->IsValidTarget(&World, Query, Handle))
			{
				return;
			}

			FSeinTargetCandidate Candidate;
			Candidate.Target = Handle;
			Candidate.Distance = Distance;
			Candidate.Score = Scorer->ScoreTarget(&World, Query, Candidate);
			OutCandidates.Add(Candidate);
		});

	// Best score first; exact ties keep canonical (ascending-slot) order —
	// the sweep produced canonical order and the sort is stable.
	OutCandidates.StableSort(
		[](const FSeinTargetCandidate& A, const FSeinTargetCandidate& B)
		{
			return A.Score > B.Score;
		});
	if (OutCandidates.Num() > Query.MaxResults)
	{
		OutCandidates.SetNum(Query.MaxResults);
	}
}

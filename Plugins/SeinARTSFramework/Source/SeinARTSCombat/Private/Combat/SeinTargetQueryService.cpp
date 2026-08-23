/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinTargetQueryService.cpp
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @latest       23 Aug 2026
 * @brief        Implements indexed deterministic acquisition, the shared
 *               per-candidate gate chain, and scorer policy evaluation.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Combat/SeinTargetQueryService.h"
#include "Combat/SeinCombatMath.h"
#include "Combat/SeinTargetScorer.h"
#include "Engine/World.h"
#include "Math/MathLib.h"
#include "Simulation/ComponentStorage.h"
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
			// Picker convention: an invalid class falls back to the default
			// AND reports. Silence here would be a desync waiting to happen —
			// one peer that cannot load the Blueprint would acquire with the
			// neutral scorer while the others use the authored one.
			UE_LOG(LogTemp, Error,
				TEXT("FindTargets/CheckTarget: ScorerClass '%s' did not resolve to a USeinTargetScorer subclass; using the built-in neutral scorer."),
				*Query.ScorerClass.ToString());
		}
		return GetDefault<USeinTargetScorer>();
	}

	/** Everything FindTargets and CheckTarget precompute once per query. */
	struct FQueryContext
	{
		FFixedVector Origin = FFixedVector::ZeroVector;
		FFixedVector PlanarForward = FFixedVector::ForwardVector;
		FFixedPoint CosHalfAngle = FFixedPoint::Zero;
		bool bArcGated = false;
		FSeinPlayerID ObserverPlayer;
		/** True only when the instigator is a LIVE entity; a stale handle
		 *  disables the owner gate instead of silently resolving to Neutral
		 *  and rejecting every neutral-owned candidate. */
		bool bHasLiveInstigator = false;
		const USeinTargetScorer* Scorer = nullptr;
		bool bUsesBuiltInScorer = true;
		const ISeinComponentStorage* RequiredStorage = nullptr;
		bool bRequiredComponentUnsatisfiable = false;

		FQueryContext(
			const USeinWorldSubsystem& World, const FSeinTargetQuery& Query)
		{
			if (Query.RequiredComponent)
			{
				RequiredStorage =
					World.GetComponentStorageRaw(Query.RequiredComponent);
				// A required component nobody has ever carried has no storage
				// yet: every candidate fails the gate (never "no gate"), and
				// nothing below is worth computing.
				bRequiredComponentUnsatisfiable = RequiredStorage == nullptr;
				if (bRequiredComponentUnsatisfiable)
				{
					Scorer = GetDefault<USeinTargetScorer>();
					return;
				}
			}

			Origin = Query.Origin;
			FFixedVector InstigatorForward = FFixedVector::ForwardVector;
			if (const FSeinEntity* InstigatorEntity =
				World.GetEntity(Query.Instigator))
			{
				bHasLiveInstigator = true;
				if (Origin.IsZero())
				{
					Origin = InstigatorEntity->Transform.GetLocation();
				}
				InstigatorForward = InstigatorEntity->Transform
					.GetRotation().GetForwardVector();
				ObserverPlayer = World.GetEntityOwner(Query.Instigator);
			}

			// Arc gate precompute: dot(forward, delta) >= |delta| * cos(half)
			// avoids normalizing per-candidate. 180°+ disables the gate.
			bArcGated =
				Query.ArcHalfAngleDegrees < FFixedPoint::FromInt(180)
				&& Query.ArcHalfAngleDegrees >= FFixedPoint::Zero;
			CosHalfAngle = bArcGated
				? SeinMath::Cos(Query.ArcHalfAngleDegrees
					* FFixedPoint::Pi / FFixedPoint::FromInt(180))
				: FFixedPoint::Zero;
			PlanarForward = InstigatorForward;
			PlanarForward.Z = FFixedPoint::Zero;

			Scorer = ResolveScorer(Query);
			bUsesBuiltInScorer =
				Scorer->GetClass() == USeinTargetScorer::StaticClass();
		}
	};

	/** The one gate chain. Order is the contract documented on
	 *  ESeinTargetCheckResult; FindTargets and CheckTarget both run it. */
	ESeinTargetCheckResult EvaluateCandidate(
		const USeinWorldSubsystem& World,
		const FSeinTargetQuery& Query,
		const FQueryContext& Context,
		FSeinEntityHandle Handle,
		FSeinTargetCandidate& OutCandidate)
	{
		if (!Handle.IsValid() || Handle == Query.Instigator)
		{
			return ESeinTargetCheckResult::InvalidTarget;
		}
		// One generation-checked pool lookup; GetEntity already returns null
		// for a deferred-destroy tombstone, which is never a target.
		const FSeinEntity* Entity = World.GetEntity(Handle);
		if (!Entity)
		{
			return ESeinTargetCheckResult::InvalidTarget;
		}
		if (Context.bRequiredComponentUnsatisfiable
			|| (Context.RequiredStorage
				&& !Context.RequiredStorage->HasComponent(Handle)))
		{
			return ESeinTargetCheckResult::MissingComponent;
		}
		const FFixedVector Location = Entity->Transform.GetLocation();
		if (!FFixedVector::IsPlanarDistanceWithin(
				Location, Context.Origin, Query.Range))
		{
			return ESeinTargetCheckResult::OutOfRange;
		}
		// The planar distance needs a fixed-point sqrt — the one expensive
		// step in the chain — so it is taken lazily: only the arc gate and the
		// final score need it, and most rejected candidates (out of range,
		// wrong tags, same owner) must not pay for it.
		FFixedPoint Distance = FFixedPoint::Zero;
		bool bDistanceComputed = false;
		if (Context.bArcGated)
		{
			Distance = SeinCombatInternal::PlanarDistanceSaturated(
				Location, Context.Origin);
			bDistanceComputed = true;
			FFixedVector PlanarDelta = Location - Context.Origin;
			PlanarDelta.Z = FFixedPoint::Zero;
			const FFixedPoint Dot =
				Context.PlanarForward.X * PlanarDelta.X
				+ Context.PlanarForward.Y * PlanarDelta.Y;
			if (Dot < Distance * Context.CosHalfAngle)
			{
				return ESeinTargetCheckResult::OutsideArc;
			}
		}
		for (const FGameplayTag& Tag : Query.RequiredTargetTags)
		{
			if (!World.HasTag(Handle, Tag))
			{
				return ESeinTargetCheckResult::MissingTags;
			}
		}
		if (Query.bRequireLineOfSight
			&& World.LineOfSightResolver.IsBound()
			&& !World.LineOfSightResolver.Execute(
				Context.ObserverPlayer, Location))
		{
			return ESeinTargetCheckResult::NoLineOfSight;
		}
		if (Context.bUsesBuiltInScorer)
		{
			// Built-in fast path: same-owner exclusion, nearer scores higher.
			if (Context.bHasLiveInstigator
				&& World.GetEntityOwner(Handle) == Context.ObserverPlayer)
			{
				return ESeinTargetCheckResult::RejectedByScorer;
			}
		}
		else if (!Context.Scorer->IsValidTarget(&World, Query, Handle))
		{
			return ESeinTargetCheckResult::RejectedByScorer;
		}

		if (!bDistanceComputed)
		{
			Distance = SeinCombatInternal::PlanarDistanceSaturated(
				Location, Context.Origin);
		}
		OutCandidate.Target = Handle;
		OutCandidate.Distance = Distance;
		OutCandidate.Score = Context.bUsesBuiltInScorer
			? Query.Range - Distance
			: Context.Scorer->ScoreTarget(&World, Query, OutCandidate);
		return ESeinTargetCheckResult::Eligible;
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
	const FQueryContext Context(World, Query);
	if (Context.bRequiredComponentUnsatisfiable)
	{
		return;
	}

	TArray<FSeinEntityHandle> PotentialTargets;
	bool bUsedIndex = false;
	if (UWorld* UnrealWorld = World.GetWorld())
	{
		if (const USeinCombatSubsystem* Combat =
			UnrealWorld->GetSubsystem<USeinCombatSubsystem>())
		{
			bUsedIndex = Combat->CollectTargetCandidates(
				World,
				Context.Origin,
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
		FSeinTargetCandidate Candidate;
		if (EvaluateCandidate(World, Query, Context, Handle, Candidate)
			!= ESeinTargetCheckResult::Eligible)
		{
			continue;
		}

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

ESeinTargetCheckResult FSeinTargetQueryService::CheckTarget(
	const USeinWorldSubsystem& World,
	const FSeinTargetQuery& Query,
	FSeinEntityHandle Target,
	FSeinTargetCandidate& OutCandidate)
{
	OutCandidate = FSeinTargetCandidate();
	if (Query.Range <= FFixedPoint::Zero)
	{
		return ESeinTargetCheckResult::OutOfRange;
	}
	const FQueryContext Context(World, Query);
	return EvaluateCandidate(World, Query, Context, Target, OutCandidate);
}

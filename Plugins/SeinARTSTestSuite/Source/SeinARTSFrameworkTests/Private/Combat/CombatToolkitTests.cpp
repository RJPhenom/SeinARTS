/**
 * SeinARTS Test Suite - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    CombatToolkitTests.cpp
 * @brief   Combat toolkit contracts: deterministic target queries over a
 *          designer-owned vitals struct, the per-target Check Target verdict
 *          (and its agreement with Find Targets), the schema-agnostic Apply
 *          Field Delta stat verb, and the presentation notifications that
 *          carry designer-resolved combat outcomes to the render layer.
 *
 *          The framework ships no vitals/weapon/damage schema. Every "health"
 *          here is FSeinTestVitalsComponent — exactly what a game would author.
 */

#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Combat/SeinTargetQueryService.h"
#include "Events/SeinVisualEvent.h"
#include "Lib/SeinCombatBPFL.h"
#include "Lib/SeinCombatMutationBPFL.h"
#include "Lib/SeinSimMutationBPFL.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinCombatTestTypes.h"

struct FSeinWorldSubsystemTestAccess
{
	static bool TickSimulation(USeinWorldSubsystem& World, float DeltaTime)
	{
		return World.TickSimulation(DeltaTime);
	}
};

namespace UE::SeinARTSTests
{
	namespace CombatToolkitTestLocal
	{
		FFixedVector At(int32 X, int32 Y = 0)
		{
			return FFixedVector(
				FFixedPoint::FromInt(X), FFixedPoint::FromInt(Y),
				FFixedPoint::Zero);
		}

		struct FCombatFixture
		{
			FActorTestSpawner Spawner;
			USeinWorldSubsystem* World = nullptr;
			FSeinPlayerID Attacker = FSeinPlayerID(1);
			FSeinPlayerID Defender = FSeinPlayerID(2);

			bool Initialize(
				TFunctionRef<void()> AuthorEntities, uint32 Seed,
				const TCHAR* FixtureId)
			{
				World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
				if (!World) return false;
				FString Error;
				return SeinTestMatchBootstrap::Materialize(
						*World,
						[&]()
						{
							World->RegisterPlayer(Attacker, FSeinFactionID(1));
							World->RegisterPlayer(Defender, FSeinFactionID(2));
							AuthorEntities();
						},
						FSeinMatchSettings(),
						Seed,
						FixtureId,
						&Error)
					&& SeinTestMatchBootstrap::Start(*World, &Error);
			}

			FSeinEntityHandle SpawnVitalsEntity(
				const FFixedVector& Location, FSeinPlayerID Owner,
				int32 MaxHealth = 100)
			{
				const FSeinEntityHandle Handle = World->SpawnAbstractEntity(
					FFixedTransform(Location), Owner);
				World->AddComponent(
					Handle, FSeinTestVitalsComponent::Make(MaxHealth));
				return Handle;
			}

			FFixedPoint Health(FSeinEntityHandle Handle) const
			{
				const FSeinTestVitalsComponent* Vitals =
					World->GetComponent<FSeinTestVitalsComponent>(Handle);
				return Vitals ? Vitals->Health : FFixedPoint::Zero;
			}

			static FSeinTargetQuery VitalsQuery(
				FSeinEntityHandle Instigator, int32 Range, int32 MaxResults = 8)
			{
				FSeinTargetQuery Query;
				Query.Instigator = Instigator;
				Query.Range = FFixedPoint::FromInt(Range);
				Query.MaxResults = MaxResults;
				Query.RequiredComponent =
					FSeinTestVitalsComponent::StaticStruct();
				return Query;
			}

			static bool Contains(
				const TArray<FSeinTargetCandidate>& Candidates,
				FSeinEntityHandle Handle)
			{
				return Candidates.ContainsByPredicate(
					[&](const FSeinTargetCandidate& Candidate)
					{
						return Candidate.Target == Handle;
					});
			}
		};

		struct FFieldDeltaResult
		{
			bool bSucceeded = false;
			FFixedPoint NewValue = FFixedPoint::Zero;
			bool bChanged = false;
			bool bAtMin = false;
			bool bAtMax = false;
		};

		/** Clamped by default (the shape a damage/heal graph uses); pass
		 *  bClampMin/bClampMax=false to exercise the unwired-pin defaults. */
		FFieldDeltaResult ApplyHealthDelta(
			USeinWorldSubsystem& World, FSeinEntityHandle Entity,
			FFixedPoint Delta, FFixedPoint Min, FFixedPoint Max,
			FName Field = TEXT("Health"),
			UScriptStruct* Struct = FSeinTestVitalsComponent::StaticStruct(),
			bool bClampMin = true, bool bClampMax = true)
		{
			FFieldDeltaResult Result;
			Result.bSucceeded = USeinSimMutationBPFL::SeinApplyFieldDelta(
				&World, Entity, Struct, Field, Delta,
				bClampMin, Min, bClampMax, Max,
				Result.NewValue, Result.bChanged, Result.bAtMin, Result.bAtMax);
			return Result;
		}
	}

	TEST(TargetQueriesFilterDeterministicallyAndScoreNearest,
		"SeinARTS.Sim.Combat.Acquisition")
	{
		using namespace CombatToolkitTestLocal;
		FCombatFixture Fixture;
		FSeinEntityHandle Instigator;
		FSeinEntityHandle OwnUnit;
		FSeinEntityHandle NearEnemy;
		FSeinEntityHandle FarEnemy;
		FSeinEntityHandle OutOfRangeEnemy;
		FSeinEntityHandle Vitalless;
		ASSERT_THAT(IsTrue(Fixture.Initialize(
			[&]()
			{
				Instigator = Fixture.SpawnVitalsEntity(At(0), Fixture.Attacker);
				OwnUnit = Fixture.SpawnVitalsEntity(At(200), Fixture.Attacker);
				NearEnemy = Fixture.SpawnVitalsEntity(At(400), Fixture.Defender);
				FarEnemy = Fixture.SpawnVitalsEntity(At(900), Fixture.Defender);
				OutOfRangeEnemy =
					Fixture.SpawnVitalsEntity(At(5000), Fixture.Defender);
				// An enemy entity with NO vitals struct — a marker, a decal
				// anchor, whatever. In range, but not a "thing with vitals".
				Vitalless = Fixture.World->SpawnAbstractEntity(
					FFixedTransform(At(300)), Fixture.Defender);
			},
			0x434D4233, TEXT("SeinARTS.Combat.Acquisition"))));

		FSeinTargetQuery Query = FCombatFixture::VitalsQuery(Instigator, 1500);
		TArray<FSeinTargetCandidate> Candidates;
		FSeinTargetQueryService::FindTargets(
			*Fixture.World, Query, Candidates);
		// Own unit excluded by the neutral scorer; out-of-range excluded by
		// the mechanical gate; the vitals-less entity excluded by the
		// designer's component gate; nearest enemy scores first.
		ASSERT_THAT(AreEqual(2, Candidates.Num()));
		ASSERT_THAT(IsTrue(Candidates[0].Target == NearEnemy));
		ASSERT_THAT(IsTrue(Candidates[1].Target == FarEnemy));
		ASSERT_THAT(IsFalse(FCombatFixture::Contains(Candidates, Vitalless)));

		// With no component gate the candidate universe is every live entity:
		// "what counts as a target" is the query's opinion, not the framework's.
		Query.RequiredComponent = nullptr;
		FSeinTargetQueryService::FindTargets(
			*Fixture.World, Query, Candidates);
		ASSERT_THAT(AreEqual(3, Candidates.Num()));
		ASSERT_THAT(IsTrue(Candidates[0].Target == Vitalless));
		ASSERT_THAT(IsTrue(Candidates[1].Target == NearEnemy));
		ASSERT_THAT(IsTrue(Candidates[2].Target == FarEnemy));
		Query.RequiredComponent = FSeinTestVitalsComponent::StaticStruct();

		Query.MaxResults = 1;
		FSeinTargetQueryService::FindTargets(
			*Fixture.World, Query, Candidates);
		ASSERT_THAT(AreEqual(1, Candidates.Num()));
		ASSERT_THAT(IsTrue(Candidates[0].Target == NearEnemy));

		// A custom scorer still receives every mechanically valid candidate and
		// the bounded result set honors its score rather than the built-in fast path.
		Query.ScorerClass = FSoftClassPath(
			USeinFarthestTargetScorerTestDouble::StaticClass()->GetPathName());
		FSeinTargetQueryService::FindTargets(
			*Fixture.World, Query, Candidates);
		ASSERT_THAT(AreEqual(1, Candidates.Num()));
		ASSERT_THAT(IsTrue(Candidates[0].Target == FarEnemy));
		Query.ScorerClass.Reset();

		// A range spanning more index cells than entities deliberately falls
		// back to the canonical full sweep without changing result semantics.
		Query.Range = FFixedPoint::FromInt(10000);
		Query.MaxResults = 8;
		FSeinTargetQueryService::FindTargets(
			*Fixture.World, Query, Candidates);
		ASSERT_THAT(AreEqual(3, Candidates.Num()));
		ASSERT_THAT(IsTrue(Candidates[0].Target == NearEnemy));
		ASSERT_THAT(IsTrue(Candidates[1].Target == FarEnemy));
		ASSERT_THAT(IsTrue(Candidates[2].Target == OutOfRangeEnemy));
		Query.Range = FFixedPoint::FromInt(1500);

		// Arc gate: a 45° half-angle facing +X keeps +X targets and drops a
		// flanker at +Y.
		FSeinEntityHandle Flanker;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			Flanker = Fixture.SpawnVitalsEntity(At(0, 400), Fixture.Defender);
		}
		Query.MaxResults = 8;
		Query.ArcHalfAngleDegrees = FFixedPoint::FromInt(45);
		FSeinTargetQueryService::FindTargets(
			*Fixture.World, Query, Candidates);
		ASSERT_THAT(AreEqual(2, Candidates.Num()));
		ASSERT_THAT(IsFalse(FCombatFixture::Contains(Candidates, Flanker)));

		// A warmed spatial prefilter must follow canonical position mutation.
		// Moving the formerly-far target nearest changes the first result.
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			FSeinEntity* FarEntity =
				Fixture.World->GetEntityMutable(FarEnemy);
			ASSERT_THAT(IsNotNull(FarEntity));
			FarEntity->Transform.SetLocation(At(100));
		}
		Query.ArcHalfAngleDegrees = FFixedPoint::FromInt(180);
		FSeinTargetQueryService::FindTargets(
			*Fixture.World, Query, Candidates);
		ASSERT_THAT(IsTrue(Candidates[0].Target == FarEnemy));

		// Stat values do not affect index membership, so a designer's
		// "don't target corpses" scorer rule must stay a live narrow-phase gate
		// while the position cache is warm.
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			FSeinTestVitalsComponent* Vitals =
				Fixture.World->GetComponentMutable<FSeinTestVitalsComponent>(
					FarEnemy);
			ASSERT_THAT(IsNotNull(Vitals));
			Vitals->Health = FFixedPoint::Zero;
		}
		// Built-in scorer: health means nothing to the framework; the
		// zero-health entity is still a candidate.
		FSeinTargetQueryService::FindTargets(
			*Fixture.World, Query, Candidates);
		ASSERT_THAT(IsTrue(Candidates[0].Target == FarEnemy));
		// Designer scorer: excluded, live.
		Query.ScorerClass = FSoftClassPath(
			USeinLivingVitalsTargetScorerTestDouble::StaticClass()
				->GetPathName());
		FSeinTargetQueryService::FindTargets(
			*Fixture.World, Query, Candidates);
		ASSERT_THAT(IsTrue(Candidates[0].Target == NearEnemy));
		ASSERT_THAT(IsFalse(FCombatFixture::Contains(Candidates, FarEnemy)));
		ASSERT_THAT(IsFalse(FCombatFixture::Contains(Candidates, OwnUnit)));

		// Removing the required component from a warm-indexed entity drops it
		// immediately — component presence is a live gate, not index membership.
		Query.ScorerClass.Reset();
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			Fixture.World->RemoveComponent<FSeinTestVitalsComponent>(NearEnemy);
		}
		FSeinTargetQueryService::FindTargets(
			*Fixture.World, Query, Candidates);
		ASSERT_THAT(IsFalse(FCombatFixture::Contains(Candidates, NearEnemy)));
		Fixture.World->StopSimulation();
	}

	TEST(TargetQueriesFallbackExactlyAtFixedPointBounds,
		"SeinARTS.Sim.Combat.Acquisition.FixedPointBounds")
	{
		using namespace CombatToolkitTestLocal;
		FCombatFixture Fixture;
		FSeinEntityHandle Instigator;
		FSeinEntityHandle Enemy;
		const FFixedPoint OriginX =
			FFixedPoint::MaxValue - FFixedPoint::FromInt(1000);
		const FFixedPoint EnemyX =
			FFixedPoint::MaxValue - FFixedPoint::FromInt(500);
		ASSERT_THAT(IsTrue(Fixture.Initialize(
			[&]()
			{
				Instigator = Fixture.SpawnVitalsEntity(
					FFixedVector(OriginX, FFixedPoint::Zero, FFixedPoint::Zero),
					Fixture.Attacker);
				Enemy = Fixture.SpawnVitalsEntity(
					FFixedVector(EnemyX, FFixedPoint::Zero, FFixedPoint::Zero),
					Fixture.Defender);
			},
			0x434D4237, TEXT("SeinARTS.Combat.Acquisition.Bounds"))));

		FSeinTargetQuery Query = FCombatFixture::VitalsQuery(Instigator, 1500, 1);
		TArray<FSeinTargetCandidate> Candidates;
		FSeinTargetQueryService::FindTargets(
			*Fixture.World, Query, Candidates);
		ASSERT_THAT(AreEqual(1, Candidates.Num()));
		ASSERT_THAT(IsTrue(Candidates[0].Target == Enemy));
		Fixture.World->StopSimulation();
	}

	TEST(CheckTargetReportsTheFirstFailingGateAndAgreesWithFindTargets,
		"SeinARTS.Sim.Combat.CheckTarget")
	{
		using namespace CombatToolkitTestLocal;
		FCombatFixture Fixture;
		FSeinEntityHandle Instigator;
		FSeinEntityHandle OwnUnit;
		FSeinEntityHandle NearEnemy;
		FSeinEntityHandle FarEnemy;
		FSeinEntityHandle Flanker;
		FSeinEntityHandle Vitalless;
		ASSERT_THAT(IsTrue(Fixture.Initialize(
			[&]()
			{
				Instigator = Fixture.SpawnVitalsEntity(At(0), Fixture.Attacker);
				OwnUnit = Fixture.SpawnVitalsEntity(At(200), Fixture.Attacker);
				NearEnemy = Fixture.SpawnVitalsEntity(At(400), Fixture.Defender);
				FarEnemy = Fixture.SpawnVitalsEntity(At(5000), Fixture.Defender);
				Flanker = Fixture.SpawnVitalsEntity(At(0, 400), Fixture.Defender);
				Vitalless = Fixture.World->SpawnAbstractEntity(
					FFixedTransform(At(300)), Fixture.Defender);
				Fixture.World->AddBaseTag(
					NearEnemy, SeinARTSTags::State_UnderConstruction);
			},
			0x434D4243, TEXT("SeinARTS.Combat.CheckTarget"))));

		FSeinTargetQuery Query = FCombatFixture::VitalsQuery(Instigator, 1500);
		FSeinTargetCandidate Candidate;

		// Eligible, with the same distance/score FindTargets would report.
		ASSERT_THAT(IsTrue(
			FSeinTargetQueryService::CheckTarget(
				*Fixture.World, Query, NearEnemy, Candidate)
			== ESeinTargetCheckResult::Eligible));
		ASSERT_THAT(IsTrue(Candidate.Target == NearEnemy));
		// Fixed-point sqrt lands within an LSB or so of the exact planar
		// distance; the built-in score is exactly Range - Distance.
		const int64 DistanceErrorRaw =
			(Candidate.Distance - FFixedPoint::FromInt(400)).Value;
		ASSERT_THAT(IsTrue(
			DistanceErrorRaw < FFixedPoint::FromInt(1).Value / 1000
			&& DistanceErrorRaw > -(FFixedPoint::FromInt(1).Value / 1000)));
		ASSERT_THAT(IsTrue(
			Candidate.Score == Query.Range - Candidate.Distance));

		// Gate order: invalid → component → range → arc → tags → LoS → scorer.
		ASSERT_THAT(IsTrue(
			FSeinTargetQueryService::CheckTarget(
				*Fixture.World, Query, FSeinEntityHandle(), Candidate)
			== ESeinTargetCheckResult::InvalidTarget));
		ASSERT_THAT(IsTrue(
			FSeinTargetQueryService::CheckTarget(
				*Fixture.World, Query, Instigator, Candidate)
			== ESeinTargetCheckResult::InvalidTarget));
		ASSERT_THAT(IsTrue(
			FSeinTargetQueryService::CheckTarget(
				*Fixture.World, Query, Vitalless, Candidate)
			== ESeinTargetCheckResult::MissingComponent));
		ASSERT_THAT(IsTrue(
			FSeinTargetQueryService::CheckTarget(
				*Fixture.World, Query, FarEnemy, Candidate)
			== ESeinTargetCheckResult::OutOfRange));
		Query.ArcHalfAngleDegrees = FFixedPoint::FromInt(45);
		ASSERT_THAT(IsTrue(
			FSeinTargetQueryService::CheckTarget(
				*Fixture.World, Query, Flanker, Candidate)
			== ESeinTargetCheckResult::OutsideArc));
		Query.ArcHalfAngleDegrees = FFixedPoint::FromInt(180);
		Query.RequiredTargetTags.AddTag(SeinARTSTags::State_UnderConstruction);
		ASSERT_THAT(IsTrue(
			FSeinTargetQueryService::CheckTarget(
				*Fixture.World, Query, Flanker, Candidate)
			== ESeinTargetCheckResult::MissingTags));
		ASSERT_THAT(IsTrue(
			FSeinTargetQueryService::CheckTarget(
				*Fixture.World, Query, NearEnemy, Candidate)
			== ESeinTargetCheckResult::Eligible));
		Query.RequiredTargetTags.Reset();

		// Fog LoS: a bound resolver that denies everything produces
		// NoLineOfSight; bRequireLineOfSight=false bypasses it.
		Fixture.World->LineOfSightResolver.BindLambda(
			[](FSeinPlayerID, const FFixedVector&) { return false; });
		ASSERT_THAT(IsTrue(
			FSeinTargetQueryService::CheckTarget(
				*Fixture.World, Query, NearEnemy, Candidate)
			== ESeinTargetCheckResult::NoLineOfSight));
		Query.bRequireLineOfSight = false;
		ASSERT_THAT(IsTrue(
			FSeinTargetQueryService::CheckTarget(
				*Fixture.World, Query, NearEnemy, Candidate)
			== ESeinTargetCheckResult::Eligible));
		Query.bRequireLineOfSight = true;
		Fixture.World->LineOfSightResolver.Unbind();

		// Scorer policy is last: the built-in rejects same-owner; a designer
		// scorer rejects by its own rule.
		ASSERT_THAT(IsTrue(
			FSeinTargetQueryService::CheckTarget(
				*Fixture.World, Query, OwnUnit, Candidate)
			== ESeinTargetCheckResult::RejectedByScorer));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			Fixture.World->GetComponentMutable<FSeinTestVitalsComponent>(
				NearEnemy)->Health = FFixedPoint::Zero;
		}
		Query.ScorerClass = FSoftClassPath(
			USeinLivingVitalsTargetScorerTestDouble::StaticClass()
				->GetPathName());
		ASSERT_THAT(IsTrue(
			FSeinTargetQueryService::CheckTarget(
				*Fixture.World, Query, NearEnemy, Candidate)
			== ESeinTargetCheckResult::RejectedByScorer));
		Query.ScorerClass.Reset();

		// Agreement contract: an entity is Eligible under Check Target exactly
		// when Find Targets (unbounded) returns it — under every gate shape.
		const FSeinEntityHandle Everyone[] = {
			Instigator, OwnUnit, NearEnemy, FarEnemy, Flanker, Vitalless};
		const auto AssertAgreement = [&](const FSeinTargetQuery& Shape)
		{
			FSeinTargetQuery Unbounded = Shape;
			Unbounded.MaxResults = 64;
			TArray<FSeinTargetCandidate> Found;
			FSeinTargetQueryService::FindTargets(
				*Fixture.World, Unbounded, Found);
			for (const FSeinEntityHandle Handle : Everyone)
			{
				FSeinTargetCandidate Checked;
				const bool bEligible =
					FSeinTargetQueryService::CheckTarget(
						*Fixture.World, Unbounded, Handle, Checked)
					== ESeinTargetCheckResult::Eligible;
				const bool bFound = FCombatFixture::Contains(Found, Handle);
				ASSERT_THAT(AreEqual(bFound, bEligible));
				if (bEligible)
				{
					const FSeinTargetCandidate* FoundCandidate =
						Found.FindByPredicate(
							[&](const FSeinTargetCandidate& Candidate)
							{
								return Candidate.Target == Handle;
							});
					ASSERT_THAT(IsNotNull(FoundCandidate));
					ASSERT_THAT(IsTrue(
						FoundCandidate->Distance == Checked.Distance));
					ASSERT_THAT(IsTrue(FoundCandidate->Score == Checked.Score));
				}
			}
		};
		AssertAgreement(Query);
		{
			FSeinTargetQuery Shape = Query;
			Shape.ArcHalfAngleDegrees = FFixedPoint::FromInt(45);
			AssertAgreement(Shape);
		}
		{
			FSeinTargetQuery Shape = Query;
			Shape.RequiredComponent = nullptr;
			Shape.Range = FFixedPoint::FromInt(10000);
			AssertAgreement(Shape);
		}
		{
			FSeinTargetQuery Shape = Query;
			Shape.ScorerClass = FSoftClassPath(
				USeinFarthestTargetScorerTestDouble::StaticClass()
					->GetPathName());
			AssertAgreement(Shape);
		}
		Fixture.World->StopSimulation();
	}

	TEST(ApplyFieldDeltaSaturatesClampsAndReportsBounds,
		"SeinARTS.Sim.Combat.FieldDelta")
	{
		using namespace CombatToolkitTestLocal;
		FCombatFixture Fixture;
		FSeinEntityHandle Unit;
		FSeinEntityHandle Bare;
		ASSERT_THAT(IsTrue(Fixture.Initialize(
			[&]()
			{
				Unit = Fixture.SpawnVitalsEntity(At(0), Fixture.Defender, 100);
				Bare = Fixture.World->SpawnAbstractEntity(
					FFixedTransform(At(100)), Fixture.Defender);
			},
			0x434D4244, TEXT("SeinARTS.Combat.FieldDelta"))));

		const FFixedPoint Zero = FFixedPoint::Zero;
		const FFixedPoint Hundred = FFixedPoint::FromInt(100);
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);

			// Plain damage.
			FFieldDeltaResult R = ApplyHealthDelta(
				*Fixture.World, Unit, FFixedPoint::FromInt(-30), Zero, Hundred);
			ASSERT_THAT(IsTrue(R.bSucceeded));
			ASSERT_THAT(IsTrue(R.bChanged));
			ASSERT_THAT(IsTrue(R.NewValue == FFixedPoint::FromInt(70)));
			ASSERT_THAT(IsFalse(R.bAtMin));
			ASSERT_THAT(IsFalse(R.bAtMax));
			ASSERT_THAT(IsTrue(Fixture.Health(Unit) == FFixedPoint::FromInt(70)));

			// Overkill clamps to the floor and reports it — the designer's
			// "hit zero" cue. The framework does NOT destroy anything.
			R = ApplyHealthDelta(
				*Fixture.World, Unit, FFixedPoint::FromInt(-500), Zero, Hundred);
			ASSERT_THAT(IsTrue(R.bSucceeded));
			ASSERT_THAT(IsTrue(R.bChanged));
			ASSERT_THAT(IsTrue(R.NewValue == Zero));
			ASSERT_THAT(IsTrue(R.bAtMin));
			ASSERT_THAT(IsTrue(Fixture.World->IsEntityAlive(Unit)));

			// Further damage at the floor is a no-op: succeeds, reports the
			// bound, writes nothing.
			const uint64 RevisionBefore = Fixture.World
				->GetComponentStorageRaw(FSeinTestVitalsComponent::StaticStruct())
				->GetMutationRevision(Unit);
			R = ApplyHealthDelta(
				*Fixture.World, Unit, FFixedPoint::FromInt(-10), Zero, Hundred);
			ASSERT_THAT(IsTrue(R.bSucceeded));
			ASSERT_THAT(IsFalse(R.bChanged));
			ASSERT_THAT(IsTrue(R.bAtMin));
			ASSERT_THAT(IsTrue(R.NewValue == Zero));
			ASSERT_THAT(AreEqual(RevisionBefore, Fixture.World
				->GetComponentStorageRaw(FSeinTestVitalsComponent::StaticStruct())
				->GetMutationRevision(Unit)));

			// Heal clamps to the ceiling.
			R = ApplyHealthDelta(
				*Fixture.World, Unit, FFixedPoint::FromInt(1000), Zero, Hundred);
			ASSERT_THAT(IsTrue(R.bSucceeded));
			ASSERT_THAT(IsTrue(R.bChanged));
			ASSERT_THAT(IsTrue(R.NewValue == Hundred));
			ASSERT_THAT(IsTrue(R.bAtMax));
			ASSERT_THAT(IsFalse(R.bAtMin));

			// Unwired-pin defaults (both clamp flags off) are a plain
			// saturating add — never a silent zeroing: 100 - 25 = 75 with
			// Min/Max left at their zero defaults, and no bound is reported.
			R = ApplyHealthDelta(
				*Fixture.World, Unit, FFixedPoint::FromInt(-25), Zero, Zero,
				TEXT("Health"), FSeinTestVitalsComponent::StaticStruct(),
				/*bClampMin=*/false, /*bClampMax=*/false);
			ASSERT_THAT(IsTrue(R.bSucceeded));
			ASSERT_THAT(IsTrue(R.NewValue == FFixedPoint::FromInt(75)));
			ASSERT_THAT(IsFalse(R.bAtMin));
			ASSERT_THAT(IsFalse(R.bAtMax));
			// A single enabled clamp reports only its own bound: floor at 0
			// with the ceiling off lands exactly on 0 -> bAtMin, not bAtMax.
			R = ApplyHealthDelta(
				*Fixture.World, Unit, FFixedPoint::FromInt(-75), Zero, Zero,
				TEXT("Health"), FSeinTestVitalsComponent::StaticStruct(),
				/*bClampMin=*/true, /*bClampMax=*/false);
			ASSERT_THAT(IsTrue(R.NewValue == Zero));
			ASSERT_THAT(IsTrue(R.bAtMin));
			ASSERT_THAT(IsFalse(R.bAtMax));
			// Unclamped, the field can pass zero freely.
			R = ApplyHealthDelta(
				*Fixture.World, Unit, FFixedPoint::FromInt(-5), Zero, Zero,
				TEXT("Health"), FSeinTestVitalsComponent::StaticStruct(),
				false, false);
			ASSERT_THAT(IsTrue(R.NewValue == FFixedPoint::FromInt(-5)));
			ASSERT_THAT(IsFalse(R.bAtMin));

			// Saturation, unclamped: from -5, one +MaxValue lands exactly on
			// MaxValue - 5 (no overflow yet); the next pins at MaxValue instead
			// of wrapping negative; a third is a no-op.
			R = ApplyHealthDelta(
				*Fixture.World, Unit, FFixedPoint::MaxValue, Zero, Zero,
				TEXT("Health"), FSeinTestVitalsComponent::StaticStruct(),
				false, false);
			ASSERT_THAT(IsTrue(R.bSucceeded));
			ASSERT_THAT(IsTrue(R.NewValue
				== FFixedPoint::MaxValue - FFixedPoint::FromInt(5)));
			R = ApplyHealthDelta(
				*Fixture.World, Unit, FFixedPoint::MaxValue, Zero, Zero,
				TEXT("Health"), FSeinTestVitalsComponent::StaticStruct(),
				false, false);
			ASSERT_THAT(IsTrue(R.bSucceeded));
			ASSERT_THAT(IsTrue(R.bChanged));
			ASSERT_THAT(IsTrue(R.NewValue == FFixedPoint::MaxValue));
			R = ApplyHealthDelta(
				*Fixture.World, Unit, FFixedPoint::MaxValue, Zero, Zero,
				TEXT("Health"), FSeinTestVitalsComponent::StaticStruct(),
				false, false);
			ASSERT_THAT(IsTrue(R.bSucceeded));
			ASSERT_THAT(IsFalse(R.bChanged));
			ASSERT_THAT(IsTrue(R.NewValue == FFixedPoint::MaxValue));
			ASSERT_THAT(IsFalse(R.bAtMax)); // no clamp enabled -> no bound reported
			R = ApplyHealthDelta(
				*Fixture.World, Unit, FFixedPoint::MinValue, Zero, Zero,
				TEXT("Health"), FSeinTestVitalsComponent::StaticStruct(),
				false, false);
			ASSERT_THAT(IsTrue(R.NewValue == FFixedPoint(static_cast<int64>(-1))));
			R = ApplyHealthDelta(
				*Fixture.World, Unit, FFixedPoint::MinValue, Zero, Zero,
				TEXT("Health"), FSeinTestVitalsComponent::StaticStruct(),
				false, false);
			ASSERT_THAT(IsTrue(R.NewValue == FFixedPoint::MinValue));
			// Enabled clamp at the exact saturation bound reports it.
			R = ApplyHealthDelta(
				*Fixture.World, Unit, FFixedPoint::MinValue,
				FFixedPoint::MinValue, FFixedPoint::MaxValue);
			ASSERT_THAT(IsFalse(R.bChanged));
			ASSERT_THAT(IsTrue(R.bAtMin));

			// A second fixed-point field on the same struct is independent.
			R = ApplyHealthDelta(
				*Fixture.World, Unit, FFixedPoint::FromInt(50), Zero,
				FFixedPoint::FromInt(150), TEXT("MaxHealth"));
			ASSERT_THAT(IsTrue(R.bSucceeded));
			ASSERT_THAT(IsTrue(R.NewValue == FFixedPoint::FromInt(150)));
			ASSERT_THAT(IsTrue(R.bAtMax));

			// Rejections: inverted clamp, non-fixed-point field, unknown field,
			// entity without the component, dead/invalid entity.
			TestRunner->AddExpectedMessage(
				TEXT("ApplyFieldDelta: MinValue exceeds MaxValue"),
				ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
			R = ApplyHealthDelta(
				*Fixture.World, Unit, FFixedPoint::One, Hundred, Zero);
			ASSERT_THAT(IsFalse(R.bSucceeded));
			TestRunner->AddExpectedMessage(
				TEXT("has no fixed-point field named Armor"),
				ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
			R = ApplyHealthDelta(
				*Fixture.World, Unit, FFixedPoint::One, Zero, Hundred,
				TEXT("Armor"));
			ASSERT_THAT(IsFalse(R.bSucceeded));
			TestRunner->AddExpectedMessage(
				TEXT("has no fixed-point field named Nope"),
				ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
			R = ApplyHealthDelta(
				*Fixture.World, Unit, FFixedPoint::One, Zero, Hundred,
				TEXT("Nope"));
			ASSERT_THAT(IsFalse(R.bSucceeded));
			TestRunner->AddExpectedMessage(
				TEXT("has no SeinTestVitalsComponent"),
				ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
			R = ApplyHealthDelta(
				*Fixture.World, Bare, FFixedPoint::One, Zero, Hundred);
			ASSERT_THAT(IsFalse(R.bSucceeded));
			R = ApplyHealthDelta(
				*Fixture.World, FSeinEntityHandle(), FFixedPoint::One, Zero,
				Hundred);
			ASSERT_THAT(IsFalse(R.bSucceeded));
		}

		// Outside an authorized simulation context the verb is refused and
		// state is untouched — same gate as every other mutation node.
		const FFixedPoint Before = Fixture.Health(Unit);
		TestRunner->AddExpectedError(
			TEXT("ApplyFieldDelta rejected outside bootstrap Applying or deterministic simulation context"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		const FFieldDeltaResult Refused = ApplyHealthDelta(
			*Fixture.World, Unit, FFixedPoint::FromInt(-1), Zero, Hundred);
		ASSERT_THAT(IsFalse(Refused.bSucceeded));
		ASSERT_THAT(IsTrue(Fixture.Health(Unit) == Before));
		Fixture.World->StopSimulation();
	}

	TEST(CombatNotificationsRouteDesignerOutcomesToPresentation,
		"SeinARTS.Sim.Combat.Notify")
	{
		using namespace CombatToolkitTestLocal;
		FCombatFixture Fixture;
		FSeinEntityHandle Shooter;
		FSeinEntityHandle Victim;
		ASSERT_THAT(IsTrue(Fixture.Initialize(
			[&]()
			{
				Shooter = Fixture.SpawnVitalsEntity(At(0), Fixture.Attacker);
				Victim = Fixture.SpawnVitalsEntity(At(500), Fixture.Defender);
			},
			0x434D4245, TEXT("SeinARTS.Combat.Notify"))));

		// Drain whatever bootstrap queued so the assertions below see only
		// the notifications under test.
		Fixture.World->FlushVisualEvents();
		const FGameplayTag Tag = SeinARTSTags::State_UnderConstruction;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			ASSERT_THAT(IsTrue(USeinCombatMutationBPFL::SeinNotifyDamageApplied(
				Fixture.World, Victim, Shooter, FFixedPoint::FromInt(30), Tag)));
			ASSERT_THAT(IsTrue(USeinCombatMutationBPFL::SeinNotifyHealApplied(
				Fixture.World, Victim, FSeinEntityHandle(),
				FFixedPoint::FromInt(5), Tag)));
			ASSERT_THAT(IsTrue(USeinCombatMutationBPFL::SeinNotifyDeath(
				Fixture.World, Victim, Shooter)));
			// Notifying death does not destroy: the designer decides.
			ASSERT_THAT(IsTrue(Fixture.World->IsEntityAlive(Victim)));
			// A dead/invalid target is refused, loudly (ordering mistake).
			TestRunner->AddExpectedMessage(
				TEXT("NotifyDamageApplied: entity"),
				ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
			ASSERT_THAT(IsFalse(USeinCombatMutationBPFL::SeinNotifyDamageApplied(
				Fixture.World, FSeinEntityHandle(), Shooter,
				FFixedPoint::One, Tag)));
		}
		const TArray<FSeinVisualEvent> Events =
			Fixture.World->FlushVisualEvents();
		ASSERT_THAT(AreEqual(4, Events.Num()));
		ASSERT_THAT(IsTrue(Events[0].Type == ESeinVisualEventType::DamageApplied));
		ASSERT_THAT(IsTrue(Events[0].PrimaryEntity == Victim));
		ASSERT_THAT(IsTrue(Events[0].SecondaryEntity == Shooter));
		ASSERT_THAT(IsTrue(Events[0].Value == FFixedPoint::FromInt(30)));
		ASSERT_THAT(IsTrue(Events[0].Tag == Tag));
		ASSERT_THAT(IsTrue(Events[1].Type == ESeinVisualEventType::HealApplied));
		ASSERT_THAT(IsTrue(Events[1].Value == FFixedPoint::FromInt(5)));
		ASSERT_THAT(IsTrue(Events[2].Type == ESeinVisualEventType::Death));
		ASSERT_THAT(IsTrue(Events[2].PrimaryEntity == Victim));
		ASSERT_THAT(IsTrue(Events[2].SecondaryEntity == Shooter));
		ASSERT_THAT(IsTrue(Events[3].Type == ESeinVisualEventType::Kill));
		ASSERT_THAT(IsTrue(Events[3].PrimaryEntity == Shooter));
		ASSERT_THAT(IsTrue(Events[3].SecondaryEntity == Victim));
		ASSERT_THAT(IsTrue(Events[3].PlayerID == Fixture.Attacker));

		// Death without a live killer emits no Kill attribution.
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			ASSERT_THAT(IsTrue(USeinCombatMutationBPFL::SeinNotifyDeath(
				Fixture.World, Victim, FSeinEntityHandle())));
		}
		const TArray<FSeinVisualEvent> Unattributed =
			Fixture.World->FlushVisualEvents();
		ASSERT_THAT(AreEqual(1, Unattributed.Num()));
		ASSERT_THAT(IsTrue(Unattributed[0].Type == ESeinVisualEventType::Death));

		// Outside an authorized context the notification is refused.
		TestRunner->AddExpectedError(
			TEXT("NotifyDamageApplied rejected outside bootstrap Applying or deterministic simulation context"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(USeinCombatMutationBPFL::SeinNotifyDamageApplied(
			Fixture.World, Victim, Shooter, FFixedPoint::One, Tag)));
		ASSERT_THAT(IsFalse(Fixture.World->HasPendingVisualEvents()));
		Fixture.World->StopSimulation();
	}
}

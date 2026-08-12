#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Formations/SeinFormation.h"
#include "Math/MathLib.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		void ReferenceSeparatePositions(
			const TArray<FFixedPoint>& Radii,
			TArray<FFixedVector>& Positions,
			int32 MaxIterations)
		{
			const int32 N = Positions.Num();
			if (N < 2) return;
			const FFixedPoint Eps =
				FFixedPoint::One / FFixedPoint::FromInt(100);
			for (int32 Iter = 0; Iter < MaxIterations; ++Iter)
			{
				bool bMoved = false;
				for (int32 i = 0; i < N; ++i)
				{
					for (int32 j = i + 1; j < N; ++j)
					{
						FFixedVector D = Positions[j] - Positions[i];
						D.Z = FFixedPoint::Zero;
						const FFixedPoint DistSq =
							D.X * D.X + D.Y * D.Y;
						const FFixedPoint Ri = Radii.IsValidIndex(i)
							? Radii[i] : FFixedPoint::Zero;
						const FFixedPoint Rj = Radii.IsValidIndex(j)
							? Radii[j] : FFixedPoint::Zero;
						const FFixedPoint MinDist =
							Ri + Rj + FFixedPoint::FromInt(25);
						if (DistSq >= MinDist * MinDist) continue;

						FFixedPoint Dist = SeinMath::Sqrt(DistSq);
						FFixedVector Dir;
						if (Dist > Eps)
						{
							Dir = FFixedVector(
								D.X / Dist, D.Y / Dist,
								FFixedPoint::Zero);
						}
						else
						{
							const FFixedPoint Ang = FFixedPoint::TwoPi
								* FFixedPoint::FromInt((i * 7 + j) % 16)
								/ FFixedPoint::FromInt(16);
							Dir = FFixedVector(
								SeinMath::Cos(Ang), SeinMath::Sin(Ang),
								FFixedPoint::Zero);
							Dist = FFixedPoint::Zero;
						}
						const FFixedPoint Push =
							(MinDist - Dist) / FFixedPoint::Two;
						Positions[i].X = Positions[i].X - Dir.X * Push;
						Positions[i].Y = Positions[i].Y - Dir.Y * Push;
						Positions[j].X = Positions[j].X + Dir.X * Push;
						Positions[j].Y = Positions[j].Y + Dir.Y * Push;
						bMoved = true;
					}
				}
				if (!bMoved) break;
			}
		}

		bool MatchesReference(
			const TArray<FFixedPoint>& Radii,
			const TArray<FFixedVector>& Input)
		{
			TArray<FFixedVector> Expected = Input;
			TArray<FFixedVector> Actual = Input;
			ReferenceSeparatePositions(Radii, Expected, 16);
			USeinFormation::SeparatePositions(Radii, Actual, 16);
			return Actual == Expected;
		}
	}

	TEST(FormationSeparationBroadphaseIsBitExact,
		"SeinARTS.Unit.Formation")
	{
		TArray<FFixedPoint> Radii;
		TArray<FFixedVector> Grid;
		Radii.Reserve(100);
		Grid.Reserve(100);
		for (int32 i = 0; i < 100; ++i)
		{
			Radii.Add(FFixedPoint::FromInt(50 + (i % 3) * 5));
			Grid.Add(FFixedVector(
				FFixedPoint::FromInt((i / 10) * 105 - 500),
				FFixedPoint::FromInt((i % 10) * 105 - 500),
				FFixedPoint::FromInt(75)));
		}
		ASSERT_THAT(IsTrue(MatchesReference(Radii, Grid)));

		TArray<FFixedVector> Ring;
		Ring.Reserve(100);
		for (int32 i = 0; i < 100; ++i)
		{
			const FFixedPoint Angle = FFixedPoint::TwoPi
				* FFixedPoint::FromInt(i) / FFixedPoint::FromInt(100);
			Ring.Add(FFixedVector(
				FFixedPoint::FromInt(900) * SeinMath::Cos(Angle),
				FFixedPoint::FromInt(900) * SeinMath::Sin(Angle),
				FFixedPoint::Zero));
		}
		ASSERT_THAT(IsTrue(MatchesReference(Radii, Ring)));
	}

	TEST(FormationSeparationCoincidentCacheIsBitExact,
		"SeinARTS.Unit.Formation")
	{
		TArray<FFixedPoint> Radii;
		Radii.Init(FFixedPoint::FromInt(47), 73);

		TArray<FFixedVector> First;
		First.Init(FFixedVector(
			FFixedPoint::FromInt(1234), FFixedPoint::FromInt(-567),
			FFixedPoint::FromInt(80)), Radii.Num());
		ASSERT_THAT(IsTrue(MatchesReference(Radii, First)));

		// Same footprint signature at a different anchor exercises the cached
		// translation-invariant offsets, not a second relaxation solve.
		TArray<FFixedVector> Shifted;
		Shifted.Init(FFixedVector(
			FFixedPoint::FromInt(-4321), FFixedPoint::FromInt(876),
			FFixedPoint::FromInt(125)), Radii.Num());
		ASSERT_THAT(IsTrue(MatchesReference(Radii, Shifted)));
	}

	TEST(FormationProjectionRoutesExactGroupExclusions,
		"SeinARTS.Unit.Formation")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const TArray<FSeinEntityHandle> Members = {
			FSeinEntityHandle(1, 1),
			FSeinEntityHandle(2, 1),
		};
		const TArray<FFixedPoint> Radii = {
			FFixedPoint::FromInt(25),
			FFixedPoint::FromInt(30),
		};
		TArray<FFixedVector> Positions = {
			FFixedVector(FFixedPoint::FromInt(100),
				FFixedPoint::FromInt(200), FFixedPoint::Zero),
			FFixedVector(FFixedPoint::FromInt(300),
				FFixedPoint::FromInt(400), FFixedPoint::Zero),
		};

		int32 GroupPassabilityCalls = 0;
		int32 GroupProjectionCalls = 0;
		int32 LegacyPassabilityCalls = 0;
		int32 LegacyProjectionCalls = 0;
		bool bObservedExactMembers = true;
		auto ObserveMembers = [&](const TSet<FSeinEntityHandle>& Ignored)
		{
			bObservedExactMembers = bObservedExactMembers
				&& Ignored.Num() == Members.Num()
				&& Ignored.Contains(Members[0])
				&& Ignored.Contains(Members[1]);
		};

		World->AgentDynamicPassableIgnoringResolver.BindLambda(
			[&](const FSeinNavAgentProfile&,
				const FFixedVector&,
				const TSet<FSeinEntityHandle>& Ignored)
			{
				++GroupPassabilityCalls;
				ObserveMembers(Ignored);
				return false;
			});
		World->NavProjectAgentFreeIgnoringResolver.BindLambda(
			[&](const FSeinNavAgentProfile&,
				const FFixedVector& InWorld,
				const TSet<FSeinEntityHandle>& Ignored,
				const TArray<FFixedVector>&,
				const TArray<FFixedPoint>&,
				FFixedVector& OutProjected)
			{
				++GroupProjectionCalls;
				ObserveMembers(Ignored);
				OutProjected = InWorld;
				OutProjected.X += FFixedPoint::FromInt(10);
				return true;
			});
		World->AgentDynamicPassableResolver.BindLambda(
			[&](const FSeinNavAgentProfile&, const FFixedVector&)
			{
				++LegacyPassabilityCalls;
				return false;
			});
		World->NavProjectAgentFreeResolver.BindLambda(
			[&](const FSeinNavAgentProfile&,
				const FFixedVector& InWorld,
				const TArray<FFixedVector>&,
				const TArray<FFixedPoint>&,
				FFixedVector& OutProjected)
			{
				++LegacyProjectionCalls;
				OutProjected = InWorld;
				return true;
			});

		const TArray<FFixedVector> Original = Positions;
		USeinFormation::ProjectPositionsToNavigable(
			World, Radii, Positions, Members);

		ASSERT_THAT(AreEqual(2, GroupPassabilityCalls));
		ASSERT_THAT(AreEqual(2, GroupProjectionCalls));
		ASSERT_THAT(AreEqual(0, LegacyPassabilityCalls));
		ASSERT_THAT(AreEqual(0, LegacyProjectionCalls));
		ASSERT_THAT(IsTrue(bObservedExactMembers));
		ASSERT_THAT(IsTrue(
			Positions[0].X
				== Original[0].X + FFixedPoint::FromInt(10)));
		ASSERT_THAT(IsTrue(
			Positions[1].X
				== Original[1].X + FFixedPoint::FromInt(10)));
	}
}

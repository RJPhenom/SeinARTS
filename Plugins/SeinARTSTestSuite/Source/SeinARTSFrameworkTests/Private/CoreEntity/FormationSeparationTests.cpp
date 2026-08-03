#include "CQTest.h"

#include "Formations/SeinFormation.h"
#include "Math/MathLib.h"

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
}

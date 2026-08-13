#include "CQTest.h"
#include "Lib/MathBPFL.h"
#include "Math/MathLib.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Stamping/SeinStampUtils.h"
#include "Types/FixedPoint.h"
#include "Types/Quat.h"
#include "Types/Vector.h"

#include <limits>

namespace UE::SeinARTSTests
{
	static_assert(TStructOpsTypeTraits<FFixedPoint>::WithSerializer,
		"FFixedPoint must retain its native serializer to avoid tagged-struct asset serialization.");
	static_assert(sizeof(FFixedPoint) == sizeof(int64),
		"FFixedPoint's native serialization contract is exactly one raw int64.");

	TEST(FixedPointNativeSerializerIsRawInt64, "SeinARTS.Unit.Core.FixedPoint.Serialization")
	{
		FFixedPoint Source(0x0123456789ABCDEFLL);
		TArray<uint8> Bytes;
		{
			FMemoryWriter Writer(Bytes, /*bIsPersistent=*/true);
			ASSERT_THAT(IsTrue(Source.Serialize(Writer)));
			ASSERT_THAT(IsFalse(Writer.IsError()));
		}

		ASSERT_THAT(AreEqual(static_cast<int32>(sizeof(int64)), Bytes.Num()));
#if PLATFORM_LITTLE_ENDIAN
		const TArray<uint8> ExpectedBytes =
		{
			0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01
		};
#else
		const TArray<uint8> ExpectedBytes =
		{
			0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF
		};
#endif
		ASSERT_THAT(IsTrue(Bytes == ExpectedBytes));

		FFixedPoint Restored;
		{
			FMemoryReader Reader(Bytes, /*bIsPersistent=*/true);
			ASSERT_THAT(IsTrue(Restored.Serialize(Reader)));
			ASSERT_THAT(IsFalse(Reader.IsError()));
		}
		ASSERT_THAT(AreEqual(Source.Value, Restored.Value));
	}

	TEST(FixedPointWrapIsDefined, "SeinARTS.Unit.Core")
	{
		ASSERT_THAT(IsTrue((FFixedPoint::MaxValue + FFixedPoint::SmallNumber) == FFixedPoint::MinValue));
		ASSERT_THAT(IsTrue((FFixedPoint::MinValue - FFixedPoint::SmallNumber) == FFixedPoint::MaxValue));
		ASSERT_THAT(IsTrue(-FFixedPoint::MinValue == FFixedPoint::MinValue));

		FFixedPoint Compound = FFixedPoint::MaxValue;
		Compound += FFixedPoint::SmallNumber;
		ASSERT_THAT(IsTrue(Compound == FFixedPoint::MinValue));
		Compound -= FFixedPoint::SmallNumber;
		ASSERT_THAT(IsTrue(Compound == FFixedPoint::MaxValue));

		ASSERT_THAT(AreEqual(-4294967296LL,
			(FFixedPoint::MaxValue * FFixedPoint::MaxValue).Value));
		ASSERT_THAT(AreEqual(-4294967295LL,
			(FFixedPoint::MaxValue / FFixedPoint::MinValue).Value));
		ASSERT_THAT(AreEqual(-4294967296LL,
			(FFixedPoint::MinValue / FFixedPoint::MaxValue).Value));
		ASSERT_THAT(AreEqual(0LL,
			(FFixedPoint::MinValue / FFixedPoint(1)).Value));

		ASSERT_THAT(AreEqual(FFixedPoint::FromInt(-1).Value,
			FFixedPoint::FromInt64(INT64_MAX).Value));
		ASSERT_THAT(AreEqual(INT32_MIN, FFixedPoint::FromInt(INT32_MIN).ToInt()));
		ASSERT_THAT(AreEqual(-1, FFixedPoint(-1).ToInt()));
		ASSERT_THAT(AreEqual(INT32_MIN, FFixedPoint::MaxValue.CeilToInt()));
		ASSERT_THAT(IsFalse(FFixedPoint::MaxValue.IsNearlyEqual(FFixedPoint::MinValue)));
	}

	TEST(FixedPointFloatBoundariesAreDefined, "SeinARTS.Unit.Core")
	{
		const float NaN = std::numeric_limits<float>::quiet_NaN();
		const float Infinity = std::numeric_limits<float>::infinity();

		ASSERT_THAT(IsTrue(FFixedPoint::FromFloat(NaN) == FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(FFixedPoint::FromFloat(Infinity) == FFixedPoint::MaxValue));
		ASSERT_THAT(IsTrue(FFixedPoint::FromFloat(-Infinity) == FFixedPoint::MinValue));
		ASSERT_THAT(IsTrue(FFixedPoint::FromFloat(2147483648.0f) == FFixedPoint::MaxValue));
		ASSERT_THAT(IsTrue(FFixedPoint::FromFloat(-2147483648.0f) == FFixedPoint::MinValue));
	}

	TEST(FixedVectorSaturatedMagnitudeHandlesLongRange,
		"SeinARTS.Unit.Core")
	{
		const FFixedVector InRange(
			FFixedPoint::FromInt(30000),
			FFixedPoint::FromInt(30000),
			FFixedPoint::Zero);
		ASSERT_THAT(IsTrue(
			InRange.SizeSquaredSaturated()
				== FFixedPoint::FromInt(1800000000)));
		const FFixedVector FractionalBoundary(
			FFixedPoint::FromInt(46340) + FFixedPoint::Half,
			FFixedPoint::Zero,
			FFixedPoint::Zero);
		ASSERT_THAT(IsTrue(
			FractionalBoundary.SizeSquaredSaturated()
				== FractionalBoundary.SizeSquared()));

		const FFixedVector LongAxis(
			FFixedPoint::FromInt(50000),
			FFixedPoint::Zero,
			FFixedPoint::Zero);
		ASSERT_THAT(IsTrue(
			LongAxis.SizeSquared() < FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			LongAxis.SizeSquaredSaturated() == FFixedPoint::MaxValue));
		ASSERT_THAT(IsTrue(
			LongAxis.Size() == FFixedPoint::FromInt(50000)));
		ASSERT_THAT(IsTrue(
			LongAxis.GetNormalized() == FFixedVector(
				FFixedPoint::One,
				FFixedPoint::Zero,
				FFixedPoint::Zero)));

		const FFixedVector LongDiagonal(
			FFixedPoint::FromInt(50000),
			FFixedPoint::FromInt(5000),
			FFixedPoint::Zero);
		const FFixedVector Normalized = LongDiagonal.GetNormalized();
		ASSERT_THAT(IsTrue(SeinMath::Abs(
			Normalized.SizeSquared() - FFixedPoint::One)
			< FFixedPoint::KindaSmallNumber));

		const FFixedVector MaximumDiagonal(
			FFixedPoint::MaxValue,
			FFixedPoint::MaxValue,
			FFixedPoint::Zero);
		const FFixedVector MaximumNormalized =
			MaximumDiagonal.GetNormalized();
		ASSERT_THAT(IsTrue(SeinMath::Abs(
			MaximumNormalized.SizeSquared() - FFixedPoint::One)
			< FFixedPoint::KindaSmallNumber));
		ASSERT_THAT(IsTrue(MaximumNormalized.X < FFixedPoint::One));
		ASSERT_THAT(IsTrue(MaximumNormalized.Y < FFixedPoint::One));

		const FFixedVector MinimumEndpoint(
			FFixedPoint::MinValue,
			FFixedPoint::Zero,
			FFixedPoint::Zero);
		const FFixedVector MaximumEndpoint(
			FFixedPoint::MaxValue,
			FFixedPoint::Zero,
			FFixedPoint::Zero);
		ASSERT_THAT(IsTrue(FFixedVector::DistSquaredSaturated(
			MinimumEndpoint, MaximumEndpoint) == FFixedPoint::MaxValue));
		ASSERT_THAT(IsTrue(FFixedVector::DistanceSaturated(
			MinimumEndpoint, MaximumEndpoint) == FFixedPoint::MaxValue));
		ASSERT_THAT(IsTrue(FFixedVector::GetSafeNormalDifference(
			MinimumEndpoint, MaximumEndpoint) == FFixedVector::ForwardVector));
		ASSERT_THAT(IsTrue(FFixedVector::GetSafeNormalDifference(
			MaximumEndpoint, MinimumEndpoint) == FFixedVector::BackwardVector));

		const FFixedVector DeltaA(
			FFixedPoint::FromInt(-20000),
			FFixedPoint::FromInt(10000),
			FFixedPoint::Zero);
		const FFixedVector DeltaB(
			FFixedPoint::FromInt(20000),
			FFixedPoint::FromInt(20000),
			FFixedPoint::Zero);
		ASSERT_THAT(IsTrue(FFixedVector::DistSquaredSaturated(
			DeltaA, DeltaB) == FFixedPoint::FromInt(1700000000)));
		ASSERT_THAT(IsTrue(FFixedVector::SquareSaturated(
			FFixedPoint::FromInt(50000)) == FFixedPoint::MaxValue));
		ASSERT_THAT(IsTrue(FFixedVector::SquareSaturated(
			FFixedPoint::FromInt(46340))
			== FFixedPoint::FromInt(2147395600)));

		const FFixedVector Origin = FFixedVector::ZeroVector;
		const FFixedVector FortyThousand(
			FFixedPoint::FromInt(40000), FFixedPoint::Zero, FFixedPoint::Zero);
		const FFixedVector OneHundredThousand(
			FFixedPoint::FromInt(100000), FFixedPoint::Zero, FFixedPoint::Zero);
		const FFixedPoint FiftyThousand = FFixedPoint::FromInt(50000);
		ASSERT_THAT(IsTrue(FFixedVector::IsDistanceWithin(
			Origin, FortyThousand, FiftyThousand)));
		ASSERT_THAT(IsFalse(FFixedVector::IsDistanceWithin(
			Origin, OneHundredThousand, FiftyThousand)));
		ASSERT_THAT(IsFalse(FFixedVector::IsDistanceWithin(
			MinimumEndpoint, MaximumEndpoint, FFixedPoint::MaxValue)));
	}

	TEST(FixedPointNegativeRounding, "SeinARTS.Unit.Core")
	{
		const FFixedPoint MinusOneQuarter = -FFixedPoint::One - FFixedPoint::Quarter;
		const FFixedPoint MinusOneHalf = -FFixedPoint::One - FFixedPoint::Half;
		const FFixedPoint MinusOneThreeQuarters = -FFixedPoint::One - FFixedPoint::ThreeQuarters;

		ASSERT_THAT(IsTrue(SeinMath::Floor(MinusOneQuarter) == FFixedPoint::FromInt(-2)));
		ASSERT_THAT(IsTrue(SeinMath::Ceil(MinusOneQuarter) == FFixedPoint::FromInt(-1)));
		ASSERT_THAT(IsTrue(SeinMath::Round(MinusOneQuarter) == FFixedPoint::FromInt(-1)));
		ASSERT_THAT(IsTrue(SeinMath::Round(MinusOneHalf) == FFixedPoint::FromInt(-1)));
		ASSERT_THAT(IsTrue(SeinMath::Round(MinusOneThreeQuarters) == FFixedPoint::FromInt(-2)));
		ASSERT_THAT(AreEqual(-1, SeinStampUtils::FloorToInt(-FFixedPoint::SmallNumber)));

		ASSERT_THAT(IsTrue(SeinMath::Abs(FFixedPoint::MinValue) == FFixedPoint::MaxValue));
		ASSERT_THAT(IsTrue(SeinMath::Mod(FFixedPoint::MinValue, FFixedPoint(-1)) == FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(SeinMath::Pow(FFixedPoint::One, INT32_MIN) == FFixedPoint::One));
		ASSERT_THAT(IsTrue(SeinMath::Pow(FFixedPoint::Two, -32) == FFixedPoint::SmallNumber));
		ASSERT_THAT(IsTrue(SeinMath::Atan(FFixedPoint::MinValue) < FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(SeinMath::Asin(FFixedPoint::MinValue) == FFixedPoint::Zero));
	}

	TEST(FixedPointPartsPreserveRawBits, "SeinARTS.Unit.Core")
	{
		const FFixedPoint Original(INT64_MIN + 0x12345678LL);
		int32 IntegerPart = 0;
		int32 FractionPart = 0;
		UMathBPFL::BreakFixedPointToParts(Original, IntegerPart, FractionPart);
		ASSERT_THAT(IsTrue(UMathBPFL::MakeFixedPointFromParts(IntegerPart, FractionPart) == Original));
	}

	TEST(FixedQuaternionNormalizationIsBounded, "SeinARTS.Unit.Core")
	{
		ASSERT_THAT(IsTrue(FFixedQuaternion::Identity.GetNormalized() == FFixedQuaternion::Identity));

		const FFixedQuaternion Large(
			FFixedPoint::MaxValue, FFixedPoint::MaxValue,
			FFixedPoint::Zero, FFixedPoint::Zero);
		const FFixedQuaternion Normalized = Large.GetNormalized();
		ASSERT_THAT(IsTrue(SeinMath::Abs(
			Normalized.SizeSquared() - FFixedPoint::One) < FFixedPoint::KindaSmallNumber));
		ASSERT_THAT(IsTrue(Normalized.X > FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(Normalized.Y > FFixedPoint::Zero));
	}
}

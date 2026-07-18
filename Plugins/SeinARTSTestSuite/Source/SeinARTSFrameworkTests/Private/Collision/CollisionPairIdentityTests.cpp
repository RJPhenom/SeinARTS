#include "CQTest.h"
#include "Collision/SeinCollisionResolver.h"
#include "Collision/SeinCollisionSpatialHash.h"

namespace UE::SeinARTSTests
{
	struct FCollisionResolverTestAccess : USeinCollisionResolver
	{
		static bool PairKeysEqual(
			FSeinEntityHandle A, FSeinEntityHandle B,
			FSeinEntityHandle C, FSeinEntityHandle D)
		{
			return MakePairKey(A, B) == MakePairKey(C, D);
		}
	};

	TEST(CollisionPairGenerationIdentity, "SeinARTS.Unit.Collision")
	{
		const FSeinEntityHandle A1(3, 1);
		const FSeinEntityHandle A2(3, 2);
		const FSeinEntityHandle B1(9, 1);

		ASSERT_THAT(IsTrue(FCollisionResolverTestAccess::PairKeysEqual(A1, B1, B1, A1)));
		ASSERT_THAT(IsFalse(FCollisionResolverTestAccess::PairKeysEqual(A1, B1, A2, B1)));
	}

	TEST(CollisionSpatialHashKeepsNegativeCellsDistinct, "SeinARTS.Unit.Collision")
	{
		FSeinCollisionSpatialHash Hash;
		Hash.Initialize(FFixedPoint::FromInt(10), FFixedVector::ZeroVector);
		const FSeinEntityHandle NegativeHandle(1, 1);
		const FSeinEntityHandle PositiveHandle(2, 1);
		Hash.InsertStatic(NegativeHandle,
			FFixedVector(FFixedPoint::FromInt(-5), FFixedPoint::Zero, FFixedPoint::Zero),
			FFixedPoint::Zero);
		Hash.InsertStatic(PositiveHandle,
			FFixedVector(FFixedPoint::FromInt(5), FFixedPoint::Zero, FFixedPoint::Zero),
			FFixedPoint::Zero);

		TArray<FSeinEntityHandle> Results;
		Hash.QueryRadius(
			FFixedVector(FFixedPoint::FromInt(-5), FFixedPoint::Zero, FFixedPoint::Zero),
			FFixedPoint::One, Results);
		ASSERT_THAT(AreEqual(1, Results.Num()));
		ASSERT_THAT(IsTrue(Results[0] == NegativeHandle));
	}
}

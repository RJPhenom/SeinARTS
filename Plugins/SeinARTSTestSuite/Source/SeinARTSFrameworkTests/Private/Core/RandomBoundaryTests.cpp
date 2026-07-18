#include "CQTest.h"
#include "Types/Random.h"

namespace UE::SeinARTSTests
{
	TEST(RandomSignedResultsPreserveBits, "SeinARTS.Unit.Core")
	{
		FFixedRandom Bits32(12345);
		FFixedRandom Signed32(12345);
		ASSERT_THAT(AreEqual(BitCast<int32>(Bits32.Next32()), Signed32.Int()));

		FFixedRandom Bits64(67890);
		FFixedRandom Signed64(67890);
		ASSERT_THAT(AreEqual(BitCast<int64>(Bits64.Next64()), Signed64.Int64()));
	}

	TEST(RandomFullIntRangeIsDefined, "SeinARTS.Unit.Core")
	{
		FFixedRandom ExpectedRandom(54321);
		FFixedRandom RangeRandom(54321);
		const int64 Expected = static_cast<int64>(INT32_MIN) + ExpectedRandom.Next32();
		ASSERT_THAT(AreEqual(static_cast<int32>(Expected),
			RangeRandom.IntRange(INT32_MIN, INT32_MAX)));
	}
}

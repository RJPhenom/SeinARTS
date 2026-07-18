#include "CQTest.h"
#include "Types/FixedPoint.h"

namespace UE::SeinARTSTests
{
	TEST(FixedPointArithmetic, "SeinARTS.Unit.Core")
	{
		const FFixedPoint Product = FFixedPoint::FromInt(-7) * FFixedPoint::FromInt(6);
		const FFixedPoint Quotient = Product / FFixedPoint::FromInt(3);

		ASSERT_THAT(AreEqual(-14, Quotient.ToInt()));
		ASSERT_THAT(AreEqual(FFixedPoint::FromInt(-14).Value, Quotient.Value));
	}
}

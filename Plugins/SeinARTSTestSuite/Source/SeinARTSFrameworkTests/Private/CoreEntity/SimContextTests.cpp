#include "CQTest.h"
#include "Core/SeinSimContext.h"

namespace UE::SeinARTSTests
{
	TEST(SimContextScopeRestoresNestedState, "SeinARTS.Unit.CoreEntity")
	{
		SeinSetSimContext(false);
		ASSERT_THAT(IsFalse(SEIN_IS_SIM_CONTEXT()));

		{
			FSeinSimContextScope Outer;
			ASSERT_THAT(IsTrue(SEIN_IS_SIM_CONTEXT()));
			{
				FSeinSimContextScope Inner;
				ASSERT_THAT(IsTrue(SEIN_IS_SIM_CONTEXT()));
			}
			ASSERT_THAT(IsTrue(SEIN_IS_SIM_CONTEXT()));
		}

		ASSERT_THAT(IsFalse(SEIN_IS_SIM_CONTEXT()));
	}
}

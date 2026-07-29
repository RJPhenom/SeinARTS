#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace UE::SeinARTSTests
{
	TEST(SimContextScopeRestoresNestedState, "SeinARTS.Unit.CoreEntity")
	{
		FActorTestSpawner OuterSpawner;
		FActorTestSpawner InnerSpawner;
		USeinWorldSubsystem* OuterWorld =
			OuterSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		USeinWorldSubsystem* InnerWorld =
			InnerSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(OuterWorld));
		ASSERT_THAT(IsNotNull(InnerWorld));

		ASSERT_THAT(IsFalse(SEIN_IS_SIM_CONTEXT()));

		{
			auto Outer = FSeinSimContextTestAccess::Enter(*OuterWorld);
			ASSERT_THAT(IsTrue(SEIN_IS_SIM_CONTEXT()));
			ASSERT_THAT(IsTrue(SeinIsInSimContext(OuterWorld)));
			ASSERT_THAT(IsFalse(SeinIsInSimContext(InnerWorld)));
			{
				auto Inner = FSeinSimContextTestAccess::Enter(*InnerWorld);
				ASSERT_THAT(IsTrue(SEIN_IS_SIM_CONTEXT()));
				ASSERT_THAT(IsFalse(SeinIsInSimContext(OuterWorld)));
				ASSERT_THAT(IsTrue(SeinIsInSimContext(InnerWorld)));
			}
			ASSERT_THAT(IsTrue(SEIN_IS_SIM_CONTEXT()));
			ASSERT_THAT(IsTrue(SeinIsInSimContext(OuterWorld)));
		}

		ASSERT_THAT(IsFalse(SEIN_IS_SIM_CONTEXT()));
	}
}

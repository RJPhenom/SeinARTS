#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Formations/SeinBoxFormation.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "TestTypes/SeinFormationExecutionTestTypes.h"

namespace UE::SeinARTSTests
{
	TEST(StockFormationExecutesOnReusableStatelessScratch,
		"SeinARTS.Unit.Formation.ExecutionContract")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, []() {})));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		FSeinFormationLayout First;
		FSeinFormationLayout Second;
		ESeinFormationFacing Facing = ESeinFormationFacing::Uniform;
		FString Error;
		const TArray<FSeinEntityHandle> EmptyMembers;
		const FSeinOrderTarget Target;
		ASSERT_THAT(IsTrue(USeinFormation::ExecuteStateless(
			World, USeinBoxFormation::StaticClass(), EmptyMembers,
			Target, First, Facing, &Error)));
		ASSERT_THAT(IsTrue(Error.IsEmpty()));
		ASSERT_THAT(IsTrue(USeinFormation::ExecuteStateless(
			World, USeinBoxFormation::StaticClass(), EmptyMembers,
			Target, Second, Facing, &Error)));
		ASSERT_THAT(IsTrue(World->IsExecutionTopologyValid()));
	}

	TEST(UnadmittedNativeFormationFailsClosed,
		"SeinARTS.Unit.Formation.ExecutionContract")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, []() {})));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		FSeinFormationLayout Layout;
		ESeinFormationFacing Facing = ESeinFormationFacing::Uniform;
		FString Error;
		TestRunner->AddExpectedError(
			TEXT("inherited the stateless claim"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(USeinFormation::ExecuteStateless(
			World,
			USeinUnadmittedNativeFormationTest::StaticClass(),
			{}, {}, Layout, Facing, &Error)));
		ASSERT_THAT(IsFalse(Error.IsEmpty()));
		ASSERT_THAT(IsFalse(World->IsExecutionTopologyValid()));
	}

	TEST(ReflectedFormationMutationFailStopsAndNeverTouchesCDO,
		"SeinARTS.Unit.Formation.ExecutionContract")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, []() {})));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		const USeinMutatingFormationTest* Defaults =
			GetDefault<USeinMutatingFormationTest>();
		ASSERT_THAT(IsNotNull(Defaults));
		ASSERT_THAT(AreEqual(0, Defaults->InvocationCount));

		FSeinFormationLayout Layout;
		ESeinFormationFacing Facing = ESeinFormationFacing::Uniform;
		FString Error;
		TestRunner->AddExpectedError(
			TEXT("mutated reflected member"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(USeinFormation::ExecuteStateless(
			World,
			USeinMutatingFormationTest::StaticClass(),
			{}, {}, Layout, Facing, &Error)));
		ASSERT_THAT(IsFalse(Error.IsEmpty()));
		ASSERT_THAT(AreEqual(0, Defaults->InvocationCount));
		ASSERT_THAT(IsFalse(World->IsExecutionTopologyValid()));
	}
}

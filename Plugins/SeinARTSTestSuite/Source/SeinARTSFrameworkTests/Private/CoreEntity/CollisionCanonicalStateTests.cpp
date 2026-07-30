#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Containers/Ticker.h"

#include "Collision/SeinCollisionResolver.h"
#include "Collision/SeinCollisionResolverDefault.h"
#include "Collision/SeinCollisionResolverParallel.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "TestTypes/SeinCollisionStateTestTypes.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		/** Swap the configured collision resolver class for one test. The
		 *  spawner must be created AFTER this so world initialization reads
		 *  the override; the destructor restores the shipped setting. */
		struct FScopedCollisionResolverClassOverride
		{
			explicit FScopedCollisionResolverClassOverride(
				const UClass* ResolverClass)
				: Settings(
					GetMutableDefault<USeinARTSCoreSettings>())
				, SavedResolverClass(
					Settings
						? Settings->CollisionResolverClass
						: FSoftClassPath())
			{
				check(Settings);
				check(ResolverClass);
				Settings->CollisionResolverClass =
					FSoftClassPath(ResolverClass);
			}

			~FScopedCollisionResolverClassOverride()
			{
				Settings->CollisionResolverClass =
					SavedResolverClass;
			}

			USeinARTSCoreSettings* Settings = nullptr;
			FSoftClassPath SavedResolverClass;
		};

		bool StartCollisionStateWorld(
			USeinWorldSubsystem& World,
			FName FixtureId,
			FString& OutError)
		{
			return SeinTestMatchBootstrap::Materialize(
				World,
				FSeinMatchSettings(),
				0x434F4C53,
				FixtureId,
				&OutError)
				&& SeinTestMatchBootstrap::Start(World, &OutError);
		}
	}

	TEST(BootstrapRejectsCustomCollisionResolverWithoutExactStateClaim,
		"SeinARTS.Unit.CoreEntity.CollisionCanonicalState")
	{
		FScopedCollisionResolverClassOverride ResolverClass(
			USeinUnclaimedCollisionResolverTestDouble::StaticClass());
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Cast<
			USeinUnclaimedCollisionResolverTestDouble>(
				World->GetCollisionResolver())));

		TestRunner->AddExpectedError(
			TEXT("does not explicitly claim exact mutable-state coverage"),
			EAutomationExpectedErrorFlags::Contains,
			2,
			false);
		TestRunner->AddExpectedError(
			TEXT("SeinMatchBootstrap: transaction closed (failed)"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		FString Error;
		ASSERT_THAT(IsFalse(StartCollisionStateWorld(
			*World,
			TEXT("CollisionState.UnclaimedBootstrap"),
			Error)));
		ASSERT_THAT(IsTrue(Error.Contains(
			TEXT("does not explicitly claim exact mutable-state coverage"))));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinMatchBootstrapState::Failed),
			static_cast<uint8>(World->GetMatchBootstrapState())));
		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
	}

	TEST(BootstrapAcceptsClaimedNativeCollisionResolverSubclass,
		"SeinARTS.Unit.CoreEntity.CollisionCanonicalState")
	{
		FScopedCollisionResolverClassOverride ResolverClass(
			USeinClaimedCollisionResolverTestDouble::StaticClass());
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Cast<
			USeinClaimedCollisionResolverTestDouble>(
				World->GetCollisionResolver())));

		FString Error;
		ASSERT_THAT(IsTrue(StartCollisionStateWorld(
			*World,
			TEXT("CollisionState.ClaimedBootstrap"),
			Error)));
		ASSERT_THAT(IsTrue(
			World->GetCanonicalStateContractDigest().IsValid()));
		ASSERT_THAT(IsTrue(World->IsSimulationRunning()));
		World->StopSimulation();
	}

	TEST(PostFreezeParallelResolverTuningMutationFailStops,
		"SeinARTS.Unit.CoreEntity.CollisionCanonicalState")
	{
		FScopedCollisionResolverClassOverride ResolverClass(
			USeinCollisionResolverParallel::StaticClass());
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		USeinCollisionResolverParallel* Resolver = Cast<
			USeinCollisionResolverParallel>(
				World->GetCollisionResolver());
		ASSERT_THAT(IsNotNull(Resolver));

		FString Error;
		ASSERT_THAT(IsTrue(StartCollisionStateWorld(
			*World,
			TEXT("CollisionState.TuningFailStop"),
			Error)));

		// A post-freeze tuning edit changes the resolution-config digest the
		// per-tick binding recapture recomputes from live values.
		++Resolver->NumPasses;

		TestRunner->AddExpectedError(
			TEXT("Canonical StateContract world bindings changed"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		const int32 TickBefore = World->GetCurrentTick();
		FTSTicker::GetCoreTicker().Tick(
			World->GetFixedDeltaTimeSeconds());

		ASSERT_THAT(AreEqual(TickBefore, World->GetCurrentTick()));
		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
		ASSERT_THAT(IsFalse(World->IsExecutionTopologyValid()));
	}
}

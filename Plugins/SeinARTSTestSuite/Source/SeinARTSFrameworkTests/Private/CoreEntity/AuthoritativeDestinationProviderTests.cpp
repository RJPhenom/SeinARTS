#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Containers/Ticker.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace
{
	FSeinAuthoritativeDestinationProviderResolver MakeProvider(
		TFunction<bool(const FSeinAuthoritativeDestinationQuery&)> Callback)
	{
		FSeinAuthoritativeDestinationProviderResolver Resolver;
		Resolver.BindLambda(MoveTemp(Callback));
		return Resolver;
	}

	USeinWorldSubsystem& MakeIsolatedWorld(FActorTestSpawner& Spawner)
	{
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		check(World);
		World->ClearAuthoritativeDestinationProvidersForTests();
		return *World;
	}
}

namespace UE::SeinARTSTests
{
	TEST(AuthoritativeDestinationProvidersComposeInCanonicalOrder,
		"SeinARTS.Unit.CoreEntity.AuthoritativeDestinationProviders")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem& World = MakeIsolatedWorld(Spawner);
		TArray<FString> Calls;
		const FSeinEntityHandle Requester(17, 3);
		const FFixedVector Destination(
			FFixedPoint::FromInt(100),
			FFixedPoint::FromInt(200),
			FFixedPoint::Zero);

		uint64 ZuluToken = 0;
		FString Error;
		ASSERT_THAT(IsTrue(World.RegisterAuthoritativeDestinationProvider(
			"seinarts.test.zulu",
			1,
			MakeProvider([&](const FSeinAuthoritativeDestinationQuery& Query)
			{
				Calls.Add(TEXT("zulu"));
				return Query.Requester == Requester
					&& Query.WorldPosition == Destination;
			}),
			ZuluToken,
			&Error)));

		uint64 AlphaToken = 0;
		ASSERT_THAT(IsTrue(World.RegisterAuthoritativeDestinationProvider(
			"SeinARTS.Test.Alpha",
			2,
			MakeProvider([&](const FSeinAuthoritativeDestinationQuery& Query)
			{
				Calls.Add(TEXT("alpha"));
				return Query.Requester.IsValid() && false;
			}),
			AlphaToken,
			&Error)));

		ASSERT_THAT(IsTrue(World.HasAuthoritativeDestinationProviders()));
		ASSERT_THAT(IsTrue(World.IsAuthoritativeDestination(
			Destination, Requester)));
		ASSERT_THAT(AreEqual(2, Calls.Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("alpha")), Calls[0]));
		ASSERT_THAT(AreEqual(FString(TEXT("zulu")), Calls[1]));
		ASSERT_THAT(IsTrue(AlphaToken != 0 && ZuluToken != 0));
	}

	TEST(AuthoritativeDestinationProviderTokensRemoveExactRegistrations,
		"SeinARTS.Unit.CoreEntity.AuthoritativeDestinationProviders")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem& World = MakeIsolatedWorld(Spawner);
		FString Error;
		uint64 FalseToken = 0;
		uint64 TrueToken = 0;
		ASSERT_THAT(IsTrue(World.RegisterAuthoritativeDestinationProvider(
			"seinarts.test.false",
			1,
			MakeProvider([](const FSeinAuthoritativeDestinationQuery&)
			{
				return false;
			}),
			FalseToken,
			&Error)));
		ASSERT_THAT(IsTrue(World.RegisterAuthoritativeDestinationProvider(
			"seinarts.test.true",
			1,
			MakeProvider([](const FSeinAuthoritativeDestinationQuery&)
			{
				return true;
			}),
			TrueToken,
			&Error)));

		ASSERT_THAT(IsTrue(World.UnregisterAuthoritativeDestinationProvider(
			FalseToken, &Error)));
		ASSERT_THAT(IsTrue(World.IsAuthoritativeDestination(FFixedVector())));
		ASSERT_THAT(IsFalse(World.UnregisterAuthoritativeDestinationProvider(
			FalseToken, &Error)));
		ASSERT_THAT(IsTrue(World.IsExecutionTopologyValid()));
		ASSERT_THAT(IsTrue(World.UnregisterAuthoritativeDestinationProvider(
			TrueToken, &Error)));
		ASSERT_THAT(IsFalse(World.HasAuthoritativeDestinationProviders()));
		ASSERT_THAT(IsFalse(World.IsAuthoritativeDestination(FFixedVector())));
	}

	TEST(AuthoritativeDestinationLegacyFallbackRemainsCompatible,
		"SeinARTS.Unit.CoreEntity.AuthoritativeDestinationProviders")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem& World = MakeIsolatedWorld(Spawner);
		const FFixedVector Accepted(
			FFixedPoint::FromInt(5),
			FFixedPoint::FromInt(7),
			FFixedPoint::Zero);
		World.AuthoritativeDestinationResolver.BindLambda(
			[Accepted](const FFixedVector& Position)
			{
				return Position == Accepted;
			});

		ASSERT_THAT(IsTrue(World.HasAuthoritativeDestinationProviders()));
		ASSERT_THAT(IsTrue(World.IsAuthoritativeDestination(Accepted)));
		ASSERT_THAT(IsFalse(World.IsAuthoritativeDestination(FFixedVector())));
	}

	TEST(DuplicateAuthoritativeDestinationProviderIdsFailClosed,
		"SeinARTS.Unit.CoreEntity.AuthoritativeDestinationProviders")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem& World = MakeIsolatedWorld(Spawner);
		FString Error;
		uint64 FirstToken = 0;
		uint64 DuplicateToken = 0;
		TestRunner->AddExpectedError(
			TEXT("Execution topology invalid: Duplicate authoritative-destination provider ID"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		ASSERT_THAT(IsTrue(World.RegisterAuthoritativeDestinationProvider(
			"seinarts.test.duplicate",
			1,
			MakeProvider([](const FSeinAuthoritativeDestinationQuery&)
			{
				return false;
			}),
			FirstToken,
			&Error)));
		ASSERT_THAT(IsFalse(World.RegisterAuthoritativeDestinationProvider(
			"SeinARTS.Test.Duplicate",
			1,
			MakeProvider([](const FSeinAuthoritativeDestinationQuery&)
			{
				return true;
			}),
			DuplicateToken,
			&Error)));
		ASSERT_THAT(AreEqual(uint64(0), DuplicateToken));
		ASSERT_THAT(IsFalse(World.IsExecutionTopologyValid()));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("Duplicate"))));
	}

	TEST(InvalidAuthoritativeDestinationProviderIdsFailClosed,
		"SeinARTS.Unit.CoreEntity.AuthoritativeDestinationProviders")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem& World = MakeIsolatedWorld(Spawner);
		FString Error;
		uint64 Token = 0;
		TestRunner->AddExpectedError(
			TEXT("Execution topology invalid: Invalid authoritative-destination provider ID"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		ASSERT_THAT(IsFalse(World.RegisterAuthoritativeDestinationProvider(
			"invalid provider id",
			1,
			MakeProvider([](const FSeinAuthoritativeDestinationQuery&)
			{
				return true;
			}),
			Token,
			&Error)));
		ASSERT_THAT(AreEqual(uint64(0), Token));
		ASSERT_THAT(IsFalse(World.IsExecutionTopologyValid()));
		ASSERT_THAT(IsFalse(Error.IsEmpty()));
	}

	TEST(RecursiveAuthoritativeDestinationProvidersFailClosed,
		"SeinARTS.Unit.CoreEntity.AuthoritativeDestinationProviders")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem& World = MakeIsolatedWorld(Spawner);
		FString Error;
		uint64 Token = 0;
		TestRunner->AddExpectedError(
			TEXT("Execution topology invalid: Authoritative-destination provider queries may not re-enter"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		ASSERT_THAT(IsTrue(World.RegisterAuthoritativeDestinationProvider(
			"seinarts.test.recursive",
			1,
			MakeProvider([&World](const FSeinAuthoritativeDestinationQuery&)
			{
				return World.IsAuthoritativeDestination(FFixedVector());
			}),
			Token,
			&Error)));

		ASSERT_THAT(IsFalse(World.IsAuthoritativeDestination(FFixedVector())));
		ASSERT_THAT(IsFalse(World.IsExecutionTopologyValid()));
	}

	TEST(AuthoritativeDestinationRegistrationOrderIsNotContractState,
		"SeinARTS.Determinism.CoreEntity.AuthoritativeDestinationProviders")
	{
		FActorTestSpawner FirstSpawner;
		FActorTestSpawner SecondSpawner;
		USeinWorldSubsystem& First = MakeIsolatedWorld(FirstSpawner);
		USeinWorldSubsystem& Second = MakeIsolatedWorld(SecondSpawner);
		FString Error;
		uint64 Token = 0;
		ASSERT_THAT(IsTrue(First.RegisterAuthoritativeDestinationProvider(
			"seinarts.test.zulu", 3,
			MakeProvider([](const FSeinAuthoritativeDestinationQuery&)
			{
				return false;
			}), Token, &Error)));
		ASSERT_THAT(IsTrue(First.RegisterAuthoritativeDestinationProvider(
			"seinarts.test.alpha", 7,
			MakeProvider([](const FSeinAuthoritativeDestinationQuery&)
			{
				return true;
			}), Token, &Error)));
		ASSERT_THAT(IsTrue(Second.RegisterAuthoritativeDestinationProvider(
			"seinarts.test.alpha", 7,
			MakeProvider([](const FSeinAuthoritativeDestinationQuery&)
			{
				return true;
			}), Token, &Error)));
		ASSERT_THAT(IsTrue(Second.RegisterAuthoritativeDestinationProvider(
			"seinarts.test.zulu", 3,
			MakeProvider([](const FSeinAuthoritativeDestinationQuery&)
			{
				return false;
			}), Token, &Error)));

		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(First)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(Second)));
		ASSERT_THAT(IsTrue(First.GetCanonicalStateContractDigest().IsValid()));
		ASSERT_THAT(AreEqual(
			First.GetCanonicalStateContractDigest(),
			Second.GetCanonicalStateContractDigest()));
	}

	TEST(AuthoritativeDestinationBehaviorRevisionChangesContract,
		"SeinARTS.Determinism.CoreEntity.AuthoritativeDestinationProviders")
	{
		FActorTestSpawner FirstSpawner;
		FActorTestSpawner SecondSpawner;
		USeinWorldSubsystem& First = MakeIsolatedWorld(FirstSpawner);
		USeinWorldSubsystem& Second = MakeIsolatedWorld(SecondSpawner);
		FString Error;
		uint64 Token = 0;
		ASSERT_THAT(IsTrue(First.RegisterAuthoritativeDestinationProvider(
			"seinarts.test.revision", 1,
			MakeProvider([](const FSeinAuthoritativeDestinationQuery&)
			{
				return false;
			}), Token, &Error)));
		ASSERT_THAT(IsTrue(Second.RegisterAuthoritativeDestinationProvider(
			"seinarts.test.revision", 2,
			MakeProvider([](const FSeinAuthoritativeDestinationQuery&)
			{
				return false;
			}), Token, &Error)));

		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(First)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(Second)));
		ASSERT_THAT(IsTrue(
			First.GetCanonicalStateContractDigest()
				!= Second.GetCanonicalStateContractDigest()));
	}

	TEST(PostBootstrapLegacyBindingDriftStopsBeforeNextFixedTick,
		"SeinARTS.Determinism.CoreEntity.AuthoritativeDestinationProviders")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem& World = MakeIsolatedWorld(Spawner);
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(World)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(World)));
		const int32 TickBefore = World.GetCurrentTick();
		TestRunner->AddExpectedError(
			TEXT("Canonical StateContract world-binding validation failed at the fixed-tick boundary: The legacy authoritative-destination resolver cannot enter a deterministic match"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		World.AuthoritativeDestinationResolver.BindLambda(
			[](const FFixedVector&)
			{
				return false;
			});

		FTSTicker::GetCoreTicker().Tick(World.GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsFalse(World.IsExecutionTopologyValid()));
		ASSERT_THAT(AreEqual(TickBefore, World.GetCurrentTick()));
	}

	TEST(LegacyAuthoritativeDestinationResolverCannotSealDeterministicMatch,
		"SeinARTS.Determinism.CoreEntity.AuthoritativeDestinationProviders")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem& World = MakeIsolatedWorld(Spawner);
		World.AuthoritativeDestinationResolver.BindLambda(
			[](const FFixedVector&)
			{
				return false;
			});
		TestRunner->AddExpectedError(
			TEXT("legacy authoritative-destination resolver cannot enter a deterministic match"),
			EAutomationExpectedErrorFlags::Contains,
			2,
			false);
		TestRunner->AddExpectedError(
			TEXT("SeinMatchBootstrap: transaction closed (failed)"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		FString Error;
		ASSERT_THAT(IsFalse(SeinTestMatchBootstrap::Materialize(
			World,
			FSeinMatchSettings(),
			0,
			TEXT("AuthoritativeDestination.LegacyRejected"),
			&Error)));
		ASSERT_THAT(IsTrue(Error.Contains(
			TEXT("legacy authoritative-destination resolver"))));
		ASSERT_THAT(IsFalse(World.IsSimulationRunning()));
	}

	TEST(PostBootstrapProviderWithdrawalInvalidatesImmediately,
		"SeinARTS.Determinism.CoreEntity.AuthoritativeDestinationProviders")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem& World = MakeIsolatedWorld(Spawner);
		FString Error;
		uint64 Token = 0;
		ASSERT_THAT(IsTrue(World.RegisterAuthoritativeDestinationProvider(
			"seinarts.test.withdrawal", 1,
			MakeProvider([](const FSeinAuthoritativeDestinationQuery&)
			{
				return false;
			}), Token, &Error)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(World)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(World)));
		TestRunner->AddExpectedError(
			TEXT("Execution topology invalid: Authoritative-destination provider 'seinarts.test.withdrawal' unregistered"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);

		ASSERT_THAT(IsTrue(World.UnregisterAuthoritativeDestinationProvider(
			Token, &Error)));
		ASSERT_THAT(IsFalse(World.IsExecutionTopologyValid()));
		ASSERT_THAT(IsFalse(World.HasAuthoritativeDestinationProviders()));
	}
}

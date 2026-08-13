#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Containers/Ticker.h"

#include "CoverCanonicalStateTestTypes.h"
#include "Data/SeinMatchSettings.h"
#include "Settings/SeinARTSCoverSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "System/SeinCoverDefault.h"
#include "System/SeinCoverSubsystem.h"
#include "System/SeinCoverSystem.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		/** Swap the configured cover implementation for one fixture world.
		 *  Must outlive the FActorTestSpawner: USeinCoverSubsystem reads the
		 *  setting once during world-subsystem Initialize. */
		struct FScopedCoverSystemClassOverride
		{
			explicit FScopedCoverSystemClassOverride(
				const UClass* CoverClass)
				: Settings(
					GetMutableDefault<USeinARTSCoverSettings>())
				, SavedCoverSystemClass(
					Settings
						? Settings->CoverSystemClass
						: FSoftClassPath())
			{
				check(Settings);
				check(CoverClass);
				Settings->CoverSystemClass =
					FSoftClassPath(CoverClass);
			}

			~FScopedCoverSystemClassOverride()
			{
				Settings->CoverSystemClass =
					SavedCoverSystemClass;
			}

			USeinARTSCoverSettings* Settings = nullptr;
			FSoftClassPath SavedCoverSystemClass;
		};

		bool StartCoverStateWorld(
			USeinWorldSubsystem& World,
			FName FixtureId,
			FString& OutError)
		{
			return SeinTestMatchBootstrap::Materialize(
				World,
				FSeinMatchSettings(),
				0x434F5653,
				FixtureId,
				&OutError)
				&& SeinTestMatchBootstrap::Start(World, &OutError);
		}
	}

	TEST(CustomCoverWithoutStateClaimFailsClosed,
		"SeinARTS.Unit.Cover.CanonicalState")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		FString Error;
		FSeinCoverStateCoverageClaim Claim;

		USeinUnclaimedCoverTestSystem* Unclaimed =
			NewObject<USeinUnclaimedCoverTestSystem>(&UnrealWorld);
		ASSERT_THAT(IsFalse(
			Unclaimed->ComputeStateCoverageClaim(Claim, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(
			TEXT("does not explicitly claim exact mutable-state coverage"))));

		USeinInheritedCoverDefaultTestSystem* Inherited =
			NewObject<USeinInheritedCoverDefaultTestSystem>(
				&UnrealWorld);
		Error.Reset();
		ASSERT_THAT(IsFalse(
			Inherited->ComputeStateCoverageClaim(Claim, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(
			TEXT("must explicitly claim exact mutable-state coverage"))));

		USeinClaimedCoverDefaultTestSystem* Claimed =
			NewObject<USeinClaimedCoverDefaultTestSystem>(
				&UnrealWorld);
		Error.Reset();
		ASSERT_THAT(IsTrue(
			Claimed->ComputeStateCoverageClaim(Claim, Error)));
		ASSERT_THAT(IsTrue(
			Claim.StableImplementationId
				== TEXT("seinarts.cover.default")));
		ASSERT_THAT(IsTrue(
			Claim.StateCoverage
				== ESeinCoverStateCoverage::Stateless));

		USeinCoverDefault* Shipped =
			NewObject<USeinCoverDefault>(&UnrealWorld);
		Error.Reset();
		ASSERT_THAT(IsTrue(
			Shipped->ComputeStateCoverageClaim(Claim, Error)));
		ASSERT_THAT(IsTrue(
			Claim.BehaviorRevision != 0
				&& Claim.CoverageRevision != 0));
	}

	TEST(BootstrapRejectsCustomCoverWithoutExactStateClaim,
		"SeinARTS.Unit.Cover.CanonicalState")
	{
		FScopedCoverSystemClassOverride CoverClass(
			USeinUnclaimedCoverTestSystem::StaticClass());
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinCoverSubsystem* Cover =
			UnrealWorld.GetSubsystem<USeinCoverSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Cover));
		ASSERT_THAT(IsNotNull(Cast<USeinUnclaimedCoverTestSystem>(
			Cover->GetCoverSystem())));

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
		ASSERT_THAT(IsFalse(StartCoverStateWorld(
			*World,
			TEXT("CoverState.UnclaimedBootstrap"),
			Error)));
		ASSERT_THAT(IsTrue(Error.Contains(
			TEXT("does not explicitly claim exact mutable-state coverage"))));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(
				ESeinMatchBootstrapState::Failed),
			static_cast<uint8>(
				World->GetMatchBootstrapState())));
		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
	}

	TEST(ShippedCoverDefaultBootstrapsWithStableWorldBinding,
		"SeinARTS.Unit.Cover.CanonicalState")
	{
		// Pin the shipped default explicitly so a project-configured custom
		// class in the local ini cannot change what this test certifies.
		FScopedCoverSystemClassOverride CoverClass(
			USeinCoverDefault::StaticClass());
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinCoverSubsystem* Cover =
			UnrealWorld.GetSubsystem<USeinCoverSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Cover));
		ASSERT_THAT(IsNotNull(Cast<USeinCoverDefault>(
			Cover->GetCoverSystem())));
		ASSERT_THAT(IsTrue(
			World->HasAuthoritativeDestinationProviders()));
		ASSERT_THAT(IsFalse(
			World->AuthoritativeDestinationResolver.IsBound()));

		FString Error;
		ASSERT_THAT(IsTrue(StartCoverStateWorld(
			*World,
			TEXT("CoverState.DefaultBinding"),
			Error)));
		ASSERT_THAT(IsTrue(
			World->GetCanonicalStateContractDigest().IsValid()));
		ASSERT_THAT(IsTrue(World->IsSimulationRunning()));

		// Recapture stability: the canonical root and the per-tick
		// world-binding revalidation must both agree with the frozen frame.
		FGuid FirstRoot;
		FGuid SecondRoot;
		ASSERT_THAT(IsTrue(
			World->ComputeCanonicalStateRoot(FirstRoot, Error)));
		ASSERT_THAT(IsTrue(
			World->ComputeCanonicalStateRoot(SecondRoot, Error)));
		ASSERT_THAT(IsTrue(FirstRoot == SecondRoot));

		FTSTicker::GetCoreTicker().Tick(
			World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsTrue(World->IsSimulationRunning()));
		ASSERT_THAT(IsTrue(World->IsExecutionTopologyValid()));
		World->StopSimulation();
	}

	TEST(BootstrapAcceptsExplicitStatelessCoverClaim,
		"SeinARTS.Unit.Cover.CanonicalState")
	{
		FScopedCoverSystemClassOverride CoverClass(
			USeinStatelessClaimedCoverTestSystem::StaticClass());
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinCoverSubsystem* Cover =
			UnrealWorld.GetSubsystem<USeinCoverSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Cover));
		ASSERT_THAT(IsNotNull(Cast<
			USeinStatelessClaimedCoverTestSystem>(
				Cover->GetCoverSystem())));

		FString Error;
		ASSERT_THAT(IsTrue(StartCoverStateWorld(
			*World,
			TEXT("CoverState.StatelessBootstrap"),
			Error)));
		ASSERT_THAT(IsTrue(
			World->GetCanonicalStateContractDigest().IsValid()));
		ASSERT_THAT(IsTrue(World->IsSimulationRunning()));
		World->StopSimulation();
	}
}

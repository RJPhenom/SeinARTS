#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Abilities/SeinTargeterSpec.h"
#include "Actor/SeinActor.h"
#include "Components/SeinAbilityPayload.h"
#include "Containers/Ticker.h"
#include "Data/SeinFaction.h"
#include "Data/SeinMatchSettings.h"
#include "Lib/SeinMatchFlowBPFL.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Serialization/SeinCanonicalReflectedStateDigest.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinCommandSchemaTestTypes.h"
#include "TestTypes/SeinInitialStateDigestTestTypes.h"
#include "UObject/StrongObjectPtr.h"

struct FSeinMatchBootstrapPoolTestAccess
{
	static void AppendDuplicateAbilityPointer(
		USeinWorldSubsystem& World,
		USeinAbility& Ability)
	{
		World.AbilityPool.Add(&Ability);
	}
};

namespace UE::SeinARTSTests
{
	namespace
	{
		const FGuid BootstrapContextA(
			0x10000000, 0x20000000, 0x30000000, 1);
		const FGuid BootstrapContextB(
			0x10000000, 0x20000000, 0x30000000, 2);
		const FGuid BootstrapPlan(
			0x40000000, 0x50000000, 0x60000000, 1);
		const FName BootstrapAuthorityID(
			TEXT("SeinFrameworkTests.MatchBootstrap"));

		bool ClaimTestAuthority(
			USeinWorldSubsystem& World,
			FSeinMatchBootstrapAuthorityHandle& OutAuthority,
			FString& OutError)
		{
			return World.ClaimMatchBootstrapAuthority(
				BootstrapAuthorityID,
				&World,
				OutAuthority,
				OutError);
		}

		bool MaterializeMatch(
			USeinWorldSubsystem& World,
			const FSeinMatchBootstrapAuthorityHandle& Authority,
			const FGuid& Context,
			TFunctionRef<void()> AuthorState,
			FSeinMatchBootstrapReceipt& OutReceipt,
			FString& OutError)
		{
			FSeinMatchSettings Settings;

			if (!World.SeedSimRandom(Authority, 12345, OutError))
			{
				return false;
			}
			const FSeinMatchBootstrapMaterializer PreviousMaterializer =
				World.MatchBootstrapMaterializer;
			World.MatchBootstrapMaterializer.BindLambda(
				[&World, &AuthorState, Context](
					const FSeinMatchSettings& MaterializedSettings,
					const FGuid& MaterializedContext,
					FSeinMatchBootstrapReceipt& Receipt,
					FString& Error)
				{
					const bool bCoreOpenedExactTransaction =
						World.GetMatchBootstrapState()
							== ESeinMatchBootstrapState::Applying
						&& MaterializedContext == Context
						&& World.GetMatchBootstrapAuthorizationContextDigest()
							== MaterializedContext;
					if (!bCoreOpenedExactTransaction)
					{
						Error = TEXT(
							"Core did not open the exact Applying bootstrap transaction before materialization.");
						return false;
					}
					World.StartMatch(MaterializedSettings);
					if (World.GetMatchState() != ESeinMatchState::Starting)
					{
						Error = TEXT("Could not install the empty test match.");
						return false;
					}
					AuthorState();
					return World.SealLocalMatchBootstrap(
						BootstrapPlan, Receipt, Error);
				});
			const bool bReady = World.EnsureMatchBootstrapLocallyReady(
				Authority, Settings, Context, OutReceipt, OutError);
			World.MatchBootstrapMaterializer = PreviousMaterializer;
			return bReady;
		}

		bool MaterializeEmptyMatch(
			USeinWorldSubsystem& World,
			const FSeinMatchBootstrapAuthorityHandle& Authority,
			const FGuid& Context,
			FSeinMatchBootstrapReceipt& OutReceipt,
			FString& OutError)
		{
			const auto AuthorNothing = []() {};
			return MaterializeMatch(
				World, Authority, Context, AuthorNothing, OutReceipt, OutError);
		}
	}

	TEST(MatchBootstrapBarrierRequiresExactAuthorizationBeforeFirstLaunch,
		"SeinARTS.Unit.CoreEntity.MatchBootstrap")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		int32 CloseCount = 0;
		bool bClosedAuthorized = false;
		World->OnMatchBootstrapClosed.AddLambda(
			[&CloseCount, &bClosedAuthorized](bool bAuthorized)
			{
				++CloseCount;
				bClosedAuthorized = bAuthorized;
			});

		FSeinMatchBootstrapReceipt Receipt;
		FString Error;
		FSeinMatchBootstrapAuthorityHandle Authority;
		ASSERT_THAT(IsTrue(ClaimTestAuthority(*World, Authority, Error)));
		ASSERT_THAT(IsTrue(MaterializeEmptyMatch(
			*World, Authority, BootstrapContextA, Receipt, Error)));
		ASSERT_THAT(IsTrue(Receipt.IsValid()));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinMatchBootstrapState::LocallyReady),
			static_cast<uint8>(World->GetMatchBootstrapState())));
		ASSERT_THAT(AreEqual(0, CloseCount));

		ASSERT_THAT(IsTrue(World->AuthorizeMatchBootstrap(
			Authority, Receipt, BootstrapContextA, Error)));
		ASSERT_THAT(IsTrue(World->AuthorizeMatchBootstrap(
			Authority, Receipt, BootstrapContextA, Error)));
		ASSERT_THAT(AreEqual(1, CloseCount));
		ASSERT_THAT(IsTrue(bClosedAuthorized));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinMatchBootstrapState::Authorized),
			static_cast<uint8>(World->GetMatchBootstrapState())));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(AreEqual(0, World->GetCurrentTick()));
		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));

		TestRunner->AddExpectedError(
			TEXT("Simulation start refused by match bootstrap barrier"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(World->StartSimulation()));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinMatchBootstrapState::Authorized),
			static_cast<uint8>(World->GetMatchBootstrapState())));
		ASSERT_THAT(IsTrue(World->LaunchAuthorizedMatchBootstrap(
			Authority, Error)));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinMatchBootstrapState::Consumed),
			static_cast<uint8>(World->GetMatchBootstrapState())));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(AreEqual(1, World->GetCurrentTick()));
		World->StopSimulation();
		ASSERT_THAT(IsTrue(World->StartSimulation()));
		World->StopSimulation();
		ASSERT_THAT(AreEqual(1, CloseCount));
	}

	TEST(MatchBootstrapAuthorityClaimIsExclusiveAndRetrySafe,
		"SeinARTS.Unit.CoreEntity.MatchBootstrap")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FString Error;
		FSeinMatchBootstrapAuthorityHandle FirstAuthority;
		ASSERT_THAT(IsTrue(ClaimTestAuthority(
			*World, FirstAuthority, Error)));
		ASSERT_THAT(IsTrue(FirstAuthority.IsValid()));

		FSeinMatchBootstrapAuthorityHandle RetryAuthority;
		ASSERT_THAT(IsTrue(ClaimTestAuthority(
			*World, RetryAuthority, Error)));
		ASSERT_THAT(IsTrue(RetryAuthority.IsValid()));

		UObject* Competitor = NewObject<USeinCommandSchemaTestHandler>(World);
		ASSERT_THAT(IsNotNull(Competitor));
		FSeinMatchBootstrapAuthorityHandle RejectedAuthority;
		ASSERT_THAT(IsFalse(World->ClaimMatchBootstrapAuthority(
			BootstrapAuthorityID,
			Competitor,
			RejectedAuthority,
			Error)));
		ASSERT_THAT(IsFalse(RejectedAuthority.IsValid()));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("already claimed"))));

		FSeinMatchBootstrapReceipt Receipt;
		ASSERT_THAT(IsTrue(MaterializeEmptyMatch(
			*World, RetryAuthority, BootstrapContextA, Receipt, Error)));
		ASSERT_THAT(IsTrue(World->AuthorizeMatchBootstrap(
			RetryAuthority, Receipt, BootstrapContextA, Error)));
		ASSERT_THAT(IsTrue(World->LaunchAuthorizedMatchBootstrap(
			FirstAuthority, Error)));
		ASSERT_THAT(IsTrue(World->LaunchAuthorizedMatchBootstrap(
			RetryAuthority, Error)));
		World->StopSimulation();
	}

	TEST(MatchBootstrapAuthorityRejectsInvalidAndForeignCapabilities,
		"SeinARTS.Unit.CoreEntity.MatchBootstrap")
	{
		FActorTestSpawner TargetSpawner;
		USeinWorldSubsystem* Target =
			TargetSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Target));

		FActorTestSpawner ForeignSpawner;
		USeinWorldSubsystem* Foreign =
			ForeignSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Foreign));

		FString Error;
		FSeinMatchBootstrapAuthorityHandle TargetAuthority;
		FSeinMatchBootstrapAuthorityHandle ForeignAuthority;
		ASSERT_THAT(IsTrue(ClaimTestAuthority(
			*Target, TargetAuthority, Error)));
		ASSERT_THAT(IsTrue(ClaimTestAuthority(
			*Foreign, ForeignAuthority, Error)));

		FSeinMatchBootstrapAuthorityHandle InvalidAuthority;
		uint64 SeedStateBefore0 = 0;
		uint64 SeedStateBefore1 = 0;
		Target->SimRandom.GetState(SeedStateBefore0, SeedStateBefore1);
		ASSERT_THAT(IsFalse(Target->SeedSimRandom(
			InvalidAuthority, 12345, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("exact claimed"))));
		ASSERT_THAT(IsFalse(Target->SeedSimRandom(
			ForeignAuthority, 12345, Error)));
		uint64 SeedStateAfterRejected0 = 0;
		uint64 SeedStateAfterRejected1 = 0;
		Target->SimRandom.GetState(
			SeedStateAfterRejected0, SeedStateAfterRejected1);
		ASSERT_THAT(AreEqual(SeedStateBefore0, SeedStateAfterRejected0));
		ASSERT_THAT(AreEqual(SeedStateBefore1, SeedStateAfterRejected1));

		ASSERT_THAT(IsTrue(Target->SeedSimRandom(
			TargetAuthority, 12345, Error)));
		ASSERT_THAT(IsTrue(Target->SeedSimRandom(
			TargetAuthority, 12345, Error)));
		uint64 InstalledSeedState0 = 0;
		uint64 InstalledSeedState1 = 0;
		Target->SimRandom.GetState(
			InstalledSeedState0, InstalledSeedState1);
		ASSERT_THAT(IsFalse(Target->SeedSimRandom(
			TargetAuthority, 54321, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("conflicting retry"))));
		uint64 SeedStateAfterConflict0 = 0;
		uint64 SeedStateAfterConflict1 = 0;
		Target->SimRandom.GetState(
			SeedStateAfterConflict0, SeedStateAfterConflict1);
		ASSERT_THAT(AreEqual(InstalledSeedState0, SeedStateAfterConflict0));
		ASSERT_THAT(AreEqual(InstalledSeedState1, SeedStateAfterConflict1));

		FSeinMatchBootstrapReceipt Receipt;
		ASSERT_THAT(IsTrue(MaterializeEmptyMatch(
			*Target, TargetAuthority, BootstrapContextA, Receipt, Error)));
		ASSERT_THAT(IsFalse(Target->SeedSimRandom(
			TargetAuthority, 12345, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("Awaiting world"))));
		ASSERT_THAT(IsFalse(Target->AuthorizeMatchBootstrap(
			InvalidAuthority, Receipt, BootstrapContextA, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("exact claimed authority"))));
		ASSERT_THAT(IsFalse(Target->AuthorizeMatchBootstrap(
			ForeignAuthority, Receipt, BootstrapContextA, Error)));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinMatchBootstrapState::LocallyReady),
			static_cast<uint8>(Target->GetMatchBootstrapState())));

		ASSERT_THAT(IsTrue(Target->AuthorizeMatchBootstrap(
			TargetAuthority, Receipt, BootstrapContextA, Error)));
		ASSERT_THAT(IsFalse(Target->LaunchAuthorizedMatchBootstrap(
			InvalidAuthority, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("exact claimed authority"))));
		ASSERT_THAT(IsFalse(Target->LaunchAuthorizedMatchBootstrap(
			ForeignAuthority, Error)));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinMatchBootstrapState::Authorized),
			static_cast<uint8>(Target->GetMatchBootstrapState())));
		ASSERT_THAT(IsFalse(Target->IsSimulationRunning()));

		ASSERT_THAT(IsTrue(Target->LaunchAuthorizedMatchBootstrap(
			TargetAuthority, Error)));
		Target->StopSimulation();
	}

	TEST(MatchBootstrapBarrierFailsTerminallyOnWrongAuthorizationContext,
		"SeinARTS.Unit.CoreEntity.MatchBootstrap")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		int32 CloseCount = 0;
		bool bClosedAuthorized = true;
		World->OnMatchBootstrapClosed.AddLambda(
			[&CloseCount, &bClosedAuthorized](bool bAuthorized)
			{
				++CloseCount;
				bClosedAuthorized = bAuthorized;
			});

		FSeinMatchBootstrapReceipt Receipt;
		FString Error;
		FSeinMatchBootstrapAuthorityHandle Authority;
		ASSERT_THAT(IsTrue(ClaimTestAuthority(*World, Authority, Error)));
		ASSERT_THAT(IsTrue(MaterializeEmptyMatch(
			*World, Authority, BootstrapContextA, Receipt, Error)));
		TestRunner->AddExpectedError(
			TEXT("Match bootstrap failed closed"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("transaction closed (failed)"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(World->AuthorizeMatchBootstrap(
			Authority, Receipt, BootstrapContextB, Error)));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinMatchBootstrapState::Failed),
			static_cast<uint8>(World->GetMatchBootstrapState())));
		ASSERT_THAT(AreEqual(1, CloseCount));
		ASSERT_THAT(IsFalse(bClosedAuthorized));
		ASSERT_THAT(IsFalse(World->GetMatchBootstrapFailureReason().IsEmpty()));

		ASSERT_THAT(IsFalse(World->AuthorizeMatchBootstrap(
			Authority, Receipt, BootstrapContextA, Error)));
		ASSERT_THAT(IsTrue(World->FailMatchBootstrap(
			Authority, TEXT("must remain terminal"), Error)));
		ASSERT_THAT(AreEqual(1, CloseCount));
	}

	TEST(MatchBootstrapBarrierFreezesMutationAfterLocalReceipt,
		"SeinARTS.Unit.CoreEntity.MatchBootstrap")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FSeinMatchBootstrapReceipt Receipt;
		FString Error;
		FSeinMatchBootstrapAuthorityHandle Authority;
		ASSERT_THAT(IsTrue(ClaimTestAuthority(*World, Authority, Error)));
		ASSERT_THAT(IsTrue(MaterializeEmptyMatch(
			*World, Authority, BootstrapContextA, Receipt, Error)));
		TestRunner->AddExpectedError(
			TEXT("RegisterPlayer rejected outside bootstrap Applying"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		World->RegisterPlayer(
			FSeinPlayerID(7), FSeinFactionID(3), /*TeamID=*/2);
		ASSERT_THAT(IsNull(World->GetPlayerState(FSeinPlayerID(7))));
		ASSERT_THAT(IsTrue(World->AuthorizeMatchBootstrap(
			Authority, Receipt, BootstrapContextA, Error)));
	}

	TEST(MatchBootstrapSealRequiresTheMaterializerInvocation,
		"SeinARTS.Unit.CoreEntity.MatchBootstrap")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FString Error;
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinMatchBootstrapState::Awaiting),
			static_cast<uint8>(World->GetMatchBootstrapState())));

		FSeinMatchBootstrapReceipt Receipt;
		ASSERT_THAT(IsFalse(World->SealLocalMatchBootstrap(
			BootstrapPlan, Receipt, Error)));
		ASSERT_THAT(IsFalse(Receipt.IsValid()));
		ASSERT_THAT(IsTrue(Error.Contains(
			TEXT("authority-gated materializer invocation"))));
	}

	TEST(MatchBootstrapObserversCannotUseTheMaterializerCapability,
		"SeinARTS.Unit.CoreEntity.MatchBootstrap")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FString Error;
		FSeinMatchBootstrapAuthorityHandle Authority;
		ASSERT_THAT(IsTrue(ClaimTestAuthority(*World, Authority, Error)));

		USeinFaction* ObserverFaction = NewObject<USeinFaction>(World);
		ASSERT_THAT(IsNotNull(ObserverFaction));
		ObserverFaction->FactionID = FSeinFactionID(9);

		bool bObserverCalled = false;
		bool bObserverRegisteredValue = true;
		bool bObserverSealed = true;
		bool bMaterializerRegisteredValue = false;
		FString ObserverError;
		const FName ContributorID(
			TEXT("SeinFrameworkTests.ObserverBootstrapValue"));
		const FDelegateHandle ObserverHandle =
			World->OnEntitySpawned.AddLambda(
				[&](FSeinEntityHandle)
				{
					bObserverCalled = true;
					FSeinCommandSchemaAlternateTestPayload ObserverValue;
					ObserverValue.Marker = 41;
					bObserverRegisteredValue =
						World->RegisterCanonicalBootstrapEvidenceValue(
							ContributorID,
							1,
							FInstancedStruct::Make(ObserverValue),
							ObserverError);
					World->RegisterFaction(ObserverFaction);
					World->RegisterFactionsFromSettings();
					World->StartMatch(FSeinMatchSettings());

					FSeinMatchBootstrapReceipt ObserverReceipt;
					bObserverSealed = World->SealLocalMatchBootstrap(
						BootstrapPlan, ObserverReceipt, ObserverError);
				});

		TestRunner->AddExpectedError(
			TEXT("RegisterCanonicalBootstrapEvidenceValue rejected"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("RegisterFaction rejected"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("RegisterFactionsFromSettings rejected"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("StartMatch rejected"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("SealLocalMatchBootstrap rejected"),
			EAutomationExpectedErrorFlags::Contains, 1, false);

		FSeinMatchBootstrapReceipt Receipt;
		const auto AuthorState = [&]()
		{
			const FSeinEntityHandle Spawned = World->SpawnEntity(
				ASeinActor::StaticClass(),
				FFixedTransform(),
				FSeinPlayerID::Neutral());
			if (!Spawned.IsValid())
			{
				return;
			}

			FSeinCommandSchemaAlternateTestPayload MaterializerValue;
			MaterializerValue.Marker = 42;
			bMaterializerRegisteredValue =
				World->RegisterCanonicalBootstrapEvidenceValue(
					ContributorID,
					1,
					FInstancedStruct::Make(MaterializerValue),
					Error);
		};
		const bool bMaterialized = MaterializeMatch(
			*World,
			Authority,
			BootstrapContextA,
			AuthorState,
			Receipt,
			Error);
		World->OnEntitySpawned.Remove(ObserverHandle);

		ASSERT_THAT(IsTrue(bMaterialized));
		ASSERT_THAT(IsTrue(bObserverCalled));
		ASSERT_THAT(IsFalse(bObserverRegisteredValue));
		ASSERT_THAT(IsFalse(bObserverSealed));
		ASSERT_THAT(IsTrue(bMaterializerRegisteredValue));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinMatchBootstrapState::LocallyReady),
			static_cast<uint8>(World->GetMatchBootstrapState())));
	}

	TEST(MatchBootstrapValueContributorsAffectTheSealedReceipt,
		"SeinARTS.Unit.CoreEntity.MatchBootstrap")
	{
		FSeinMatchBootstrapReceipt Receipts[2];
		for (int32 Index = 0; Index < 2; ++Index)
		{
			FActorTestSpawner Spawner;
			USeinWorldSubsystem* World =
				Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			ASSERT_THAT(IsNotNull(World));

			FString Error;
			FSeinMatchBootstrapAuthorityHandle Authority;
			ASSERT_THAT(IsTrue(ClaimTestAuthority(*World, Authority, Error)));

			FSeinCommandSchemaAlternateTestPayload Value;
			Value.Marker = 10 + Index;
			bool bRegistered = false;
			const auto AuthorState = [&]()
			{
				bRegistered = World->RegisterCanonicalBootstrapEvidenceValue(
					TEXT("SeinFrameworkTests.BootstrapValue"),
					1,
					FInstancedStruct::Make(Value),
					Error);
			};
			ASSERT_THAT(IsTrue(MaterializeMatch(
				*World,
				Authority,
				BootstrapContextA,
				AuthorState,
				Receipts[Index],
				Error)));
			ASSERT_THAT(IsTrue(bRegistered));
		}

		ASSERT_THAT(IsTrue(
			Receipts[0].ContractDigest == Receipts[1].ContractDigest));
		ASSERT_THAT(IsTrue(Receipts[0].PlanDigest == Receipts[1].PlanDigest));
		ASSERT_THAT(IsTrue(
			Receipts[0].InitialStateDigest != Receipts[1].InitialStateDigest));
	}

	TEST(BlueprintInitialStateValueRegistrationClosesAtSeal,
		"SeinARTS.Unit.CoreEntity.MatchBootstrap")
	{
		FActorTestSpawner Spawner;
		UWorld& TestWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			TestWorld.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FString Error;
		FSeinMatchBootstrapAuthorityHandle Authority;
		ASSERT_THAT(IsTrue(ClaimTestAuthority(*World, Authority, Error)));

		FSeinCommandSchemaAlternateTestPayload Value;
		Value.Marker = 31;
		bool bRegistered = false;
		FString RegistrationError;
		const auto AuthorState = [&]()
		{
			bRegistered =
				USeinMatchFlowBPFL::SeinRegisterBootstrapEvidenceValue(
				&TestWorld,
				TEXT("SeinFrameworkTests.BlueprintBootstrapValue"),
				1,
				FInstancedStruct::Make(Value),
				RegistrationError);
		};

		FSeinMatchBootstrapReceipt Receipt;
		ASSERT_THAT(IsTrue(MaterializeMatch(
			*World,
			Authority,
			BootstrapContextA,
			AuthorState,
			Receipt,
			Error)));
		ASSERT_THAT(IsTrue(bRegistered));
		ASSERT_THAT(IsTrue(RegistrationError.IsEmpty()));
		const FSeinMatchBootstrapReceipt FrozenReceipt =
			World->GetMatchBootstrapReceipt();

		Value.Marker = 32;
		ASSERT_THAT(IsFalse(
			USeinMatchFlowBPFL::SeinRegisterBootstrapEvidenceValue(
				&TestWorld,
				TEXT("SeinFrameworkTests.BlueprintBootstrapValueAfterSeal"),
				1,
				FInstancedStruct::Make(Value),
				Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("only while bootstrap is Applying"))));
		ASSERT_THAT(IsTrue(World->GetMatchBootstrapReceipt() == FrozenReceipt));
		ASSERT_THAT(IsTrue(World->AuthorizeMatchBootstrap(
			Authority, Receipt, BootstrapContextA, Error)));
	}

	TEST(MatchBootstrapNativeContributorsExecuteOnlyAtSeal,
		"SeinARTS.Unit.CoreEntity.MatchBootstrap")
	{
		int32 CaptureCount = 0;
		FString RegistrationError;
		FSeinCanonicalInitialStateContributorHandle Contributor =
			FSeinCanonicalInitialStateDigest::RegisterNativeContributor(
				TEXT("SeinFrameworkTests.BootstrapCallbackOnce"),
				1,
				[&CaptureCount](
					const USeinWorldSubsystem&,
					FSeinCanonicalDigestWriter& Writer,
					FString&)
				{
					++CaptureCount;
					return Writer.WriteInt32(77);
				},
				&RegistrationError);
		ASSERT_THAT(IsTrue(Contributor.IsValid()));
		ASSERT_THAT(IsTrue(RegistrationError.IsEmpty()));

		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FString Error;
		FSeinMatchBootstrapAuthorityHandle Authority;
		ASSERT_THAT(IsTrue(ClaimTestAuthority(*World, Authority, Error)));

		int32 CaptureCountBeforeSeal = -1;
		const auto AuthorState = [&]()
		{
			CaptureCountBeforeSeal = CaptureCount;
		};
		FSeinMatchBootstrapReceipt Receipt;
		ASSERT_THAT(IsTrue(MaterializeMatch(
			*World,
			Authority,
			BootstrapContextA,
			AuthorState,
			Receipt,
			Error)));
		ASSERT_THAT(AreEqual(0, CaptureCountBeforeSeal));
		ASSERT_THAT(AreEqual(1, CaptureCount));

		ASSERT_THAT(IsTrue(World->AuthorizeMatchBootstrap(
			Authority, Receipt, BootstrapContextA, Error)));
		ASSERT_THAT(AreEqual(1, CaptureCount));
		ASSERT_THAT(IsTrue(World->LaunchAuthorizedMatchBootstrap(
			Authority, Error)));
		World->StopSimulation();
		ASSERT_THAT(AreEqual(1, CaptureCount));
	}

	TEST(MatchBootstrapNativeContributorReloadLeasesAreGenerationExact,
		"SeinARTS.Unit.CoreEntity.MatchBootstrap")
	{
		const FName ContributorID(
			TEXT("SeinFrameworkTests.BootstrapReloadLease"));
		int32 PreviousCaptures = 0;
		int32 ReplacementCaptures = 0;
		FString Error;
		FSeinCanonicalInitialStateContributorHandle Previous =
			FSeinCanonicalInitialStateDigest::RegisterNativeContributor(
				ContributorID,
				1,
				[&PreviousCaptures](
					const USeinWorldSubsystem&,
					FSeinCanonicalDigestWriter& Writer,
					FString&)
				{
					++PreviousCaptures;
					return Writer.WriteInt32(1);
				},
				&Error);
		ASSERT_THAT(IsTrue(Previous.IsValid()));

		FSeinCanonicalInitialStateContributorHandle Replacement =
			FSeinCanonicalInitialStateDigest::RegisterNativeContributor(
				ContributorID,
				1,
				[&ReplacementCaptures](
					const USeinWorldSubsystem&,
					FSeinCanonicalDigestWriter& Writer,
					FString&)
				{
					++ReplacementCaptures;
					return Writer.WriteInt32(2);
				},
				&Error);
		ASSERT_THAT(IsTrue(Replacement.IsValid()));

		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const auto CaptureActiveGeneration = [&]()
		{
			TArray<FSeinCanonicalInitialStateNativeContribution> Captured;
			ASSERT_THAT(IsTrue(
				FSeinCanonicalInitialStateDigest::CaptureNativeContributors(
					Captured, Error)));
			const FSeinCanonicalInitialStateNativeContribution* Match =
				Captured.FindByPredicate(
					[ContributorID](
						const FSeinCanonicalInitialStateNativeContribution&
							Contribution)
					{
						return Contribution.StableContributorID
							== ContributorID;
					});
			ASSERT_THAT(IsNotNull(Match));
			FSeinCanonicalDigestWriter Writer(
				TEXT("SeinFrameworkTests.BootstrapReloadLease"));
			ASSERT_THAT(IsTrue(Match->Capture(*World, Writer, Error)));
		};

		CaptureActiveGeneration();
		ASSERT_THAT(AreEqual(0, PreviousCaptures));
		ASSERT_THAT(AreEqual(1, ReplacementCaptures));

		Previous.Reset();
		CaptureActiveGeneration();
		ASSERT_THAT(AreEqual(0, PreviousCaptures));
		ASSERT_THAT(AreEqual(2, ReplacementCaptures));

		FSeinCanonicalInitialStateContributorHandle Conflict =
			FSeinCanonicalInitialStateDigest::RegisterNativeContributor(
				ContributorID,
				2,
				[](
					const USeinWorldSubsystem&,
					FSeinCanonicalDigestWriter& Writer,
					FString&)
				{
					return Writer.WriteInt32(3);
				},
				&Error);
		ASSERT_THAT(IsFalse(Conflict.IsValid()));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("conflicts"))));

		Replacement.Reset();
		TArray<FSeinCanonicalInitialStateNativeContribution> Captured;
		ASSERT_THAT(IsTrue(
			FSeinCanonicalInitialStateDigest::CaptureNativeContributors(
				Captured, Error)));
		ASSERT_THAT(IsNull(Captured.FindByPredicate(
			[ContributorID](
				const FSeinCanonicalInitialStateNativeContribution&
					Contribution)
			{
				return Contribution.StableContributorID == ContributorID;
			})));
	}

	TEST(CanonicalReflectedStateDigestProjectsValuesAndFailsClosed,
		"SeinARTS.Unit.CoreEntity.MatchBootstrap")
	{
		const FSeinCanonicalReflectedStateLimits Limits;
		FGuid SchemaDigest;
		FString Error;
		ASSERT_THAT(IsTrue(
			FSeinCanonicalReflectedStateDigest::ComputeSchemaDigest(
				FSeinInitialStateDigestProbeComponent::StaticStruct(),
				Limits,
				SchemaDigest,
				Error)));
		FGuid SignedSchema;
		FGuid UnsignedSchema;
		ASSERT_THAT(IsTrue(
			FSeinCanonicalReflectedStateDigest::ComputeSchemaDigest(
				FSeinInitialStateDigestSignedValue::StaticStruct(),
				Limits,
				SignedSchema,
				Error)));
		ASSERT_THAT(IsTrue(
			FSeinCanonicalReflectedStateDigest::ComputeSchemaDigest(
				FSeinInitialStateDigestUnsignedValue::StaticStruct(),
				Limits,
				UnsignedSchema,
				Error)));
		ASSERT_THAT(IsTrue(SignedSchema != UnsignedSchema));

		FSeinInitialStateDigestProbeComponent A;
		A.Scalar = 17;
		A.Values.Add(TEXT("Alpha"), 1);
		A.Values.Add(TEXT("Bravo"), 2);
		A.Nested = FInstancedStruct::Make(
			FSeinInitialStateDigestNestedValue{ 31 });
		A.TransientValue = 100;
		A.MetadataOnlyIgnoreAttempt = 300;
		A.PresentationText = FText::FromString(TEXT("Presentation A"));

		FSeinInitialStateDigestProbeComponent B;
		B.Scalar = 17;
		B.Values.Add(TEXT("Bravo"), 2);
		B.Values.Add(TEXT("Alpha"), 1);
		B.Nested = FInstancedStruct::Make(
			FSeinInitialStateDigestNestedValue{ 31 });
		B.TransientValue = 200;
		B.MetadataOnlyIgnoreAttempt = 300;
		B.PresentationText = FText::FromString(TEXT("Presentation B"));

		FGuid DigestA;
		FGuid DigestB;
		ASSERT_THAT(IsTrue(
			FSeinCanonicalReflectedStateDigest::ComputeStructValueDigest(
				FSeinInitialStateDigestProbeComponent::StaticStruct(),
				&A,
				SchemaDigest,
				Limits,
				DigestA,
				Error)));
		ASSERT_THAT(IsTrue(
			FSeinCanonicalReflectedStateDigest::ComputeStructValueDigest(
				FSeinInitialStateDigestProbeComponent::StaticStruct(),
				&B,
				SchemaDigest,
				Limits,
				DigestB,
				Error)));
		ASSERT_THAT(IsTrue(DigestA == DigestB));

		B.MetadataOnlyIgnoreAttempt = 400;
		ASSERT_THAT(IsTrue(
			FSeinCanonicalReflectedStateDigest::ComputeStructValueDigest(
				FSeinInitialStateDigestProbeComponent::StaticStruct(),
				&B,
				SchemaDigest,
				Limits,
				DigestB,
				Error)));
		ASSERT_THAT(IsTrue(DigestA != DigestB));

		B.MetadataOnlyIgnoreAttempt = 300;
		B.Nested = FInstancedStruct::Make(
			FSeinInitialStateDigestNestedValue{ 32 });
		ASSERT_THAT(IsTrue(
			FSeinCanonicalReflectedStateDigest::ComputeStructValueDigest(
				FSeinInitialStateDigestProbeComponent::StaticStruct(),
				&B,
				SchemaDigest,
				Limits,
				DigestB,
				Error)));
		ASSERT_THAT(IsTrue(DigestA != DigestB));

		FSeinInitialStateDigestProbeComponent Aliased;
		Aliased.FirstAlias =
			NewObject<USeinInitialStateDigestAliasNode>(
				GetTransientPackage());
		Aliased.FirstAlias->Marker = 44;
		Aliased.SecondAlias = Aliased.FirstAlias;
		FSeinInitialStateDigestProbeComponent Duplicated;
		Duplicated.FirstAlias =
			NewObject<USeinInitialStateDigestAliasNode>(
				GetTransientPackage());
		Duplicated.SecondAlias =
			NewObject<USeinInitialStateDigestAliasNode>(
				GetTransientPackage());
		Duplicated.FirstAlias->Marker = 44;
		Duplicated.SecondAlias->Marker = 44;
		ASSERT_THAT(IsTrue(
			FSeinCanonicalReflectedStateDigest::ComputeStructValueDigest(
				FSeinInitialStateDigestProbeComponent::StaticStruct(),
				&Aliased,
				SchemaDigest,
				Limits,
				DigestA,
				Error)));
		ASSERT_THAT(IsTrue(
			FSeinCanonicalReflectedStateDigest::ComputeStructValueDigest(
				FSeinInitialStateDigestProbeComponent::StaticStruct(),
				&Duplicated,
				SchemaDigest,
				Limits,
				DigestB,
				Error)));
		ASSERT_THAT(IsTrue(DigestA != DigestB));

		B.RuntimeObject =
			NewObject<USeinInitialStateDigestAliasNode>(
				GetTransientPackage());
		ASSERT_THAT(IsFalse(
			FSeinCanonicalReflectedStateDigest::ComputeStructValueDigest(
				FSeinInitialStateDigestProbeComponent::StaticStruct(),
				&B,
				SchemaDigest,
				Limits,
				DigestB,
				Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("RuntimeObject"))));
	}

	TEST(MatchBootstrapReceiptBindsCompleteComponentPayloads,
		"SeinARTS.Unit.CoreEntity.MatchBootstrap")
	{
		FSeinMatchBootstrapReceipt Receipts[3];
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FActorTestSpawner Spawner;
			USeinWorldSubsystem* World =
				Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			ASSERT_THAT(IsNotNull(World));
			FString Error;
			FSeinMatchBootstrapAuthorityHandle Authority;
			ASSERT_THAT(IsTrue(ClaimTestAuthority(
				*World, Authority, Error)));

			bool bAuthored = false;
			const auto AuthorState = [&]()
			{
				const FSeinEntityHandle Handle = World->SpawnEntity(
					ASeinActor::StaticClass(),
					FFixedTransform(),
					FSeinPlayerID::Neutral());
				if (!Handle.IsValid())
				{
					return;
				}
				FSeinInitialStateDigestProbeComponent Probe;
				Probe.Scalar = Index == 2 ? 11 : 10;
				Probe.Values.Add(TEXT("Stable"), 7);
				Probe.Nested = FInstancedStruct::Make(
					FSeinInitialStateDigestNestedValue{ 9 });
				World->AddComponent(Handle, Probe);
				bAuthored = true;
			};
			ASSERT_THAT(IsTrue(MaterializeMatch(
				*World,
				Authority,
				BootstrapContextA,
				AuthorState,
				Receipts[Index],
				Error)));
			ASSERT_THAT(IsTrue(bAuthored));
		}
		ASSERT_THAT(IsTrue(
			Receipts[0].InitialStateDigest
				== Receipts[1].InitialStateDigest));
		ASSERT_THAT(IsTrue(
			Receipts[0].InitialStateDigest
				!= Receipts[2].InitialStateDigest));
	}

	TEST(MatchBootstrapReceiptBindsPoolAndFactionReflectedState,
		"SeinARTS.Unit.CoreEntity.MatchBootstrap")
	{
		FSeinMatchBootstrapReceipt PoolReceipts[4];
		for (int32 Index = 0; Index < 4; ++Index)
		{
			FActorTestSpawner Spawner;
			USeinWorldSubsystem* World =
				Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			ASSERT_THAT(IsNotNull(World));
			FString Error;
			FSeinMatchBootstrapAuthorityHandle Authority;
			ASSERT_THAT(IsTrue(ClaimTestAuthority(
				*World, Authority, Error)));
			bool bRegistered = false;
			const auto AuthorState = [&]()
			{
				USeinInitialStateDigestTestAbility* Ability =
					NewObject<USeinInitialStateDigestTestAbility>(World);
				Ability->DeterministicMarker = Index == 1 ? 2 : 1;
				if (Index == 3)
				{
					Ability->ValidTargetTags =
						FGameplayTagQuery::MakeQuery_MatchTag(
							SeinARTSTags::Command_Type_Ping);
				}
				Ability->TargeterSpec =
					NewObject<USeinPointTargeterSpec>(Ability);
				USeinInitialStateDigestTestResolver* Resolver =
					NewObject<USeinInitialStateDigestTestResolver>(World);
				Resolver->DeterministicMarker = Index == 2 ? 2 : 1;
				Resolver->AuthoredValues.Add(TEXT("Stable"), 5);
				bRegistered =
					World->RegisterAbilityInstance(Ability) == 0
					&& World->RegisterCommandBrokerResolver(Resolver) == 0;
			};
			ASSERT_THAT(IsTrue(MaterializeMatch(
				*World,
				Authority,
				BootstrapContextA,
				AuthorState,
				PoolReceipts[Index],
				Error)));
			ASSERT_THAT(IsTrue(bRegistered));
		}
		ASSERT_THAT(IsTrue(
			PoolReceipts[0].InitialStateDigest
				!= PoolReceipts[1].InitialStateDigest));
		ASSERT_THAT(IsTrue(
			PoolReceipts[0].InitialStateDigest
				!= PoolReceipts[2].InitialStateDigest));
		ASSERT_THAT(IsTrue(
			PoolReceipts[0].InitialStateDigest
				!= PoolReceipts[3].InitialStateDigest));

		FSeinMatchBootstrapReceipt FactionReceipts[3];
		for (int32 Index = 0; Index < 3; ++Index)
		{
			TStrongObjectPtr<USeinInitialStateDigestTestFaction> Faction(
				NewObject<USeinInitialStateDigestTestFaction>(
					GetTransientPackage(),
					MakeUniqueObjectName(
						GetTransientPackage(),
						USeinInitialStateDigestTestFaction::StaticClass(),
						TEXT("SeinInitialStateDigestFaction"))));
			Faction->FactionID = FSeinFactionID(7);
			Faction->ResourceKit.AddDefaulted();
			Faction->ResourceKit[0].bOverrideStartingValue = true;
			Faction->ResourceKit[0].StartingValueOverride =
				FFixedPoint::FromInt(Index == 2 ? 11 : 10);
			FActorTestSpawner Spawner;
			USeinWorldSubsystem* World =
				Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			ASSERT_THAT(IsNotNull(World));
			FString Error;
			FSeinMatchBootstrapAuthorityHandle Authority;
			ASSERT_THAT(IsTrue(ClaimTestAuthority(
				*World, Authority, Error)));
			const auto AuthorState = [&]()
			{
				World->RegisterFaction(Faction.Get());
			};
			ASSERT_THAT(IsTrue(MaterializeMatch(
				*World,
				Authority,
				BootstrapContextA,
				AuthorState,
				FactionReceipts[Index],
				Error)));
		}
		ASSERT_THAT(IsTrue(
			FactionReceipts[0].InitialStateDigest
				== FactionReceipts[1].InitialStateDigest));
		ASSERT_THAT(IsTrue(
			FactionReceipts[0].InitialStateDigest
				!= FactionReceipts[2].InitialStateDigest));
	}

	TEST(MatchBootstrapRejectsDuplicateLivePoolObjectPointers,
		"SeinARTS.Unit.CoreEntity.MatchBootstrap")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		FString Error;
		FSeinMatchBootstrapAuthorityHandle Authority;
		ASSERT_THAT(IsTrue(ClaimTestAuthority(
			*World, Authority, Error)));
		int32 OriginalID = INDEX_NONE;
		int32 DuplicateRegistrationID = INDEX_NONE;
		const auto AuthorState = [&]()
		{
			USeinInitialStateDigestTestAbility* Ability =
				NewObject<USeinInitialStateDigestTestAbility>(World);
			OriginalID = World->RegisterAbilityInstance(Ability);
			DuplicateRegistrationID =
				World->RegisterAbilityInstance(Ability);

			// Public registration rejects the duplicate early. Seed the
			// otherwise unreachable corruption directly so the digest retains
			// its defense-in-depth proof against a malformed live pool.
			FSeinMatchBootstrapPoolTestAccess::
				AppendDuplicateAbilityPointer(*World, *Ability);
		};
		TestRunner->AddExpectedError(
			TEXT("Match bootstrap failed closed"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		TestRunner->AddExpectedError(
			TEXT("transaction closed (failed)"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		FSeinMatchBootstrapReceipt Receipt;
		ASSERT_THAT(IsFalse(MaterializeMatch(
			*World,
			Authority,
			BootstrapContextA,
			AuthorState,
			Receipt,
			Error)));
		ASSERT_THAT(IsTrue(OriginalID != INDEX_NONE));
		ASSERT_THAT(AreEqual(INDEX_NONE, DuplicateRegistrationID));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("duplicate live UObject pointer"))));
	}
}

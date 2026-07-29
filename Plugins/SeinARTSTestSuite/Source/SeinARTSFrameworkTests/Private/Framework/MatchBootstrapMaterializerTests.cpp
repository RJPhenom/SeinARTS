#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "GameMode/SeinMatchBootstrapSubsystem.h"
#include "GameMode/SeinPlayerStart.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		const FGuid MaterializerAuthorizationContext(
			0x91000000, 0x92000000, 0x93000000, 1);
		const FName MaterializerAuthorityID(
			TEXT("SeinFrameworkTests.Materializer"));

		bool ClaimMaterializerAuthority(
			USeinWorldSubsystem& World,
			FSeinMatchBootstrapAuthorityHandle& OutAuthority,
			FString& OutError)
		{
			return World.ClaimMatchBootstrapAuthority(
				MaterializerAuthorityID,
				&World,
				OutAuthority,
				OutError);
		}

		FSeinMatchSlot MakeActiveSlot(
			int32 SlotIndex,
			ESeinSlotState State,
			uint8 Faction,
			uint8 Team)
		{
			FSeinMatchSlot Slot;
			Slot.SlotIndex = SlotIndex;
			Slot.State = State;
			Slot.FactionID = FSeinFactionID(Faction);
			Slot.TeamID = Team;
			return Slot;
		}

		ASeinPlayerStart* SpawnBakedPlayerStart(
			UWorld& World,
			const FSeinMatchSlot& Slot)
		{
			ASeinPlayerStart* Start = World.SpawnActor<ASeinPlayerStart>();
			if (!Start)
			{
				return nullptr;
			}
			Start->PlayerSlot = Slot.SlotIndex;
			Start->FactionID = Slot.FactionID;
			Start->TeamID = Slot.TeamID;
			Start->PlacedSimTransform = FFixedTransform();
			Start->bSimTransformBaked = true;
			Start->SpawnEntity = nullptr;
			return Start;
		}

		bool Materialize(
			FActorTestSpawner& Spawner,
			const FSeinMatchSettings& Settings,
			int64 Seed,
			FSeinMatchBootstrapReceipt& OutReceipt,
			FString& OutError)
		{
			UWorld& World = Spawner.GetWorld();
			USeinWorldSubsystem* Sim =
				World.GetSubsystem<USeinWorldSubsystem>();
			USeinMatchBootstrapSubsystem* Facade =
				World.GetSubsystem<USeinMatchBootstrapSubsystem>();
			if (!Sim || !Facade)
			{
				OutError = TEXT("Framework bootstrap test subsystems are missing.");
				return false;
			}
			FSeinMatchBootstrapAuthorityHandle Authority;
			if (!ClaimMaterializerAuthority(*Sim, Authority, OutError))
			{
				return false;
			}
			if (!Sim->SeedSimRandom(Authority, Seed, OutError))
			{
				return false;
			}
			return Sim->EnsureMatchBootstrapLocallyReady(
				Authority,
				Settings,
				MaterializerAuthorizationContext,
				OutReceipt,
				OutError);
		}
	}

	TEST(FrameworkBootstrapMaterializesAnExactOptionalSpawnAnchor,
		"SeinARTS.Unit.Framework.MatchBootstrap")
	{
		FActorTestSpawner Spawner;
		UWorld& World = Spawner.GetWorld();
		const FSeinMatchSlot Human = MakeActiveSlot(
			1, ESeinSlotState::Human, 4, 2);
		ASSERT_THAT(IsNotNull(SpawnBakedPlayerStart(World, Human)));

		FSeinMatchSettings Settings;
		Settings.Slots = {Human};
		FSeinMatchBootstrapReceipt Receipt;
		FString Error;
		ASSERT_THAT(IsTrue(Materialize(
			Spawner, Settings, 1234, Receipt, Error)));

		USeinWorldSubsystem* Sim =
			World.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Sim));
		ASSERT_THAT(IsTrue(Receipt.IsValid()));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinMatchBootstrapState::LocallyReady),
			static_cast<uint8>(Sim->GetMatchBootstrapState())));
		const FSeinPlayerState* Player =
			Sim->GetPlayerState(FSeinPlayerID(1));
		ASSERT_THAT(IsNotNull(Player));
		ASSERT_THAT(AreEqual(static_cast<uint8>(4), Player->FactionID.Value));
		ASSERT_THAT(AreEqual(static_cast<uint8>(2), Player->TeamID));

		FSeinMatchBootstrapReceipt RetryReceipt;
		FSeinMatchBootstrapAuthorityHandle Authority;
		ASSERT_THAT(IsTrue(ClaimMaterializerAuthority(
			*Sim, Authority, Error)));
		ASSERT_THAT(IsTrue(Sim->EnsureMatchBootstrapLocallyReady(
			Authority,
			Settings,
			MaterializerAuthorizationContext,
			RetryReceipt,
			Error)));
		ASSERT_THAT(IsTrue(RetryReceipt == Receipt));
		ASSERT_THAT(IsTrue(Sim->AuthorizeMatchBootstrap(
			Authority, Receipt, MaterializerAuthorizationContext, Error)));
		ASSERT_THAT(IsTrue(Sim->LaunchAuthorizedMatchBootstrap(
			Authority, Error)));
		Sim->StopSimulation();
	}

	TEST(FrameworkBootstrapRejectsAMissingActiveSlotAnchorBeforePlayerMutation,
		"SeinARTS.Unit.Framework.MatchBootstrap")
	{
		FActorTestSpawner Spawner;
		FSeinMatchSettings Settings;
		Settings.Slots = {
			MakeActiveSlot(1, ESeinSlotState::Human, 4, 2)};

		TestRunner->AddExpectedError(
			TEXT("Match bootstrap failed closed"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("transaction closed (failed)"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		FSeinMatchBootstrapReceipt Receipt;
		FString Error;
		ASSERT_THAT(IsFalse(Materialize(
			Spawner, Settings, 1234, Receipt, Error)));

		USeinWorldSubsystem* Sim = Spawner.GetWorld()
			.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Sim));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinMatchBootstrapState::Failed),
			static_cast<uint8>(Sim->GetMatchBootstrapState())));
		ASSERT_THAT(IsNull(Sim->GetPlayerState(FSeinPlayerID(1))));
		ASSERT_THAT(AreEqual(1, Sim->GetPlayerCount()));
	}

	TEST(FrameworkBootstrapRejectsDuplicateAnchorsForOneActiveSlot,
		"SeinARTS.Unit.Framework.MatchBootstrap")
	{
		FActorTestSpawner Spawner;
		UWorld& World = Spawner.GetWorld();
		const FSeinMatchSlot Human = MakeActiveSlot(
			1, ESeinSlotState::Human, 4, 2);
		ASSERT_THAT(IsNotNull(SpawnBakedPlayerStart(World, Human)));
		ASSERT_THAT(IsNotNull(SpawnBakedPlayerStart(World, Human)));

		FSeinMatchSettings Settings;
		Settings.Slots = {Human};
		TestRunner->AddExpectedError(
			TEXT("Match bootstrap failed closed"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("transaction closed (failed)"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		FSeinMatchBootstrapReceipt Receipt;
		FString Error;
		ASSERT_THAT(IsFalse(Materialize(
			Spawner, Settings, 1234, Receipt, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("exactly one"))));
	}

	TEST(FrameworkBootstrapReceiptIgnoresActorAndSettingsInsertionOrder,
		"SeinARTS.Determinism.Framework.MatchBootstrap")
	{
		const FSeinMatchSlot Human = MakeActiveSlot(
			1, ESeinSlotState::Human, 4, 2);
		const FSeinMatchSlot AI = MakeActiveSlot(
			2, ESeinSlotState::AI, 7, 3);

		FActorTestSpawner FirstSpawner;
		ASSERT_THAT(IsNotNull(SpawnBakedPlayerStart(
			FirstSpawner.GetWorld(), Human)));
		ASSERT_THAT(IsNotNull(SpawnBakedPlayerStart(
			FirstSpawner.GetWorld(), AI)));
		FSeinMatchSettings FirstSettings;
		FirstSettings.Slots = {Human, AI};
		FSeinMatchBootstrapReceipt FirstReceipt;
		FString Error;
		ASSERT_THAT(IsTrue(Materialize(
			FirstSpawner, FirstSettings, 777, FirstReceipt, Error)));

		FActorTestSpawner SecondSpawner;
		ASSERT_THAT(IsNotNull(SpawnBakedPlayerStart(
			SecondSpawner.GetWorld(), AI)));
		ASSERT_THAT(IsNotNull(SpawnBakedPlayerStart(
			SecondSpawner.GetWorld(), Human)));
		FSeinMatchSettings SecondSettings;
		SecondSettings.Slots = {AI, Human};
		FSeinMatchBootstrapReceipt SecondReceipt;
		ASSERT_THAT(IsTrue(Materialize(
			SecondSpawner, SecondSettings, 777, SecondReceipt, Error)));

		ASSERT_THAT(IsTrue(FirstReceipt == SecondReceipt));
	}
}

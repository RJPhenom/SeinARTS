#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Actor/SeinActor.h"
#include "Components/ActorComponent.h"
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

		ASeinActor* SpawnInactivePlacedActor(
			UWorld& World,
			FName ActorName,
			int32 PlayerSlot)
		{
			FActorSpawnParameters Params;
			Params.Name = ActorName;
			ASeinActor* Actor = World.SpawnActor<ASeinActor>(
				ASeinActor::StaticClass(), FTransform::Identity, Params);
			if (!Actor)
			{
				return nullptr;
			}
			Actor->PlayerSlot = PlayerSlot;
			Actor->bSimLocationBaked = true;
			Actor->bSimRotationBaked = true;
			return Actor;
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

	TEST(FrameworkBootstrapAcceptsFactionlessOccupiedSlot,
		"SeinARTS.Unit.Framework.MatchBootstrap")
	{
		// Factions are an OPT-IN catalog: a project with zero authored
		// faction assets fields occupied slots with the invalid/zero
		// FactionID (the lobby auto-claim assigns none), and bootstrap must
		// accept them — this is the shipped Sandbox baseline, and requiring
		// a faction here once refused every factionless project in PIE while
		// all automation fixtures happened to author factions.
		FActorTestSpawner Spawner;
		UWorld& World = Spawner.GetWorld();
		const FSeinMatchSlot Factionless = MakeActiveSlot(
			1, ESeinSlotState::Human, /*Faction=*/0, /*Team=*/0);
		ASSERT_THAT(IsFalse(Factionless.FactionID.IsValid()));
		ASSERT_THAT(IsNotNull(SpawnBakedPlayerStart(World, Factionless)));

		FSeinMatchSettings Settings;
		Settings.Slots = {Factionless};
		FSeinMatchBootstrapReceipt Receipt;
		FString Error;
		ASSERT_THAT(IsTrue(Materialize(
			Spawner, Settings, 777, Receipt, Error)));

		USeinWorldSubsystem* Sim =
			World.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Sim));
		ASSERT_THAT(IsTrue(Receipt.IsValid()));
		const FSeinPlayerState* Player =
			Sim->GetPlayerState(FSeinPlayerID(1));
		ASSERT_THAT(IsNotNull(Player));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(0), Player->FactionID.Value));
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

	TEST(FrameworkBootstrapOmitsPlacedActorsForInactiveSlots,
		"SeinARTS.Unit.Framework.MatchBootstrap")
	{
		FActorTestSpawner Spawner;
		UWorld& World = Spawner.GetWorld();
		const FSeinMatchSlot Human = MakeActiveSlot(
			1, ESeinSlotState::Human, 4, 2);
		const FSeinMatchSlot Open = MakeActiveSlot(
			2, ESeinSlotState::Open, 7, 3);
		ASSERT_THAT(IsNotNull(SpawnBakedPlayerStart(World, Human)));
		ASSERT_THAT(IsNotNull(SpawnBakedPlayerStart(World, Open)));

		ASeinActor* InactiveActor = SpawnInactivePlacedActor(
			World, TEXT("InactiveSlotTwo"), 2);
		ASSERT_THAT(IsNotNull(InactiveActor));

		FSeinMatchSettings Settings;
		Settings.Slots = {Human, Open};
		FSeinMatchBootstrapReceipt Receipt;
		FString Error;
		ASSERT_THAT(IsTrue(Materialize(
			Spawner, Settings, 4567, Receipt, Error)));
		ASSERT_THAT(IsTrue(Receipt.IsValid()));
		ASSERT_THAT(IsTrue(InactiveActor->IsHidden()));
		ASSERT_THAT(IsFalse(InactiveActor->GetActorEnableCollision()));
		ASSERT_THAT(IsFalse(InactiveActor->IsActorTickEnabled()));
		ASSERT_THAT(IsFalse(InactiveActor->HasValidEntity()));
		TInlineComponentArray<UActorComponent*> Components(InactiveActor);
		for (const UActorComponent* Component : Components)
		{
			ASSERT_THAT(IsFalse(Component->IsComponentTickEnabled()));
		}
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
		const FSeinMatchSlot OpenTwo = MakeActiveSlot(
			3, ESeinSlotState::Open, 8, 4);
		const FSeinMatchSlot OpenThree = MakeActiveSlot(
			4, ESeinSlotState::Open, 9, 5);

		FActorTestSpawner FirstSpawner;
		ASSERT_THAT(IsNotNull(SpawnBakedPlayerStart(
			FirstSpawner.GetWorld(), Human)));
		ASSERT_THAT(IsNotNull(SpawnBakedPlayerStart(
			FirstSpawner.GetWorld(), AI)));
		ASSERT_THAT(IsNotNull(SpawnInactivePlacedActor(
			FirstSpawner.GetWorld(), TEXT("InactiveAlpha"), 3)));
		ASSERT_THAT(IsNotNull(SpawnInactivePlacedActor(
			FirstSpawner.GetWorld(), TEXT("InactiveBravo"), 4)));
		FSeinMatchSettings FirstSettings;
		FirstSettings.Slots = {Human, AI, OpenTwo, OpenThree};
		FSeinMatchBootstrapReceipt FirstReceipt;
		FString Error;
		ASSERT_THAT(IsTrue(Materialize(
			FirstSpawner, FirstSettings, 777, FirstReceipt, Error)));

		FActorTestSpawner SecondSpawner;
		ASSERT_THAT(IsNotNull(SpawnBakedPlayerStart(
			SecondSpawner.GetWorld(), AI)));
		ASSERT_THAT(IsNotNull(SpawnBakedPlayerStart(
			SecondSpawner.GetWorld(), Human)));
		ASSERT_THAT(IsNotNull(SpawnInactivePlacedActor(
			SecondSpawner.GetWorld(), TEXT("InactiveBravo"), 4)));
		ASSERT_THAT(IsNotNull(SpawnInactivePlacedActor(
			SecondSpawner.GetWorld(), TEXT("InactiveAlpha"), 3)));
		FSeinMatchSettings SecondSettings;
		SecondSettings.Slots = {OpenThree, AI, OpenTwo, Human};
		FSeinMatchBootstrapReceipt SecondReceipt;
		ASSERT_THAT(IsTrue(Materialize(
			SecondSpawner, SecondSettings, 777, SecondReceipt, Error)));

		ASSERT_THAT(IsTrue(FirstReceipt == SecondReceipt));

		FActorTestSpawner MissingInactiveSpawner;
		ASSERT_THAT(IsNotNull(SpawnBakedPlayerStart(
			MissingInactiveSpawner.GetWorld(), Human)));
		ASSERT_THAT(IsNotNull(SpawnBakedPlayerStart(
			MissingInactiveSpawner.GetWorld(), AI)));
		ASSERT_THAT(IsNotNull(SpawnInactivePlacedActor(
			MissingInactiveSpawner.GetWorld(), TEXT("InactiveAlpha"), 3)));
		FSeinMatchBootstrapReceipt MissingInactiveReceipt;
		ASSERT_THAT(IsTrue(Materialize(
			MissingInactiveSpawner,
			FirstSettings,
			777,
			MissingInactiveReceipt,
			Error)));
		ASSERT_THAT(IsFalse(FirstReceipt == MissingInactiveReceipt));
	}
}

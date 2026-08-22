#include "CQTest.h"

#include "Engine/GameInstance.h"
#include "GameMode/SeinGameMode.h"
#include "SeinLobbySubsystem.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		FSeinMatchSlot MakeSlot(int32 SlotIndex, ESeinSlotState State)
		{
			FSeinMatchSlot Slot;
			Slot.SlotIndex = SlotIndex;
			Slot.State = State;
			return Slot;
		}

		bool BuildAutoStartSettings(
			const FSeinMatchSettings& Settings,
			const TSet<int32>& BoundSlots,
			FSeinMatchSettings* FinalizedSettings = nullptr,
			int32 ExpectedPIEPlayers = 1,
			bool bEnabled = true,
			EWorldType::Type WorldType = EWorldType::PIE,
			bool bPIEViaConsole = false,
			ENetMode NetMode = NM_ListenServer,
			bool bExternal = false,
			bool bPublished = false)
		{
			FSeinMatchSettings Finalized;
			const bool bResult =
				ASeinGameMode::BuildMultiplayerPIEAutoStartSettingsForTests(
				bEnabled,
				/*bNetworkingEnabled=*/true,
				WorldType,
				bPIEViaConsole,
				NetMode,
				bExternal,
				bPublished,
				ExpectedPIEPlayers,
				Settings,
				BoundSlots,
				Finalized);
			if (FinalizedSettings)
			{
				*FinalizedSettings = MoveTemp(Finalized);
			}
			return bResult;
		}
	}

	TEST(MultiplayerPIEAutoStartUsesRequestedPlayerCount,
		"SeinARTS.Unit.Framework.MultiplayerPIEAutoStart")
	{
		FSeinMatchSettings Settings;
		Settings.Slots = {
			MakeSlot(1, ESeinSlotState::Human),
			MakeSlot(2, ESeinSlotState::Human),
			MakeSlot(3, ESeinSlotState::AI),
			MakeSlot(4, ESeinSlotState::Open)};
		Settings.Slots[1].FactionID = FSeinFactionID(7);
		Settings.Slots[1].TeamID = 3;

		FSeinMatchSettings OnePlayerSettings;
		ASSERT_THAT(IsTrue(BuildAutoStartSettings(
			Settings, {1}, &OnePlayerSettings, /*ExpectedPIEPlayers=*/1)));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinSlotState::Human),
			static_cast<uint8>(OnePlayerSettings.Slots[0].State)));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinSlotState::Open),
			static_cast<uint8>(OnePlayerSettings.Slots[1].State)));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(7),
			OnePlayerSettings.Slots[1].FactionID.Value));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(3), OnePlayerSettings.Slots[1].TeamID));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinSlotState::AI),
			static_cast<uint8>(OnePlayerSettings.Slots[2].State)));

		ASSERT_THAT(IsFalse(BuildAutoStartSettings(
			Settings, {1}, nullptr, /*ExpectedPIEPlayers=*/2)));
		ASSERT_THAT(IsTrue(BuildAutoStartSettings(
			Settings, {1, 2}, nullptr, /*ExpectedPIEPlayers=*/2)));
	}

	TEST(MultiplayerPIEAutoStartSupportsListenAndDedicatedAuthority,
		"SeinARTS.Unit.Framework.MultiplayerPIEAutoStart")
	{
		FSeinMatchSettings Settings;
		Settings.Slots = {MakeSlot(1, ESeinSlotState::Human)};
		const TSet<int32> BoundSlots = {1};

		ASSERT_THAT(IsTrue(BuildAutoStartSettings(
			Settings, BoundSlots, nullptr, 1, true, EWorldType::PIE, false,
			NM_ListenServer)));
		ASSERT_THAT(IsTrue(BuildAutoStartSettings(
			Settings, BoundSlots, nullptr, 1, true, EWorldType::PIE, false,
			NM_DedicatedServer)));
		ASSERT_THAT(IsFalse(BuildAutoStartSettings(
			Settings, BoundSlots, nullptr, 1, true, EWorldType::PIE, false,
			NM_Client)));
		ASSERT_THAT(IsFalse(BuildAutoStartSettings(
			Settings, BoundSlots, nullptr, 1, true, EWorldType::PIE, false,
			NM_Standalone)));
		ASSERT_THAT(IsTrue(BuildAutoStartSettings(
			Settings, BoundSlots, nullptr, 1, true, EWorldType::Game,
			/*bPIEViaConsole=*/true, NM_DedicatedServer)));
	}

	TEST(MultiplayerPIEAutoStartLeavesNonDirectFlowsExplicit,
		"SeinARTS.Unit.Framework.MultiplayerPIEAutoStart")
	{
		FSeinMatchSettings Settings;
		Settings.Slots = {MakeSlot(1, ESeinSlotState::Human)};
		const TSet<int32> BoundSlots = {1};

		ASSERT_THAT(IsFalse(BuildAutoStartSettings(
			Settings, BoundSlots, nullptr, 1, /*bEnabled=*/false)));
		ASSERT_THAT(IsFalse(BuildAutoStartSettings(
			Settings, BoundSlots, nullptr, 1, true, EWorldType::Game,
			/*bPIEViaConsole=*/false)));
		ASSERT_THAT(IsFalse(BuildAutoStartSettings(
			Settings, BoundSlots, nullptr, 1, true, EWorldType::PIE,
			false, NM_ListenServer, /*bExternal=*/true)));
		ASSERT_THAT(IsFalse(BuildAutoStartSettings(
			Settings, BoundSlots, nullptr, 1, true, EWorldType::PIE,
			false, NM_ListenServer, false, /*bPublished=*/true)));
	}

	TEST(MultiplayerPIEAutoStartRequiresNetworkingAndAHumanSlot,
		"SeinARTS.Unit.Framework.MultiplayerPIEAutoStart")
	{
		FSeinMatchSettings Settings;
		Settings.Slots = {MakeSlot(1, ESeinSlotState::AI)};
		ASSERT_THAT(IsFalse(BuildAutoStartSettings(Settings, {})));

		Settings.Slots = {MakeSlot(1, ESeinSlotState::Human)};
		FSeinMatchSettings Finalized;
		ASSERT_THAT(IsFalse(
			ASeinGameMode::BuildMultiplayerPIEAutoStartSettingsForTests(
				/*bSettingEnabled=*/true,
				/*bNetworkingEnabled=*/false,
				EWorldType::PIE,
				/*bPIEViaConsole=*/false,
				NM_ListenServer,
				/*bHasExternalBootstrap=*/false,
				/*bHasPublishedSnapshot=*/false,
				/*ExpectedPIEPlayers=*/1,
				Settings,
				{1},
				Finalized)));
	}

	TEST(DirectPIELobbyDefaultsPreserveAuthoredMetadata,
		"SeinARTS.Unit.Framework.MultiplayerPIEAutoStart")
	{
		FSeinMatchSlot Human = MakeSlot(1, ESeinSlotState::Human);
		Human.FactionID = FSeinFactionID(7);
		Human.TeamID = 3;
		const FSeinLobbySlotState HumanDefault =
			USeinLobbySubsystem::BuildDirectMatchSlotForTests(Human);
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinSlotState::Open),
			static_cast<uint8>(HumanDefault.State)));
		ASSERT_THAT(IsFalse(HumanDefault.bClaimed));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(7), HumanDefault.FactionID.Value));
		ASSERT_THAT(AreEqual(static_cast<uint8>(3), HumanDefault.TeamID));

		FSeinMatchSlot AI = MakeSlot(2, ESeinSlotState::AI);
		AI.FactionID = FSeinFactionID(9);
		AI.TeamID = 4;
		const FSeinLobbySlotState AIDefault =
			USeinLobbySubsystem::BuildDirectMatchSlotForTests(AI);
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinSlotState::AI),
			static_cast<uint8>(AIDefault.State)));
		ASSERT_THAT(IsTrue(AIDefault.bClaimed));
		ASSERT_THAT(IsTrue(AIDefault.bReady));
		ASSERT_THAT(AreEqual(static_cast<uint8>(2), AIDefault.ClaimedBy.Value));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(9), AIDefault.FactionID.Value));
		ASSERT_THAT(AreEqual(static_cast<uint8>(4), AIDefault.TeamID));

		const FSeinLobbySlotState CommittedHuman =
			USeinLobbySubsystem::BuildCommittedMatchSlotForTests(Human);
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinSlotState::Human),
			static_cast<uint8>(CommittedHuman.State)));
		ASSERT_THAT(IsTrue(CommittedHuman.bClaimed));
		ASSERT_THAT(IsTrue(CommittedHuman.bDisconnected));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(1), CommittedHuman.ClaimedBy.Value));

		ASSERT_THAT(IsTrue(
			USeinLobbySubsystem::IsOpenSlotAvailableForTests(
				HumanDefault, /*bLaunchCommitted=*/false)));
		ASSERT_THAT(IsFalse(
			USeinLobbySubsystem::IsOpenSlotAvailableForTests(
				HumanDefault, /*bLaunchCommitted=*/true)));
	}

	TEST(DirectPIEFailedPreparationPreservesMapDefaults,
		"SeinARTS.Unit.Framework.MultiplayerPIEAutoStart")
	{
		UGameInstance* GameInstance = NewObject<UGameInstance>();
		USeinLobbySubsystem* Lobby =
			NewObject<USeinLobbySubsystem>(GameInstance);
		ASSERT_THAT(IsNotNull(Lobby));

		FSeinMatchSettings Defaults;
		Defaults.Slots = {
			MakeSlot(1, ESeinSlotState::Human),
			MakeSlot(2, ESeinSlotState::Human)};
		Lobby->SetDirectMatchSettingsDefaults(Defaults);
		ASSERT_THAT(IsTrue(
			Lobby->InstallPreparedMatchSettingsSnapshot(Defaults)));
		ASSERT_THAT(IsTrue(
			Lobby->DiscardUncommittedPreparedMatchSettingsSnapshot()));
		ASSERT_THAT(AreEqual(
			static_cast<int32>(2),
			Lobby->GetDirectMatchSettingsDefaultsForTests().Slots.Num()));
	}

}

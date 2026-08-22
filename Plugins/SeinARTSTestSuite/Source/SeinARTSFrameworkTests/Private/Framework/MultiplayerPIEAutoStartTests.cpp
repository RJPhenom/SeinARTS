#include "CQTest.h"

#include "GameMode/SeinGameMode.h"

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

		bool ShouldAutoStart(
			const FSeinMatchSettings& Settings,
			const TSet<int32>& BoundSlots,
			bool bEnabled = true,
			EWorldType::Type WorldType = EWorldType::PIE,
			bool bPIEViaConsole = false,
			ENetMode NetMode = NM_ListenServer,
			bool bExternal = false,
			bool bPublished = false)
		{
			return ASeinGameMode::ShouldAutoStartMultiplayerPIEForTests(
				bEnabled,
				/*bNetworkingEnabled=*/true,
				WorldType,
				bPIEViaConsole,
				NetMode,
				bExternal,
				bPublished,
				Settings,
				BoundSlots);
		}
	}

	TEST(MultiplayerPIEAutoStartWaitsForEveryHumanSlot,
		"SeinARTS.Unit.Framework.MultiplayerPIEAutoStart")
	{
		FSeinMatchSettings Settings;
		Settings.Slots = {
			MakeSlot(1, ESeinSlotState::Human),
			MakeSlot(2, ESeinSlotState::Human),
			MakeSlot(3, ESeinSlotState::AI),
			MakeSlot(4, ESeinSlotState::Open)};

		ASSERT_THAT(IsFalse(ShouldAutoStart(Settings, {1})));
		ASSERT_THAT(IsTrue(ShouldAutoStart(Settings, {1, 2})));
	}

	TEST(MultiplayerPIEAutoStartSupportsListenAndDedicatedAuthority,
		"SeinARTS.Unit.Framework.MultiplayerPIEAutoStart")
	{
		FSeinMatchSettings Settings;
		Settings.Slots = {MakeSlot(1, ESeinSlotState::Human)};
		const TSet<int32> BoundSlots = {1};

		ASSERT_THAT(IsTrue(ShouldAutoStart(
			Settings, BoundSlots, true, EWorldType::PIE, false,
			NM_ListenServer)));
		ASSERT_THAT(IsTrue(ShouldAutoStart(
			Settings, BoundSlots, true, EWorldType::PIE, false,
			NM_DedicatedServer)));
		ASSERT_THAT(IsFalse(ShouldAutoStart(
			Settings, BoundSlots, true, EWorldType::PIE, false,
			NM_Client)));
		ASSERT_THAT(IsFalse(ShouldAutoStart(
			Settings, BoundSlots, true, EWorldType::PIE, false,
			NM_Standalone)));
		ASSERT_THAT(IsTrue(ShouldAutoStart(
			Settings, BoundSlots, true, EWorldType::Game,
			/*bPIEViaConsole=*/true, NM_DedicatedServer)));
	}

	TEST(MultiplayerPIEAutoStartLeavesNonDirectFlowsExplicit,
		"SeinARTS.Unit.Framework.MultiplayerPIEAutoStart")
	{
		FSeinMatchSettings Settings;
		Settings.Slots = {MakeSlot(1, ESeinSlotState::Human)};
		const TSet<int32> BoundSlots = {1};

		ASSERT_THAT(IsFalse(ShouldAutoStart(
			Settings, BoundSlots, /*bEnabled=*/false)));
		ASSERT_THAT(IsFalse(ShouldAutoStart(
			Settings, BoundSlots, true, EWorldType::Game,
			/*bPIEViaConsole=*/false)));
		ASSERT_THAT(IsFalse(ShouldAutoStart(
			Settings, BoundSlots, true, EWorldType::PIE,
			false, NM_ListenServer, /*bExternal=*/true)));
		ASSERT_THAT(IsFalse(ShouldAutoStart(
			Settings, BoundSlots, true, EWorldType::PIE,
			false, NM_ListenServer, false, /*bPublished=*/true)));
	}

	TEST(MultiplayerPIEAutoStartRequiresNetworkingAndAHumanSlot,
		"SeinARTS.Unit.Framework.MultiplayerPIEAutoStart")
	{
		FSeinMatchSettings Settings;
		Settings.Slots = {MakeSlot(1, ESeinSlotState::AI)};
		ASSERT_THAT(IsFalse(ShouldAutoStart(Settings, {})));

		Settings.Slots = {MakeSlot(1, ESeinSlotState::Human)};
		ASSERT_THAT(IsFalse(
			ASeinGameMode::ShouldAutoStartMultiplayerPIEForTests(
				/*bSettingEnabled=*/true,
				/*bNetworkingEnabled=*/false,
				EWorldType::PIE,
				/*bPIEViaConsole=*/false,
				NM_ListenServer,
				/*bHasExternalBootstrap=*/false,
				/*bHasPublishedSnapshot=*/false,
				Settings,
				{1})));
	}
}

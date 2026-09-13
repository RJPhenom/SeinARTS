/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         LobbySeamlessTravelTests.cpp
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Preserves exact lobby ownership through engine controller replacement.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "TestGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameMode/SeinGameMode.h"
#include "GameMode/SeinPlayerStart.h"
#include "Player/SeinPlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "OnlineSubsystemTypes.h"
#include "SeinLobbyState.h"
#include "SeinLobbySubsystem.h"
#include "SeinNetSubsystem.h"
#include "SeinNetRelay.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "UObject/StrongObjectPtr.h"

namespace UE::SeinARTSTests
{
	TEST(EngineSeamlessHandoffRequiresPreviouslyAdmittedSeat,
		"SeinARTS.Unit.Network.Lobby")
	{
		TGuardValue<bool> Networking(GetMutableDefault<USeinARTSCoreSettings>()->bNetworkingEnabled, true);
		for (const bool bAdmitted : { false, true })
		{
			FActorTestSpawner Spawner;
			Spawner.InitializeGameSubsystems();
			UWorld& World = Spawner.GetWorld();
			FURL URL;
			URL.AddOption(TEXT("game=/Script/SeinARTSFramework.SeinGameMode"));
			ASSERT_THAT(IsTrue(World.SetGameMode(URL)));
			ASeinGameMode* Mode = Cast<ASeinGameMode>(World.GetAuthGameMode());
			ASSERT_THAT(IsNotNull(Mode));
			FSeinMatchSettings Settings;
			FSeinMatchSlot Slot;
			Slot.SlotIndex = 1;
			Slot.State = ESeinSlotState::Human;
			Settings.Slots.Add(Slot);
			ASeinPlayerStart& Start = Spawner.SpawnActor<ASeinPlayerStart>();
			Start.PlayerSlot = 1;
			Start.FactionID = Slot.FactionID;
			Start.TeamID = Slot.TeamID;
			Start.PlacedSimTransform = FFixedTransform();
			Start.bSimTransformBaked = true;
			Start.SpawnEntity = nullptr;
			Mode->ResolvedMatchSettings = Settings;
			Mode->bMatchSettingsResolved = true;
			USeinWorldSubsystem* Sim = World.GetSubsystem<USeinWorldSubsystem>();
			ASSERT_THAT(IsNotNull(Sim));
			FSeinMatchBootstrapAuthorityHandle Authority;
			FSeinMatchBootstrapReceipt Receipt;
			FString Error;
			ASSERT_THAT(IsTrue(Sim->ClaimMatchBootstrapAuthority(TEXT("SeamlessFixture"), Sim, Authority, Error)));
			ASSERT_THAT(IsTrue(Sim->SeedSimRandom(Authority, 1234, Error)));
			ASSERT_THAT(IsTrue(Sim->EnsureMatchBootstrapLocallyReady(
				Authority, Settings, FGuid(1, 2, 3, 4), Receipt, Error)));
			USeinNetSubsystem* Net = Spawner.GetGameInstance()->GetSubsystem<USeinNetSubsystem>();
			ASSERT_THAT(IsNotNull(Net));
			Net->SetConnectionAdmissionBindingForTests(FSeinMatchInstanceID(FGuid(1, 2, 3, 4)),
				FSeinNetworkParticipantID(FGuid(5, 6, 7, 8)), FSeinPlayerID(1));
			USeinLobbySubsystem* Lobby = Spawner.GetGameInstance()->GetSubsystem<USeinLobbySubsystem>();
			ASSERT_THAT(IsNotNull(Lobby));
			Lobby->SetDirectMatchSettingsDefaults(Settings);
			ASeinPlayerController& Original = Spawner.SpawnActor<ASeinPlayerController>();
			Original.SeinPlayerID = FSeinPlayerID(1);
			ASSERT_THAT(IsNotNull(Original.PlayerState));
			TStrongObjectPtr<ULocalPlayer> LocalPlayer(NewObject<ULocalPlayer>(GEngine));
			Original.SetPlayer(LocalPlayer.Get());
			if (bAdmitted) Lobby->LoginControllerForTests(Mode, &Original);
			ASSERT_THAT(IsTrue(Lobby->InstallPreparedMatchSettingsSnapshot(Settings)));
			Lobby->ConfirmPublishedMatchSettingsLaunch();
			ASSERT_THAT(IsTrue(Net->GetRelays().IsEmpty()));
			TWeakObjectPtr<APlayerController> Replacement;
			const FDelegateHandle SpawnedHandle = World.AddOnActorSpawnedHandler(
				FOnActorSpawned::FDelegate::CreateLambda([&](AActor* Actor)
				{
					if (APlayerController* PC = Cast<APlayerController>(Actor)) Replacement = PC;
				}));
			if (!bAdmitted) TestRunner->AddExpectedError(TEXT("seamless controller handoff lost its admitted lobby seat"),
				EAutomationExpectedErrorFlags::Contains, 1);
			AController* Current = &Original;
			Mode->HandleSeamlessTravelPlayer(Current);
			World.RemoveOnActorSpawnedHandler(SpawnedHandle);
			ASSERT_THAT(IsTrue(Original.IsActorBeingDestroyed()));
			if (bAdmitted)
			{
				ASSERT_THAT(IsNotNull(Current));
				ASSERT_THAT(IsTrue(Current == Replacement.Get()));
				ASSERT_THAT(AreEqual(1, Net->GetRelays().Num()));
				ASSERT_THAT(IsTrue(Net->GetRelays()[0]->GetOwner() == Current));
			}
			else
			{
				ASSERT_THAT(IsNull(Current));
				ASSERT_THAT(IsFalse(Replacement.IsValid()));
				ASSERT_THAT(IsTrue(Net->GetRelays().IsEmpty()));
			}
			Lobby->ResetForLocalSessionExit();
		}
	}

	TEST(SeamlessReplacementRetainsExactReconnectSeat,
		"SeinARTS.Unit.Network.Lobby")
	{
		FActorTestSpawner Spawner;
		Spawner.InitializeGameSubsystems();
		USeinLobbySubsystem* Lobby = Spawner.GetGameInstance()->GetSubsystem<USeinLobbySubsystem>();
		ASSERT_THAT(IsNotNull(Lobby));
		TGuardValue<float> Grace(GetMutableDefault<USeinARTSCoreSettings>()->LobbyReconnectGraceSeconds, 60.0f);
		FSeinMatchSettings Settings;
		FSeinMatchSlot Slot;
		Slot.SlotIndex = 1;
		Slot.State = ESeinSlotState::Human;
		Settings.Slots.Add(Slot);
		Slot.SlotIndex = 2;
		Settings.Slots.Add(Slot);
		Lobby->SetDirectMatchSettingsDefaults(Settings);
		AGameModeBase& Mode = Spawner.SpawnActor<AGameModeBase>();
		APlayerController& Original = Spawner.SpawnActor<APlayerController>();
		Original.PlayerState = &Spawner.SpawnActor<APlayerState>();
		const FUniqueNetIdRef IdentityValue = FUniqueNetIdString::Create(TEXT("seamless-owner"), TEXT("NULL"));
		const FUniqueNetIdRef OtherIdentityValue = FUniqueNetIdString::Create(TEXT("different-owner"), TEXT("NULL"));
		const FUniqueNetIdRepl Identity(IdentityValue);
		const FUniqueNetIdRepl OtherIdentity(OtherIdentityValue);
		Original.PlayerState->SetUniqueId(Identity);
		Lobby->LoginControllerForTests(&Mode, &Original);
		ASSERT_THAT(IsNotNull(Lobby->GetLobbyState()));
		ASSERT_THAT(IsTrue(Lobby->GetLobbyState()->FindSlot(1)->bClaimed));
		APlayerController& AlreadyBound = Spawner.SpawnActor<APlayerController>();
		AlreadyBound.PlayerState = &Spawner.SpawnActor<APlayerState>();
		AlreadyBound.PlayerState->SetUniqueId(OtherIdentity);
		Lobby->LoginControllerForTests(&Mode, &AlreadyBound);
		ASSERT_THAT(IsFalse(Lobby->RebindSeamlessController(&Original, &AlreadyBound)));
		ASSERT_THAT(IsTrue(Lobby->InstallPreparedMatchSettingsSnapshot(Settings)));
		Lobby->ConfirmPublishedMatchSettingsLaunch();
		ASeinLobbyState* PreviousState = Lobby->GetLobbyState();
		Lobby->NotifyLobbyStateActorEndPlay(PreviousState);
		PreviousState->Destroy();
		ASSERT_THAT(IsFalse(Lobby->CanAcceptConnection(Identity)));

		APlayerController& Replacement = Spawner.SpawnActor<APlayerController>();
		Replacement.PlayerState = &Spawner.SpawnActor<APlayerState>();
		Replacement.PlayerState->SetUniqueId(Identity);
		FActorTestSpawner ForeignSpawner;
		APlayerController& Foreign = ForeignSpawner.SpawnActor<APlayerController>();
		ASSERT_THAT(IsFalse(Lobby->RebindSeamlessController(&Original, &Foreign)));

		FSeinMatchSettings Inconsistent = Settings;
		Inconsistent.Slots[0].State = ESeinSlotState::AI;
		ASSERT_THAT(IsTrue(Lobby->InstallPreparedMatchSettingsSnapshot(Inconsistent)));
		ASSERT_THAT(IsFalse(Lobby->RebindSeamlessController(&Original, &Replacement)));
		ASSERT_THAT(IsTrue(Lobby->InstallPreparedMatchSettingsSnapshot(Settings)));

		USeinNetSubsystem* Net = Spawner.GetGameInstance()->GetSubsystem<USeinNetSubsystem>();
		ASSERT_THAT(IsNotNull(Net));
		const FSeinMatchInstanceID MatchID(FGuid(1, 2, 3, 4));
		const FSeinNetworkParticipantID ParticipantID(FGuid(5, 6, 7, 8));
		Net->SetConnectionAdmissionBindingForTests(MatchID, ParticipantID, FSeinPlayerID(2));
		const FName AdmissionOwner(TEXT("SeamlessFixture"));
		ASSERT_THAT(IsTrue(Net->RegisterConnectionAdmissionAuthorizer(AdmissionOwner,
			FSeinConnectionAdmissionAuthorizer::CreateLambda([&](const FSeinConnectionAdmissionRequest&)
			{
				FSeinConnectionAdmissionDecision Decision;
				Decision.bAccepted = true;
				Decision.MatchID = MatchID;
				Decision.ParticipantID = ParticipantID;
				Decision.AssignedSlot = FSeinPlayerID(2);
				return Decision;
			}))));
		FString Error;
		FSeinPlayerID AuthorizedSlot;
		const FString Options(TEXT("?SeinAdmission=seamless-fixture"));
		ASSERT_THAT(IsTrue(Net->AuthorizeIncomingConnection(Options, TEXT("127.0.0.1"), Identity, Error)));
		ASSERT_THAT(IsTrue(Net->ConsumeAuthorizedConnection(&Original, Options, Identity, AuthorizedSlot, Error)));
		ASSERT_THAT(IsFalse(Lobby->RebindSeamlessController(&Original, &Replacement)));
		ASSERT_THAT(IsTrue(Net->GetAuthorizedConnectionSlot(&Original, AuthorizedSlot)));
		ASSERT_THAT(IsTrue(AuthorizedSlot == FSeinPlayerID(2)));
		ASSERT_THAT(IsFalse(Net->GetAuthorizedConnectionSlot(&Replacement, AuthorizedSlot)));
		Net->UnregisterConnectionAdmissionAuthorizer(AdmissionOwner);
		// UE destroys the old PlayerState before its SwapPlayerControllers hook.
		Original.PlayerState = nullptr;
		ASSERT_THAT(IsTrue(Lobby->RebindSeamlessController(&Original, &Replacement)));
		ASSERT_THAT(IsTrue(Lobby->RebindSeamlessController(&Replacement, &Replacement)));
		Lobby->LogoutControllerForTests(&Original);
		ASSERT_THAT(IsFalse(Lobby->GetLobbyState()->FindSlot(1)->bDisconnected));
		ASSERT_THAT(IsFalse(Lobby->CanAcceptConnection(Identity)));

		APlayerController& Unbound = Spawner.SpawnActor<APlayerController>();
		Unbound.PlayerState = &Spawner.SpawnActor<APlayerState>();
		ASSERT_THAT(IsFalse(Lobby->RebindSeamlessController(&Unbound, &Replacement)));
		ASSERT_THAT(IsFalse(Lobby->RebindSeamlessController(&Replacement, nullptr)));
		Lobby->LogoutControllerForTests(&Replacement);
		ASSERT_THAT(IsTrue(Lobby->GetLobbyState()->FindSlot(1)->bDisconnected));
		ASSERT_THAT(IsTrue(Lobby->CanAcceptConnection(Identity)));
		ASSERT_THAT(IsFalse(Lobby->CanAcceptConnection(OtherIdentity)));
		ASSERT_THAT(IsFalse(Lobby->CanAcceptConnection(FUniqueNetIdRepl())));
		Lobby->ResetForLocalSessionExit();
	}
}

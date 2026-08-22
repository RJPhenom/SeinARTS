/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinGameMode.cpp
 */

#include "GameMode/SeinGameMode.h"

#include "Core/SeinPlayerState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameMode/SeinPlayerStart.h"
#include "GameMode/SeinWorldSettings.h"
#include "HUD/SeinHUD.h"
#include "Player/SeinCameraPawn.h"
#include "Player/SeinPlayerController.h"
#include "SeinLobbySubsystem.h"
#include "SeinNetSubsystem.h"
#include "Serialization/SeinDeterministicValueDigest.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinMatchBootstrapBarrier.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace
{
	bool HasExternalBootstrapIntent(const UWorld& World)
	{
		if (const ASeinWorldSettings* Settings =
			Cast<ASeinWorldSettings>(World.GetWorldSettings()))
		{
			if (Settings->BootstrapIntent
				== ESeinWorldBootstrapIntent::ExternalOrchestrator)
			{
				return true;
			}
		}
		const TCHAR* IntentOption = World.URL.GetOption(
			TEXT("SeinBootstrap="), nullptr);
		return IntentOption
			&& FCString::Stricmp(
				IntentOption, TEXT("ExternalOrchestrator")) == 0;
	}
}

ASeinGameMode::ASeinGameMode()
{
	DefaultPawnClass = ASeinCameraPawn::StaticClass();
	PlayerControllerClass = ASeinPlayerController::StaticClass();
	HUDClass = ASeinHUD::StaticClass();

	// Preserve controller identity and the active net driver across lobby ->
	// gameplay travel. ASeinPlayerController::SeamlessTravelFrom carries the
	// already established gameplay slot into this world's frozen manifest.
	bUseSeamlessTravel = true;
}

void ASeinGameMode::PreLogin(
	const FString& Options,
	const FString& Address,
	const FUniqueNetIdRepl& UniqueId,
	FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		USeinNetSubsystem* Net =
			GameInstance->GetSubsystem<USeinNetSubsystem>();
		if (Net && Net->HasConnectionAdmissionAuthorizer())
		{
			Net->AuthorizeIncomingConnection(
				Options, Address, UniqueId, ErrorMessage);
			return;
		}

		if (const USeinLobbySubsystem* Lobby =
			GameInstance->GetSubsystem<USeinLobbySubsystem>())
		{
			if (!Lobby->CanAcceptConnection(UniqueId))
			{
				ErrorMessage = TEXT("Server is full");
				UE_LOG(LogTemp, Log,
					TEXT("SeinGameMode: rejected connection from %s because the frozen lobby capacity is full."),
					*Address);
			}
		}
	}
}

FString ASeinGameMode::InitNewPlayer(
	APlayerController* NewPlayerController,
	const FUniqueNetIdRepl& UniqueId,
	const FString& Options,
	const FString& Portal)
{
	USeinNetSubsystem* ConsumedAdmission = nullptr;
	ASeinPlayerController* AdmittedController = nullptr;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USeinNetSubsystem* Net =
			GameInstance->GetSubsystem<USeinNetSubsystem>())
		{
			if (Net->HasConnectionAdmissionAuthorizer())
			{
				ASeinPlayerController* SeinController =
					Cast<ASeinPlayerController>(NewPlayerController);
				if (!SeinController)
				{
					return TEXT("Online admission requires a Sein player controller");
				}
				FSeinPlayerID AssignedSlot;
				FString AdmissionError;
				if (!Net->ConsumeAuthorizedConnection(
						NewPlayerController,
						Options,
						UniqueId,
						AssignedSlot,
						AdmissionError))
				{
					return AdmissionError;
				}
				if (const USeinLobbySubsystem* Lobby =
					GameInstance->GetSubsystem<USeinLobbySubsystem>())
				{
					if (!Lobby->CanAcceptConnectionAtSlot(
							AssignedSlot, SeinController))
					{
						Net->ReleaseAuthorizedConnection(SeinController);
						return TEXT("Online admission seat is unavailable");
					}
				}
				SeinController->SeinPlayerID = AssignedSlot;
				ConsumedAdmission = Net;
				AdmittedController = SeinController;
			}
		}
	}
	const FString InitError = Super::InitNewPlayer(
		NewPlayerController, UniqueId, Options, Portal);
	if (!InitError.IsEmpty() && ConsumedAdmission && AdmittedController)
	{
		ConsumedAdmission->ReleaseAuthorizedConnection(AdmittedController);
		AdmittedController->SeinPlayerID = FSeinPlayerID::Neutral();
	}
	return InitError;
}

void ASeinGameMode::Logout(AController* Exiting)
{
	ASeinPlayerController* SeinController =
		Cast<ASeinPlayerController>(Exiting);
	for (auto It = ClaimedSlots.CreateIterator(); It; ++It)
	{
		if (!It.Value().IsValid() || It.Value().Get() == SeinController)
		{
			It.RemoveCurrent();
		}
	}
	Super::Logout(Exiting);
}

#if WITH_DEV_AUTOMATION_TESTS
void ASeinGameMode::CompletePostLoginForTests(APlayerController* NewPlayer)
{
	OnPostLogin(NewPlayer);
	HandleStartingNewPlayer(NewPlayer);
}
#endif

void ASeinGameMode::InitGame(
	const FString& MapName,
	const FString& Options,
	FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}

	const FSeinMatchSettings* SourceSettings = ResolveMatchSettingsForWorld();
	if (!SourceSettings)
	{
		UE_LOG(LogTemp, Log,
			TEXT("SeinGameMode: no match manifest; controllers remain presentation-only."));
		return;
	}

	FSeinMatchSettings CanonicalSettings = *SourceSettings;
	FGuid SettingsDigest;
	FSeinDeterministicValueDigestError DigestError;
	if (!SeinCanonicalizeAndDigestMatchSettings(
		CanonicalSettings, SettingsDigest, &DigestError))
	{
		ErrorMessage = FString::Printf(
			TEXT("Invalid SeinARTS match manifest (%s: %s)."),
			*DigestError.FieldPath, *DigestError.Message);
		UE_LOG(LogTemp, Error, TEXT("SeinGameMode: %s"), *ErrorMessage);
		return;
	}

	ResolvedMatchSettings = MoveTemp(CanonicalSettings);
	bMatchSettingsResolved = true;

	// Admission capacity is GameInstance-scoped and may be queried before the
	// destination lobby actor exists. Stage only the immutable slot count here;
	// no deterministic player/entity state is touched by GameMode.
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USeinLobbySubsystem* Lobby =
			GameInstance->GetSubsystem<USeinLobbySubsystem>())
		{
			Lobby->SetSlotCountOverride(ResolvedMatchSettings.Slots.Num());
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("SeinGameMode: staged canonical manifest with %d slot(s), digest=%s."),
		ResolvedMatchSettings.Slots.Num(),
		*SettingsDigest.ToString(EGuidFormats::Digits));
}

const FSeinMatchSettings* ASeinGameMode::ResolveMatchSettingsForWorld() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	if (World->GetNetMode() == NM_Standalone
		&& HasExternalBootstrapIntent(*World))
	{
		// The external adapter owns both contract and participant semantics.
		// GameMode must not synthesize local Human bindings ahead of it.
		return nullptr;
	}

	if (UGameInstance* GameInstance = World->GetGameInstance())
	{
		if (USeinLobbySubsystem* Lobby =
			GameInstance->GetSubsystem<USeinLobbySubsystem>())
		{
			if (Lobby->HasPublishedSnapshot()
				&& !Lobby->GetPublishedSnapshot().Slots.IsEmpty())
			{
				return &Lobby->GetPublishedSnapshot();
			}
		}
	}

	SynthesizedMatchSettings =
		ASeinPlayerStart::SynthesizeMatchSettingsFromLevel(World);
	return SynthesizedMatchSettings.Slots.IsEmpty()
		? nullptr
		: &SynthesizedMatchSettings;
}

AActor* ASeinGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (!bMatchSettingsResolved)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	ASeinPlayerController* SeinController = Cast<ASeinPlayerController>(Player);
	const auto TryHumanSlot = [this, SeinController](int32 SlotIndex)
		-> ASeinPlayerStart*
	{
		const FSeinMatchSlot* Slot = FindManifestSlot(SlotIndex);
		if (!Slot || Slot->State != ESeinSlotState::Human
			|| IsSlotClaimedByAnother(SlotIndex, SeinController))
		{
			return nullptr;
		}
		return FindPlayerStartForSlot(SlotIndex);
	};

	// Seamless travel preserves the principal's established gameplay slot,
	// independent of connection order in the destination world.
	if (SeinController && SeinController->SeinPlayerID.IsValid())
	{
		const int32 EstablishedSlot = SeinController->SeinPlayerID.Value;
		if (ASeinPlayerStart* Start = TryHumanSlot(EstablishedSlot))
		{
			return Start;
		}
		UE_LOG(LogTemp, Error,
			TEXT("SeinGameMode: established controller %s cannot route to frozen Human slot %d."),
			*GetNameSafe(SeinController), EstablishedSlot);
		return nullptr;
	}

	for (const FSeinMatchSlot& Slot : ResolvedMatchSettings.Slots)
	{
		if (Slot.State != ESeinSlotState::Human)
		{
			continue;
		}
		if (ASeinPlayerStart* Start = TryHumanSlot(Slot.SlotIndex))
		{
			return Start;
		}
	}

	UE_LOG(LogTemp, Error,
		TEXT("SeinGameMode: no unclaimed frozen Human slot is available for %s; Open/AI/Closed slots are not controller fallbacks."),
		*GetNameSafe(Player));
	return nullptr;
}

ASeinPlayerStart* ASeinGameMode::FindPlayerStartForSlot(int32 SlotIndex) const
{
	if (SlotIndex <= 0 || !GetWorld())
	{
		return nullptr;
	}

	ASeinPlayerStart* Match = nullptr;
	for (TActorIterator<ASeinPlayerStart> It(GetWorld()); It; ++It)
	{
		if (It->PlayerSlot != SlotIndex)
		{
			continue;
		}
		if (Match)
		{
			UE_LOG(LogTemp, Error,
				TEXT("SeinGameMode: PlayerSlot %d has duplicate starts (%s, %s)."),
				SlotIndex, *Match->GetPathName(), *It->GetPathName());
			return nullptr;
		}
		Match = *It;
	}
	return Match;
}

void ASeinGameMode::HandleStartingNewPlayer_Implementation(
	APlayerController* NewPlayer)
{
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);

	ASeinPlayerController* SeinController =
		Cast<ASeinPlayerController>(NewPlayer);
	if (!SeinController)
	{
		UE_LOG(LogTemp, Error,
			TEXT("SeinGameMode: controller %s is not an ASeinPlayerController."),
			*GetNameSafe(NewPlayer));
		return;
	}
	if (!bMatchSettingsResolved)
	{
		UE_LOG(LogTemp, Log,
			TEXT("SeinGameMode: %s has no match manifest and remains presentation-only."),
			*SeinController->GetName());
		return;
	}

	int32 SlotIndex = SeinController->SeinPlayerID.IsValid()
		? static_cast<int32>(SeinController->SeinPlayerID.Value)
		: 0;
	if (SlotIndex == 0)
	{
		if (const ASeinPlayerStart* Start =
			Cast<ASeinPlayerStart>(SeinController->StartSpot.Get()))
		{
			SlotIndex = Start->PlayerSlot;
		}
	}

	const FSeinMatchSlot* Slot = FindManifestSlot(SlotIndex);
	if (!Slot || Slot->State != ESeinSlotState::Human)
	{
		UE_LOG(LogTemp, Error,
			TEXT("SeinGameMode: %s cannot bind slot %d; only an existing frozen Human slot is routable."),
			*SeinController->GetName(), SlotIndex);
		return;
	}
	if (IsSlotClaimedByAnother(SlotIndex, SeinController))
	{
		UE_LOG(LogTemp, Error,
			TEXT("SeinGameMode: slot %d is already bound to another controller; %s was rejected."),
			SlotIndex, *SeinController->GetName());
		return;
	}

	USeinWorldSubsystem* WorldSubsystem =
		GetWorld() ? GetWorld()->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSubsystem)
	{
		UE_LOG(LogTemp, Error,
			TEXT("SeinGameMode: no Core world subsystem is available for manifest verification."));
		return;
	}

	const FSeinPlayerID PlayerID(static_cast<uint8>(SlotIndex));
	if (WorldSubsystem->GetMatchBootstrapState()
		== ESeinMatchBootstrapState::Failed)
	{
		UE_LOG(LogTemp, Error,
			TEXT("SeinGameMode: %s cannot bind because this world's match bootstrap failed terminally."),
			*SeinController->GetName());
		return;
	}
	if (const FSeinPlayerState* State = WorldSubsystem->GetPlayerState(PlayerID))
	{
		if (State->FactionID != Slot->FactionID || State->TeamID != Slot->TeamID)
		{
			UE_LOG(LogTemp, Error,
				TEXT("SeinGameMode: materialized state for slot %d disagrees with the frozen manifest."),
				SlotIndex);
			return;
		}
	}
	else
	{
		const ESeinMatchBootstrapState BootstrapState =
			WorldSubsystem->GetMatchBootstrapState();
		if (BootstrapState != ESeinMatchBootstrapState::Awaiting
			&& BootstrapState != ESeinMatchBootstrapState::Applying)
		{
			UE_LOG(LogTemp, Error,
				TEXT("SeinGameMode: slot %d has no materialized player state after bootstrap state %d."),
				SlotIndex, static_cast<int32>(BootstrapState));
			return;
		}
	}

	// PIE login may precede OnWorldBeginPlay. Binding the controller and relay
	// to immutable manifest identity is safe here; the transaction remains the
	// only code allowed to create deterministic player/entity state.
	SeinController->SeinPlayerID = PlayerID;
	ClaimedSlots.Add(SlotIndex, SeinController);

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USeinNetSubsystem* Net =
			GameInstance->GetSubsystem<USeinNetSubsystem>())
		{
			Net->ServerSpawnRelayForController(SeinController, PlayerID);
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("SeinGameMode: bound %s to frozen Human slot %d%s."),
		*SeinController->GetName(), SlotIndex,
		WorldSubsystem->GetPlayerState(PlayerID)
			? TEXT("")
			: TEXT(" before deterministic materialization"));
}

const FSeinMatchSlot* ASeinGameMode::FindManifestSlot(int32 SlotIndex) const
{
	return bMatchSettingsResolved
		? ResolvedMatchSettings.Slots.FindByPredicate(
			[SlotIndex](const FSeinMatchSlot& Slot)
			{
				return Slot.SlotIndex == SlotIndex;
			})
		: nullptr;
}

bool ASeinGameMode::IsSlotClaimedByAnother(
	int32 SlotIndex,
	const ASeinPlayerController* Controller) const
{
	const TWeakObjectPtr<ASeinPlayerController>* Existing =
		ClaimedSlots.Find(SlotIndex);
	return Existing && Existing->IsValid() && Existing->Get() != Controller;
}

int32 ASeinGameMode::GetEffectiveMaxPlayers() const
{
	if (bMatchSettingsResolved && !ResolvedMatchSettings.Slots.IsEmpty())
	{
		return ResolvedMatchSettings.Slots.Num();
	}
	const USeinARTSCoreSettings* Settings =
		GetDefault<USeinARTSCoreSettings>();
	return Settings && Settings->MaxPlayers > 0
		? Settings->MaxPlayers
		: 16;
}

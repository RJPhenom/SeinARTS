/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLobbySubsystem.cpp
 */

#include "SeinLobbySubsystem.h"
#include "SeinARTSNet.h"
#include "SeinLobbyState.h"
#include "SeinNetSubsystem.h"
#include "SeinNetRelay.h"
#include "Settings/PluginSettings.h"
#include "Subsystems/SeinFactionService.h"
#include "Data/SeinMatchSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/AssetManager.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Controller.h"
#include "Engine/NetConnection.h"
#include "Net/UnrealNetwork.h"
#include "Containers/Ticker.h"
#include "TimerManager.h"

namespace
{
	/** Pull the controller's connection address ("IP:Port") for the slot's
	 *  RemoteAddress field. Listen-server host PC has no NetConnection (host
	 *  is local) — return empty string. Standalone same. Remote clients
	 *  return their connection's `LowLevelGetRemoteAddress(true)` (true =
	 *  include port). */
	FString ResolveRemoteAddressForPC(APlayerController* PC)
	{
		if (!PC) return FString();
		if (UNetConnection* Conn = PC->GetNetConnection())
		{
			return Conn->LowLevelGetRemoteAddress(/*bAppendPort=*/true);
		}
		return FString();
	}
}

void USeinLobbySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bModuleOwnedStateReleased = false;

	PostLoginHandle = FGameModeEvents::GameModePostLoginEvent.AddUObject(this, &USeinLobbySubsystem::OnPostLogin);
	LogoutHandle    = FGameModeEvents::GameModeLogoutEvent.AddUObject(this, &USeinLobbySubsystem::OnLogout);

	UE_LOG(LogSeinNet, Log, TEXT("USeinLobbySubsystem initialized."));
}

void USeinLobbySubsystem::Deinitialize()
{
	ReleaseModuleOwnedStateForModuleUnload();
	Super::Deinitialize();
}

void USeinLobbySubsystem::ReleaseModuleOwnedStateForModuleUnload()
{
	check(IsInGameThread());
	if (bModuleOwnedStateReleased) return;
	bModuleOwnedStateReleased = true;

	if (PostLoginHandle.IsValid())
	{
		FGameModeEvents::GameModePostLoginEvent.Remove(PostLoginHandle);
		PostLoginHandle.Reset();
	}
	if (LogoutHandle.IsValid())
	{
		FGameModeEvents::GameModeLogoutEvent.Remove(LogoutHandle);
		LogoutHandle.Reset();
	}

	// Cancel any in-flight grace-timer entries (subsystem outlives the
	// individual lobby world but timers tick on the global core ticker).
	for (auto& Pair : PendingReclaimTimers)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Pair.Value);
	}
	PendingReclaimTimers.Reset();

	if (PendingTravelTimerHandle.IsValid())
	{
		if (UWorld* TimerWorld = PendingTravelTimerWorld.Get())
		{
			TimerWorld->GetTimerManager().ClearTimer(
				PendingTravelTimerHandle);
		}
		PendingTravelTimerHandle.Invalidate();
	}
	PendingTravelTimerWorld.Reset();

	LobbyStateActor.Reset();
	ControllerToSlot.Reset();
	PublishedSnapshot = FSeinMatchSettings();
	bSnapshotPublished = false;
	bTravelScheduled = false;
	SlotCountOverride = 0;
}

bool USeinLobbySubsystem::IsServer() const
{
	const UWorld* World = GetWorld();
	if (!World) return false;
	const ENetMode Mode = World->GetNetMode();
	// Standalone counts as "server" for lobby purposes — single-player builds
	// still benefit from a slot manifest if a designer authored one. The lobby
	// just won't have any networked claims.
	return Mode == NM_DedicatedServer || Mode == NM_ListenServer || Mode == NM_Standalone;
}

bool USeinLobbySubsystem::IsHostController(APlayerController* PC) const
{
	if (!PC) return false;
	if (!IsServer()) return false;
	// On a listen server / standalone, the local PC is the host. Dedicated
	// servers have no host PC — every PC is a remote client; host-only
	// commands rejected for all PCs (server-side console commands bypass
	// this via direct subsystem calls).
	return PC->IsLocalController();
}

USeinFactionService* USeinLobbySubsystem::GetFactionService() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<USeinFactionService>() : nullptr;
}

FSeinMatchSettings USeinLobbySubsystem::ResolveBaseSettings() const
{
	// Plugin settings owns the default Extensions array. Slots come from
	// the runtime lobby actor and are overlaid in BuildMatchSettingsSnapshot.
	FSeinMatchSettings Out;
	if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
	{
		Out.Extensions = Settings->DefaultMatchExtensions;
	}
	return Out;
}

TSoftObjectPtr<UWorld> USeinLobbySubsystem::ResolveGameplayMap() const
{
	// 1. Lobby's selected map (host's pick from the dropdown — replicated).
	if (const ASeinLobbyState* Actor = LobbyStateActor.Get())
	{
		if (!Actor->SelectedMap.IsNull())
		{
			return Actor->SelectedMap;
		}
	}
	// 2. Runtime override on this subsystem (settable from a Widget BP).
	if (!GameplayMap.IsNull()) return GameplayMap;
	// 3. Plugin-settings ship-time default.
	if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
	{
		return Settings->DefaultGameplayMap;
	}
	return nullptr;
}

FSeinMatchSlot USeinLobbySubsystem::ProjectLobbySlotToMatchSlot(const FSeinLobbySlotState& In)
{
	FSeinMatchSlot Out;
	Out.SlotIndex   = In.SlotIndex;
	Out.State       = In.State;
	Out.FactionID   = In.FactionID;
	Out.TeamID      = In.TeamID;
	Out.AIProfile   = In.AIProfile;
	return Out;
}

void USeinLobbySubsystem::OnPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
	if (!GameMode || !NewPlayer) return;
	if (!IsServer()) return;

	UWorld* World = GameMode->GetWorld();
	if (!World) return;
	if (World->GetGameInstance() != GetGameInstance())
	{
		// A different GI's game mode (sub-PIE world) — ignore.
		return;
	}

	// Lazy-init: spawn the lobby actor + seed slots on first PostLogin. Picks
	// `SlotCountOverride` (set by GameMode InitGame from ResolvedMatchSettings)
	// if non-zero, else falls back to `PluginSettings::AvailableMaps[0]` then
	// `PluginSettings::MaxPlayers`.
	//
	// The lobby actor exists in BOTH lobby maps AND gameplay maps by design —
	// it owns slot identity (claimed-by, last-claimant-NetID for reconnect)
	// across the match lifetime. In gameplay maps, the slot count must
	// reflect the actual level's SeinPlayerStarts (synthesized via the
	// GameMode's match-settings resolve, then pushed to the LobbySubsystem
	// via `SetSlotCountOverride`). Without that override, EnsureLobbyActor
	// would default to AvailableMaps[0].SlotCount (typically 2 for "1 vs. 1")
	// and reject connections on larger gameplay maps.
	EnsureLobbyActor();

	ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor) return;

	int32 TargetSlot = 0;
	bool bReconnect = false;
	bool bHasExternallyAuthorizedSlot = false;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (const USeinNetSubsystem* Net =
			GameInstance->GetSubsystem<USeinNetSubsystem>())
		{
			if (Net->HasConnectionAdmissionAuthorizer())
			{
				FSeinPlayerID AuthorizedSlot;
				if (!Net->GetAuthorizedConnectionSlot(
						NewPlayer, AuthorizedSlot))
				{
					UE_LOG(LogSeinNet, Error,
						TEXT("[Lobby] OnPostLogin: external admission did not retain an exact slot for %s."),
						*GetNameSafe(NewPlayer));
					return;
				}
				TargetSlot = AuthorizedSlot.Value;
				bHasExternallyAuthorizedSlot = true;
			}
		}
	}

	if (!bHasExternallyAuthorizedSlot)
	{
		// Legacy lobby reconnects retain their slot by transport identity.
		TargetSlot = FindDisconnectedSlotForPC(NewPlayer);
		bReconnect = TargetSlot > 0;
		if (!bReconnect)
		{
			TargetSlot = PickNextFreeSlot();
		}
	}
	if (TargetSlot <= 0)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] OnPostLogin: no free slot for %s — lobby is full (%d slot(s))."),
			*GetNameSafe(NewPlayer), Actor->Slots.Num());
		return;
	}

	FSeinLobbySlotState* Slot = Actor->FindSlotMutable(TargetSlot);
	if (!Slot) return;
	if (bHasExternallyAuthorizedSlot)
	{
		if (!CanAcceptConnectionAtSlot(
				FSeinPlayerID(static_cast<uint8>(TargetSlot)), NewPlayer))
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("[Lobby] OnPostLogin: externally authorized slot %d is unavailable in lobby state."),
				TargetSlot);
			return;
		}
		for (auto It = ControllerToSlot.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
		const int32* ExistingControllerSlot = ControllerToSlot.Find(NewPlayer);
		const bool bSameController =
			ExistingControllerSlot && *ExistingControllerSlot == TargetSlot;
		bReconnect = Slot->bDisconnected || bSameController;
	}

	if (bReconnect)
	{
		// Restoring a previously-disconnected slot: clear the disconnected
		// flag, cancel the grace timer, keep faction/team/etc.
		Slot->bDisconnected = false;
		Slot->bClaimed = true;
		// State was kept as Human during the disconnect window; reaffirm.
		Slot->State = ESeinSlotState::Human;
		CancelSlotReclaim(TargetSlot);

		UE_LOG(LogSeinNet, Log,
			TEXT("[Lobby] OnPostLogin: reconnect-rebind slot %d for %s"),
			TargetSlot, *GetNameSafe(NewPlayer));
	}
	else
	{
		// Fresh claim.
		Slot->State = ESeinSlotState::Human;
		Slot->bClaimed = true;
		Slot->bDisconnected = false;
		Slot->bReady = false;
		Slot->ClaimedBy = FSeinPlayerID(static_cast<uint8>(TargetSlot));

		// Read the joining PC's player name; PlayerState may be momentarily
		// null on PostLogin in some edge paths — fall back to a numeric label.
		FString PlayerName;
		if (NewPlayer->PlayerState)
		{
			PlayerName = NewPlayer->PlayerState->GetPlayerName();
		}
		if (PlayerName.IsEmpty())
		{
			PlayerName = FString::Printf(TEXT("Player %d"), TargetSlot);
		}
		Slot->DisplayName = FText::FromString(PlayerName);

		UE_LOG(LogSeinNet, Log,
			TEXT("[Lobby] OnPostLogin: claimed slot %d for %s (%s)."),
			TargetSlot, *GetNameSafe(NewPlayer),
			bHasExternallyAuthorizedSlot
				? TEXT("externally authorized")
				: TEXT("auto-assigned"));
	}

	// Stamp the most recent claimant's NetID so a future disconnect can be
	// matched on reconnect, plus the remote address for UI identity display.
	if (NewPlayer->PlayerState)
	{
		Slot->LastClaimantNetID = NewPlayer->PlayerState->GetUniqueId();
	}
	Slot->RemoteAddress = ResolveRemoteAddressForPC(NewPlayer);

	ControllerToSlot.Add(TWeakObjectPtr<APlayerController>(NewPlayer), TargetSlot);

	CommitSlotState(TargetSlot, *Slot);
}

void USeinLobbySubsystem::OnLogout(AGameModeBase* GameMode, AController* Exiting)
{
	if (!Exiting || !IsServer()) return;

	APlayerController* PC = Cast<APlayerController>(Exiting);
	if (!PC) return;

	ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor) return;

	// Find the slot this controller owned.
	int32 ReleasedSlot = 0;
	for (auto It = ControllerToSlot.CreateIterator(); It; ++It)
	{
		if (It.Key().Get() == PC)
		{
			ReleasedSlot = It.Value();
			It.RemoveCurrent();
			break;
		}
	}
	if (ReleasedSlot <= 0) return;

	FSeinLobbySlotState* Slot = Actor->FindSlotMutable(ReleasedSlot);
	if (!Slot) return;

	// Reconnection-aware logout: instead of fully releasing the slot, mark
	// it disconnected and schedule a grace-timer to revert to Open if no
	// reconnect arrives. LastClaimantNetID was stamped on PostLogin (and
	// any subsequent claim) — used by FindDisconnectedSlotForPC.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const float Grace = Settings ? Settings->LobbyReconnectGraceSeconds : 60.0f;

	if (Grace > 0.0f && Slot->LastClaimantNetID.IsValid())
	{
		Slot->bDisconnected = true;
		Slot->bReady = false;
		// bClaimed stays TRUE — slot is reserved for the absent player.
		// State stays Human.

		UE_LOG(LogSeinNet, Log,
			TEXT("[Lobby] OnLogout: slot %d disconnected (grace=%.1fs) — was %s."),
			ReleasedSlot, Grace, *GetNameSafe(PC));

		ScheduleSlotReclaim(ReleasedSlot);
		CommitSlotState(ReleasedSlot, *Slot);
	}
	else
	{
		// No grace configured (or no NetID) — fully release the slot.
		Slot->State = ESeinSlotState::Open;
		Slot->bClaimed = false;
		Slot->bDisconnected = false;
		Slot->bReady = false;
		Slot->ClaimedBy = FSeinPlayerID::Neutral();
		Slot->DisplayName = FText::FromString(TEXT("Open"));
		Slot->LastClaimantNetID = FUniqueNetIdRepl();
		Slot->RemoteAddress.Empty();
		// Keep FactionID + TeamID so a future-claimer sees what was set.

		UE_LOG(LogSeinNet, Log,
			TEXT("[Lobby] OnLogout: released slot %d (was %s)."),
			ReleasedSlot, *GetNameSafe(PC));

		CommitSlotState(ReleasedSlot, *Slot);
	}
}

void USeinLobbySubsystem::InitializeLobby(int32 SlotCount)
{
	if (!IsServer()) return;

	if (SlotCount <= 0)
	{
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		SlotCount = (Settings && Settings->MaxPlayers > 0) ? Settings->MaxPlayers : 4;
	}

	EnsureLobbyActor();
	ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor) return;

	Actor->Slots.Reset();
	Actor->Slots.Reserve(SlotCount);
	for (int32 i = 1; i <= SlotCount; ++i)
	{
		FSeinLobbySlotState S;
		S.SlotIndex = i;
		S.State = ESeinSlotState::Open;
		S.DisplayName = FText::FromString(TEXT("Open"));
		Actor->Slots.Add(S);
	}

	ControllerToSlot.Reset();

	// Cancel any in-flight reclaim timers — full reseed obsoletes them.
	for (auto& Pair : PendingReclaimTimers)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Pair.Value);
	}
	PendingReclaimTimers.Reset();

	// MarkDirty so the freshly-seeded array replicates immediately.
	Actor->ForceNetUpdate();
	Actor->OnLobbyStateChanged.Broadcast();

	UE_LOG(LogSeinNet, Log, TEXT("[Lobby] InitializeLobby: seeded %d Open slot(s)."), SlotCount);
}

bool USeinLobbySubsystem::ServerHandleSlotClaim(APlayerController* PC, int32 SlotIndex, FSeinFactionID Faction)
{
	if (!IsServer() || !PC) return false;

	ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor)
	{
		UE_LOG(LogSeinNet, Warning, TEXT("[Lobby] ServerHandleSlotClaim: no lobby actor."));
		return false;
	}

	FSeinLobbySlotState* Target = Actor->FindSlotMutable(SlotIndex);
	if (!Target)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerHandleSlotClaim: slot %d does not exist (PC=%s)."),
			SlotIndex, *GetNameSafe(PC));
		return false;
	}

	if (Target->State == ESeinSlotState::Closed)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerHandleSlotClaim: slot %d is Closed (PC=%s)."),
			SlotIndex, *GetNameSafe(PC));
		return false;
	}

	// Faction validation via the FactionService. Faction.Value == 0 (Neutral)
	// is a valid "unset" placeholder during slot claim; only non-zero values
	// must resolve to a known faction.
	if (Faction.Value != 0)
	{
		USeinFactionService* FS = GetFactionService();
		if (FS && !FS->IsFactionValid(Faction))
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("[Lobby] ServerHandleSlotClaim: faction %u not registered with FactionService (PC=%s)."),
				Faction.Value, *GetNameSafe(PC));
			return false;
		}

		// Faction-allowlist filtering is designer-driven now — no framework
		// preset to check. Designer who wants per-match-type faction restriction
		// reads their own rule struct from `FSeinMatchSettings::Extensions`
		// and validates client-side before submitting the claim, OR adds
		// validation in their game's USeinFactionService subclass.
	}

	// Identify the requesting controller's CURRENT slot (if any).
	int32 CurrentSlot = 0;
	for (const auto& Pair : ControllerToSlot)
	{
		if (Pair.Key.Get() == PC)
		{
			CurrentSlot = Pair.Value;
			break;
		}
	}

	if (CurrentSlot == SlotIndex)
	{
		// Same-slot re-claim: just update the faction.
		Target->FactionID = Faction;
		UE_LOG(LogSeinNet, Log,
			TEXT("[Lobby] ServerHandleSlotClaim: %s updated faction on slot %d → %u"),
			*GetNameSafe(PC), SlotIndex, Faction.Value);
		CommitSlotState(SlotIndex, *Target);
		return true;
	}

	// Cross-slot move: target must be free (Open + not claimed). Disconnected-
	// reserved slots are NOT claimable by other PCs (only the original
	// claimant can rebind via OnPostLogin reconnect path).
	if (Target->bClaimed)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerHandleSlotClaim: slot %d already claimed (PC=%s)."),
			SlotIndex, *GetNameSafe(PC));
		return false;
	}

	// Release current slot if PC had one.
	if (CurrentSlot > 0)
	{
		if (FSeinLobbySlotState* Old = Actor->FindSlotMutable(CurrentSlot))
		{
			Old->State = ESeinSlotState::Open;
			Old->bClaimed = false;
			Old->bDisconnected = false;
			Old->bReady = false;
			Old->ClaimedBy = FSeinPlayerID::Neutral();
			Old->DisplayName = FText::FromString(TEXT("Open"));
			Old->LastClaimantNetID = FUniqueNetIdRepl();
			Old->RemoteAddress.Empty();
			CommitSlotState(CurrentSlot, *Old);
		}
		ControllerToSlot.Remove(TWeakObjectPtr<APlayerController>(PC));
	}

	Target->State = ESeinSlotState::Human;
	Target->bClaimed = true;
	Target->bDisconnected = false;
	Target->bReady = false;
	Target->ClaimedBy = FSeinPlayerID(static_cast<uint8>(SlotIndex));
	Target->FactionID = Faction;
	Target->RemoteAddress = ResolveRemoteAddressForPC(PC);
	if (PC->PlayerState)
	{
		Target->LastClaimantNetID = PC->PlayerState->GetUniqueId();
		const FString Name = PC->PlayerState->GetPlayerName();
		if (!Name.IsEmpty())
		{
			Target->DisplayName = FText::FromString(Name);
		}
	}
	if (Target->DisplayName.IsEmpty() ||
		Target->DisplayName.EqualTo(FText::FromString(TEXT("Open"))))
	{
		Target->DisplayName = FText::FromString(FString::Printf(TEXT("Player %d"), SlotIndex));
	}

	ControllerToSlot.Add(TWeakObjectPtr<APlayerController>(PC), SlotIndex);

	// Rebind the PC's relay to the NEW slot so the client's view of "what
	// slot am I" stays in sync with the lobby state. Without this, after a
	// slot move:
	//   - Relay->AssignedPlayerID stays as the OLD slot (set on PostLogin)
	//   - Net->GetLocalPlayerID() returns the OLD slot
	//   - ViewModel reads OLD slot's bReady (=false, vacated) → Ready toggle
	//     reads stale data and sticks at "ready" because every flip computes
	//     from the wrong slot.
	// Updating AssignedPlayerID server-side replicates to the owning client
	// via OnRep_AssignedPlayerID, which calls Net->NotifyLocalSlotAssigned
	// and refreshes the client's LocalPlayerID.
	//
	// Note: PC->SeinPlayerID is NOT updated here — that field is only
	// consumed by ASeinGameMode::HandleStartingNewPlayer at match start,
	// which re-derives the slot binding from the published lobby snapshot.
	// (Updating it here would require depending on SeinARTSFramework, a
	// circular dep — and is unnecessary for the lobby phase.)
	const FSeinPlayerID NewSlotID(static_cast<uint8>(SlotIndex));
	if (UGameInstance* GI = GetGameInstance())
	{
		if (USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>())
		{
			for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Net->GetRelays())
			{
				ASeinNetRelay* R = Wp.Get();
				if (R && R->GetOwner() == PC)
				{
					if (R->AssignedPlayerID != NewSlotID)
					{
						R->AssignedPlayerID = NewSlotID;
						R->ForceNetUpdate();
						UE_LOG(LogSeinNet, Log,
							TEXT("[Lobby] ServerHandleSlotClaim: rebound relay %s to slot %d"),
							*GetNameSafe(R), SlotIndex);

						// On a listen server, OnRep_AssignedPlayerID does NOT fire
						// for the host's own relay (replication doesn't deliver to
						// self). Without this, Net->LocalPlayerID stays as the OLD
						// slot for the host — IsLocalReady reads stale data and
						// the Ready toggle sticks. Mirror what RegisterRelay does
						// at spawn: update the lobby slot directly when the
						// relay's owner is local. Remote clients still get this
						// via OnRep_AssignedPlayerID — they're unaffected.
						if (PC->IsLocalController())
						{
							Net->NotifyLocalLobbySlotAssigned(R, NewSlotID);
						}
					}
					break;
				}
			}
		}
	}

	UE_LOG(LogSeinNet, Log,
		TEXT("[Lobby] ServerHandleSlotClaim: %s claimed slot %d  faction=%u"),
		*GetNameSafe(PC), SlotIndex, Faction.Value);

	CommitSlotState(SlotIndex, *Target);
	return true;
}

bool USeinLobbySubsystem::ServerHandleSetReady(APlayerController* PC, bool bReady)
{
	if (!IsServer() || !PC) return false;

	ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor) return false;

	// Find the PC's slot.
	int32 SlotIndex = 0;
	for (const auto& Pair : ControllerToSlot)
	{
		if (Pair.Key.Get() == PC) { SlotIndex = Pair.Value; break; }
	}
	if (SlotIndex <= 0)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerHandleSetReady: PC %s has no claimed slot."),
			*GetNameSafe(PC));
		return false;
	}

	FSeinLobbySlotState* Slot = Actor->FindSlotMutable(SlotIndex);
	if (!Slot) return false;
	if (Slot->State != ESeinSlotState::Human)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerHandleSetReady: slot %d is not Human (state=%d)."),
			SlotIndex, (int32)Slot->State);
		return false;
	}

	Slot->bReady = bReady;
	UE_LOG(LogSeinNet, Log,
		TEXT("[Lobby] ServerHandleSetReady: slot %d  bReady=%d (PC=%s)"),
		SlotIndex, (int32)bReady, *GetNameSafe(PC));

	CommitSlotState(SlotIndex, *Slot);
	return true;
}

bool USeinLobbySubsystem::ServerHandleSetTeam(APlayerController* HostPC, int32 SlotIndex, uint8 TeamID)
{
	if (!IsServer()) return false;
	if (HostPC && !IsHostController(HostPC))
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerHandleSetTeam: rejected — non-host PC %s."),
			*GetNameSafe(HostPC));
		return false;
	}

	ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor) return false;

	FSeinLobbySlotState* Slot = Actor->FindSlotMutable(SlotIndex);
	if (!Slot)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerHandleSetTeam: slot %d does not exist."), SlotIndex);
		return false;
	}

	Slot->TeamID = TeamID;
	UE_LOG(LogSeinNet, Log,
		TEXT("[Lobby] ServerHandleSetTeam: slot %d  TeamID=%u"), SlotIndex, TeamID);

	CommitSlotState(SlotIndex, *Slot);
	return true;
}

bool USeinLobbySubsystem::ServerHandleLeave(APlayerController* PC)
{
	if (!IsServer() || !PC) return false;

	// Find the PC's current slot via the bookkeeping map (more reliable than
	// scanning ClaimedBy on the slot — survives PC->SeinPlayerID drift).
	int32 SlotIndex = 0;
	if (const int32* Found = ControllerToSlot.Find(TWeakObjectPtr<APlayerController>(PC)))
	{
		SlotIndex = *Found;
	}
	if (SlotIndex <= 0)
	{
		UE_LOG(LogSeinNet, Verbose,
			TEXT("[Lobby] ServerHandleLeave: PC %s has no claimed slot (already left?)."),
			*GetNameSafe(PC));
		return false;
	}

	ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor) return false;

	FSeinLobbySlotState* Slot = Actor->FindSlotMutable(SlotIndex);
	if (!Slot) return false;

	// Cancel any pending reconnect-grace timer for this slot. The voluntary
	// leave bypasses grace entirely — no point waiting for a reclaim that
	// the player has explicitly opted out of.
	CancelSlotReclaim(SlotIndex);

	// Fully reset the slot to Open. Same shape as ServerHandleKickPlayer's
	// post-kick reset.
	Slot->State = ESeinSlotState::Open;
	Slot->bClaimed = false;
	Slot->bDisconnected = false;
	Slot->bReady = false;
	Slot->FactionID = FSeinFactionID();
	Slot->ClaimedBy = FSeinPlayerID::Neutral();
	Slot->AIProfile = FGameplayTag();
	Slot->DisplayName = FText::FromString(TEXT("Open"));
	Slot->LastClaimantNetID = FUniqueNetIdRepl();
	Slot->RemoteAddress.Empty();

	ControllerToSlot.Remove(TWeakObjectPtr<APlayerController>(PC));

	UE_LOG(LogSeinNet, Log,
		TEXT("[Lobby] ServerHandleLeave: %s released slot %d voluntarily."),
		*GetNameSafe(PC), SlotIndex);

	CommitSlotState(SlotIndex, *Slot);
	return true;
}

bool USeinLobbySubsystem::ServerHandleKickPlayer(APlayerController* HostPC, int32 SlotIndex)
{
	if (!IsServer() || !HostPC) return false;
	if (!IsHostController(HostPC))
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerHandleKickPlayer: rejected — non-host PC %s."),
			*GetNameSafe(HostPC));
		return false;
	}

	ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor) return false;

	FSeinLobbySlotState* Slot = Actor->FindSlotMutable(SlotIndex);
	if (!Slot)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerHandleKickPlayer: slot %d does not exist."), SlotIndex);
		return false;
	}

	// Already Open: nothing to do, treat as success.
	if (Slot->State == ESeinSlotState::Open && !Slot->bClaimed)
	{
		return true;
	}

	// For non-Human slots (AI, Closed), no PC to kick — just open the slot.
	// Reuse ServerHandleSetSlotState's commit path so the same broadcast/
	// validation runs.
	if (Slot->State != ESeinSlotState::Human)
	{
		return ServerHandleSetSlotState(HostPC, SlotIndex, ESeinSlotState::Open, FGameplayTag());
	}

	// Human-claimed: find the bound PC.
	APlayerController* TargetPC = nullptr;
	for (const auto& Pair : ControllerToSlot)
	{
		if (Pair.Value == SlotIndex)
		{
			TargetPC = Pair.Key.Get();
			break;
		}
	}

	// Refuse to kick the host. Closing the host's own slot via this verb
	// would tear down the listen server (host's PC = the server). If the
	// host wants out of the lobby, they should use Leave/Quit, not Kick.
	if (TargetPC == HostPC)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerHandleKickPlayer: refused — cannot kick the host (slot %d)."),
			SlotIndex);
		return false;
	}

	if (TargetPC)
	{
		UE_LOG(LogSeinNet, Log,
			TEXT("[Lobby] ServerHandleKickPlayer: kicking %s from slot %d."),
			*GetNameSafe(TargetPC), SlotIndex);

		// Send the kick notification via the kicked PC's relay rather than
		// `ClientReturnToMainMenuWithTextReason`. The UE built-in falls back
		// to the project's GameDefaultMap when no engine-level menu map is
		// configured, which strands kicked clients alone in the gameplay
		// map. The framework's Client_NotifyKicked path uses the configured
		// `USeinARTSCoreSettings::MainMenuMap` and mirrors the Leave button
		// flow exactly — guaranteed-consistent end state.
		if (UGameInstance* GI = GetGameInstance())
		{
			if (USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>())
			{
				for (const TWeakObjectPtr<ASeinNetRelay>& Wp : Net->GetRelays())
				{
					ASeinNetRelay* R = Wp.Get();
					if (R && R->GetOwner() == TargetPC)
					{
						R->Client_NotifyKicked(TEXT("Kicked by host"));
						break;
					}
				}
			}
		}
		ControllerToSlot.Remove(TWeakObjectPtr<APlayerController>(TargetPC));
	}
	else
	{
		// Slot is Human but no PC bound — corrupt state, but recover by
		// just clearing the slot. Log so we know.
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerHandleKickPlayer: slot %d is Human-claimed but no PC bound — clearing only."),
			SlotIndex);
	}

	// Reset the slot to Open. Mirrors the cleanup in ServerHandleSetSlotState's
	// Open case, plus we also clear FactionID (the SetSlotState path doesn't,
	// but a kick should fully reset).
	Slot->State = ESeinSlotState::Open;
	Slot->bClaimed = false;
	Slot->bDisconnected = false;
	Slot->bReady = false;
	Slot->FactionID = FSeinFactionID();
	Slot->ClaimedBy = FSeinPlayerID::Neutral();
	Slot->AIProfile = FGameplayTag();
	Slot->DisplayName = FText::FromString(TEXT("Open"));
	Slot->LastClaimantNetID = FUniqueNetIdRepl();
	Slot->RemoteAddress.Empty();

	CommitSlotState(SlotIndex, *Slot);
	return true;
}

bool USeinLobbySubsystem::ServerHandleSetSlotState(APlayerController* HostPC, int32 SlotIndex, ESeinSlotState NewState, FGameplayTag AIProfile)
{
	if (!IsServer()) return false;
	if (HostPC && !IsHostController(HostPC))
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerHandleSetSlotState: rejected — non-host PC %s."),
			*GetNameSafe(HostPC));
		return false;
	}

	ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor) return false;

	FSeinLobbySlotState* Slot = Actor->FindSlotMutable(SlotIndex);
	if (!Slot)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerHandleSetSlotState: slot %d does not exist."), SlotIndex);
		return false;
	}

	// Block transitions on currently-Human-claimed slots — host must not
	// rip a slot out from under a connected player. Use ClaimSlot to move
	// the player elsewhere, or wait for the player to leave first.
	if (Slot->State == ESeinSlotState::Human && Slot->bClaimed)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerHandleSetSlotState: slot %d is human-claimed (cannot rip out)."), SlotIndex);
		return false;
	}

	switch (NewState)
	{
		case ESeinSlotState::Open:
			Slot->State = ESeinSlotState::Open;
			Slot->bClaimed = false;
			Slot->bDisconnected = false;
			Slot->bReady = false;
			Slot->ClaimedBy = FSeinPlayerID::Neutral();
			Slot->AIProfile = FGameplayTag();
			Slot->DisplayName = FText::FromString(TEXT("Open"));
			Slot->LastClaimantNetID = FUniqueNetIdRepl();
			Slot->RemoteAddress.Empty();
			break;

		case ESeinSlotState::AI:
			Slot->State = ESeinSlotState::AI;
			Slot->bClaimed = true;
			Slot->bDisconnected = false;
			Slot->bReady = true; // AI slots are implicitly ready
			Slot->AIProfile = AIProfile;
			Slot->ClaimedBy = FSeinPlayerID(static_cast<uint8>(SlotIndex));
			Slot->DisplayName = AIProfile.IsValid()
				? FText::FromString(FString::Printf(TEXT("AI - %s"), *AIProfile.ToString()))
				: FText::FromString(TEXT("AI"));
			Slot->LastClaimantNetID = FUniqueNetIdRepl();
			Slot->RemoteAddress.Empty();
			break;

		case ESeinSlotState::Closed:
			Slot->State = ESeinSlotState::Closed;
			Slot->bClaimed = false;
			Slot->bDisconnected = false;
			Slot->bReady = false;
			Slot->AIProfile = FGameplayTag();
			Slot->ClaimedBy = FSeinPlayerID::Neutral();
			Slot->DisplayName = FText::FromString(TEXT("Closed"));
			Slot->LastClaimantNetID = FUniqueNetIdRepl();
			Slot->RemoteAddress.Empty();
			break;

		case ESeinSlotState::Human:
			// Cannot transition to Human via this verb — Human claims happen
			// through ServerHandleSlotClaim with a real PC owner.
			UE_LOG(LogSeinNet, Warning,
				TEXT("[Lobby] ServerHandleSetSlotState: cannot set state to Human via this verb (use ClaimSlot)."));
			return false;
	}

	UE_LOG(LogSeinNet, Log,
		TEXT("[Lobby] ServerHandleSetSlotState: slot %d → %d  AIProfile=%s"),
		SlotIndex, (int32)NewState, *AIProfile.ToString());

	CommitSlotState(SlotIndex, *Slot);
	return true;
}

bool USeinLobbySubsystem::ServerHandleSelectMap(APlayerController* HostPC, TSoftObjectPtr<UWorld> Map)
{
	if (!IsServer()) return false;
	if (HostPC && !IsHostController(HostPC))
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerHandleSelectMap: rejected — non-host PC %s."),
			*GetNameSafe(HostPC));
		return false;
	}

	ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor)
	{
		UE_LOG(LogSeinNet, Warning, TEXT("[Lobby] ServerHandleSelectMap: no lobby actor."));
		return false;
	}

	// Validate: map must exist in PluginSettings AvailableMaps. Designer
	// adds maps there (with declared SlotCount); arbitrary out-of-band
	// map paths are rejected so a misclick or bad client request can't
	// resize the lobby to garbage.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const FSeinLobbyMapEntry* Entry = nullptr;
	if (Settings)
	{
		const FSoftObjectPath TargetPath = Map.ToSoftObjectPath();
		for (const FSeinLobbyMapEntry& E : Settings->AvailableMaps)
		{
			if (E.Map.ToSoftObjectPath() == TargetPath)
			{
				Entry = &E;
				break;
			}
		}
	}
	if (!Entry)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerHandleSelectMap: map '%s' not in PluginSettings AvailableMaps — rejected."),
			*Map.ToSoftObjectPath().ToString());
		return false;
	}

	const int32 NewSlotCount = Entry->SlotCount;

	// Reject if shrink would lose claimed slots. Host opens those slots
	// first via SetSlotState, then re-tries the map switch. UI should
	// pre-emptively grey out smaller maps when this would fire.
	if (NewSlotCount < Actor->Slots.Num())
	{
		for (const FSeinLobbySlotState& S : Actor->Slots)
		{
			if (S.SlotIndex > NewSlotCount && S.bClaimed)
			{
				UE_LOG(LogSeinNet, Warning,
					TEXT("[Lobby] ServerHandleSelectMap: rejected — switching to '%s' (%d slots) would drop claimed slot %d. Open it first."),
					*Entry->DisplayName.ToString(), NewSlotCount, S.SlotIndex);
				return false;
			}
		}
	}

	// Commit selection.
	Actor->SelectedMap = Map;

	// Resize: append Open slots on grow, truncate on shrink (validated above).
	if (NewSlotCount > Actor->Slots.Num())
	{
		const int32 OldCount = Actor->Slots.Num();
		Actor->Slots.Reserve(NewSlotCount);
		for (int32 i = OldCount + 1; i <= NewSlotCount; ++i)
		{
			FSeinLobbySlotState S;
			S.SlotIndex = i;
			S.State = ESeinSlotState::Open;
			S.DisplayName = FText::FromString(TEXT("Open"));
			Actor->Slots.Add(S);
		}
	}
	else if (NewSlotCount < Actor->Slots.Num())
	{
		Actor->Slots.SetNum(NewSlotCount);
	}

	UE_LOG(LogSeinNet, Log,
		TEXT("[Lobby] ServerHandleSelectMap: '%s' applied (slot count → %d)."),
		*Entry->DisplayName.ToString(), NewSlotCount);

	Actor->ForceNetUpdate();
	Actor->OnLobbyStateChanged.Broadcast();
	return true;
}

bool USeinLobbySubsystem::BuildMatchSettingsSnapshot(FSeinMatchSettings& CapturedSettingsOut) const
{
	// Snapshot = plugin-settings `MatchSettingsStructure` (designer-authored
	// Slots manifest + Extensions array) overlaid with the lobby's
	// replicated slot state. Designer extension structs in `Extensions`
	// flow through unchanged — framework code does not interpret them.
	CapturedSettingsOut = ResolveBaseSettings();

	const ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor)
	{
		// No lobby actor — preserve preset defaults but the snapshot is empty
		// of slots. Caller treats as "fall back to WorldSettings."
		CapturedSettingsOut.Slots.Reset();
		return false;
	}

	int32 PopulatedSlots = 0;
	CapturedSettingsOut.Slots.Reset();
	CapturedSettingsOut.Slots.Reserve(Actor->Slots.Num());
	for (const FSeinLobbySlotState& Slot : Actor->Slots)
	{
		CapturedSettingsOut.Slots.Add(ProjectLobbySlotToMatchSlot(Slot));
		if ((Slot.State == ESeinSlotState::Human && !Slot.bDisconnected)
		    || Slot.State == ESeinSlotState::AI)
		{
			++PopulatedSlots;
		}
	}

	return PopulatedSlots > 0;
}

void USeinLobbySubsystem::PublishMatchSettingsSnapshot()
{
	if (!IsServer()) return;

	FSeinMatchSettings Snap;
	const bool bMeaningful = BuildMatchSettingsSnapshot(Snap);
	if (!bMeaningful)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] PublishMatchSettingsSnapshot: lobby has no Human/AI slots — publishing empty snapshot (game mode will fall back to WorldSettings)."));
	}

	PublishedSnapshot = MoveTemp(Snap);
	bSnapshotPublished = true;

	UE_LOG(LogSeinNet, Log,
		TEXT("[Lobby] PublishMatchSettingsSnapshot: %d slot(s) captured → published as GI override."),
		PublishedSnapshot.Slots.Num());
}

bool USeinLobbySubsystem::InstallPreparedMatchSettingsSnapshot(
	const FSeinMatchSettings& Snapshot)
{
	if (Snapshot.Slots.IsEmpty()) return false;
	PublishedSnapshot = Snapshot;
	bSnapshotPublished = true;
	return true;
}

void USeinLobbySubsystem::ExecutePreparedServerTravel(FString MapURL)
{
	PendingTravelTimerHandle.Invalidate();
	PendingTravelTimerWorld.Reset();
	bTravelScheduled = false;
	if (UWorld* World = GetWorld())
	{
		if (!World->ServerTravel(MapURL, /*bAbsolute=*/true))
		{
			if (UGameInstance* GI = GetGameInstance())
			{
				if (USeinNetSubsystem* Net =
					GI->GetSubsystem<USeinNetSubsystem>())
				{
					Net->AbortPreparedMatchTravel(
						TEXT("UWorld::ServerTravel rejected the prepared URL."));
				}
			}
		}
	}
}

bool USeinLobbySubsystem::ServerStartMatch(bool bTravelToGameplayMap)
{
	if (!IsServer())
	{
		UE_LOG(LogSeinNet, Warning, TEXT("[Lobby] ServerStartMatch: rejected — caller is not server."));
		return false;
	}

	const ASeinLobbyState* Actor = LobbyStateActor.Get();

	// Validate: must have at least one Human-claimed slot.
	bool bHasHuman = false;
	if (Actor)
	{
		for (const FSeinLobbySlotState& Slot : Actor->Slots)
		{
			if (Slot.bClaimed && Slot.State == ESeinSlotState::Human && !Slot.bDisconnected)
			{
				bHasHuman = true;
				break;
			}
		}
	}
	if (!bHasHuman)
	{
		UE_LOG(LogSeinNet, Warning,
			TEXT("[Lobby] ServerStartMatch: rejected — no Human-claimed slot in lobby."));
		return false;
	}

	// Readiness gating is designer-driven — host's BP can read each slot's
	// `bReady` from the replicated lobby state and choose whether to call
	// SeinRequestStartMatch. Framework permits Start whenever there's a
	// Human-claimed slot.
	UWorld* World = GetWorld();
	const bool bNetworked = World && World->GetNetMode() != NM_Standalone;
	if (bNetworked && !bTravelToGameplayMap)
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[Lobby] ServerStartMatch: networked lobby-derived starts require travel so every peer registers the final immutable roster from tick zero."));
		return false;
	}

	TSoftObjectPtr<UWorld> TravelMap;
	FString TravelMapURL;
	if (bTravelToGameplayMap)
	{
		TravelMap = ResolveGameplayMap();
		TravelMapURL = TravelMap.IsNull()
			? FString()
			: TravelMap.ToSoftObjectPath().GetLongPackageName();
		if (!World || TravelMapURL.IsEmpty())
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("[Lobby] ServerStartMatch: travel requested but the gameplay map is invalid (soft path '%s'); refusing start."),
				*TravelMap.ToString());
			return false;
		}
	}

	// Snapshot the lobby state into the GI override so whichever GameMode
	// runs next (current world OR the post-travel world) picks it up.
	PublishMatchSettingsSnapshot();
	USeinNetSubsystem* Net = nullptr;
	if (bNetworked)
	{
		UGameInstance* GI = GetGameInstance();
		Net = GI ? GI->GetSubsystem<USeinNetSubsystem>() : nullptr;
		if (!Net)
		{
			UE_LOG(LogSeinNet, Warning,
				TEXT("[Lobby] ServerStartMatch: USeinNetSubsystem missing — could not prepare match."));
			return false;
		}

		// A menu/lobby world legitimately has no resolved match manifest, so
		// ASeinGameMode cannot bind gameplay slots or spawn relays there. At the
		// launch boundary the lobby's final ControllerToSlot map is authoritative:
		// materialize those relays now, in canonical slot order, before preparing
		// the protocol context that must be delivered ahead of travel. The spawn
		// API is idempotent, so gameplay maps that already bound a relay are safe.
		TArray<TPair<int32, TWeakObjectPtr<APlayerController>>> LaunchBindings;
		LaunchBindings.Reserve(ControllerToSlot.Num());
		for (const TPair<TWeakObjectPtr<APlayerController>, int32>& Pair
			: ControllerToSlot)
		{
			LaunchBindings.Emplace(Pair.Value, Pair.Key);
		}
		LaunchBindings.Sort([](const auto& A, const auto& B)
		{
			return A.Key < B.Key;
		});
		for (const auto& Binding : LaunchBindings)
		{
			if (APlayerController* Controller = Binding.Value.Get())
			{
				Net->ServerSpawnRelayForController(
					Controller,
					FSeinPlayerID(static_cast<uint8>(Binding.Key)));
			}
		}
	}
	if (!bNetworked)
	{
		if (bTravelToGameplayMap)
		{
			const FString BootstrapTravelURL = TravelMapURL
				+ TEXT("?SeinBootstrap=StandaloneLaunch");
			UE_LOG(LogSeinNet, Log,
				TEXT("[Lobby] ServerStartMatch: standalone ServerTravel to '%s' (from soft path '%s')"),
				*BootstrapTravelURL, *TravelMap.ToString());
			if (!bTravelScheduled)
			{
				bTravelScheduled = true;
				FTimerDelegate TravelDelegate;
				TravelDelegate.BindUObject(
					this,
					&USeinLobbySubsystem::ExecutePreparedServerTravel,
					BootstrapTravelURL);
				PendingTravelTimerWorld = World;
				PendingTravelTimerHandle =
					World->GetTimerManager().SetTimerForNextTick(
						TravelDelegate);
			}
			return true;
		}

		USeinWorldSubsystem* WorldSubsystem =
			World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
		if (!WorldSubsystem
			|| !WorldSubsystem->StandaloneBootstrapLauncher.IsBound()
			|| !WorldSubsystem->StandaloneBootstrapLauncher.Execute())
		{
			UE_LOG(LogSeinNet, Error,
				TEXT("[Lobby] ServerStartMatch: Framework standalone bootstrap rejected the published lobby contract."));
			return false;
		}
		UE_LOG(LogSeinNet, Log,
			TEXT("[Lobby] ServerStartMatch: standalone bootstrap launched in-place."));
		return true;
	}

	const FName DestinationWorldPackage = bTravelToGameplayMap
		? FName(*TravelMapURL)
		: (World && World->GetOutermost()
			? World->GetOutermost()->GetFName()
			: NAME_None);
	if (!Net->PrepareMatchTravel(
		ESeinMatchTravelIntent::NewMatch,
		DestinationWorldPackage,
		bTravelToGameplayMap
			? ESeinPreparedWorldActivation::RequiresWorldTransition
			: ESeinPreparedWorldActivation::AllowCurrentWorld))
	{
		UE_LOG(LogSeinNet, Error,
			TEXT("[Lobby] ServerStartMatch: canonical participant/protocol preparation failed; refusing travel/start."));
		return false;
	}

	if (bTravelToGameplayMap)
	{
		// Use the long package name (no asset suffix) for the travel URL.
		// `TravelMap.ToString()` returns `/Game/Maps/X.X`; ServerTravel expects
		// the package path `/Game/Maps/X`. The URL intent reserves tick-zero
		// authority for the network coordinator on every traveling peer.
		const FString BootstrapTravelURL = TravelMapURL
			+ TEXT("?SeinBootstrap=ExternalOrchestrator");
		UE_LOG(LogSeinNet, Log,
			TEXT("[Lobby] ServerStartMatch: ServerTravel to '%s' (from soft path '%s')"),
			*BootstrapTravelURL, *TravelMap.ToString());
		if (!bTravelScheduled)
		{
			bTravelScheduled = true;
			FTimerDelegate TravelDelegate;
			TravelDelegate.BindUObject(
				this,
				&USeinLobbySubsystem::ExecutePreparedServerTravel,
				BootstrapTravelURL);
			PendingTravelTimerWorld = World;
			PendingTravelTimerHandle =
				World->GetTimerManager().SetTimerForNextTick(
					TravelDelegate);
		}
		return true;
	}

	return false;
}

void USeinLobbySubsystem::NotifyLobbyStateActorBeginPlay(ASeinLobbyState* Actor)
{
	if (!Actor) return;
	LobbyStateActor = Actor;
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Lobby] NotifyLobbyStateActorBeginPlay: latched %s (NetMode=%d)"),
		*GetNameSafe(Actor), (int32)Actor->GetNetMode());
}

void USeinLobbySubsystem::NotifyLobbyStateActorEndPlay(ASeinLobbyState* Actor)
{
	if (LobbyStateActor.Get() == Actor)
	{
		LobbyStateActor.Reset();
		UE_LOG(LogSeinNet, Verbose, TEXT("[Lobby] NotifyLobbyStateActorEndPlay: cleared LobbyStateActor."));
	}
}

void USeinLobbySubsystem::EnsureLobbyActor()
{
	if (!IsServer()) return;
	if (LobbyStateActor.IsValid()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	Params.bAllowDuringConstructionScript = true;

	ASeinLobbyState* Actor = World->SpawnActor<ASeinLobbyState>(ASeinLobbyState::StaticClass(), FTransform::Identity, Params);
	if (!Actor)
	{
		UE_LOG(LogSeinNet, Error, TEXT("[Lobby] EnsureLobbyActor: SpawnActor returned null."));
		return;
	}

	LobbyStateActor = Actor;

	// Seed lobby on first creation. Priority for slot count + initial map:
	//   1. GameMode-pushed override (`SlotCountOverride`). Set by
	//      `ASeinGameMode::InitGame` to `ResolvedMatchSettings.Slots.Num()`
	//      when the level has a slot manifest (PIE-direct from
	//      SeinPlayerStarts, or post-travel from a published lobby snapshot).
	//      This is the gameplay-map case — lobby reflects the actual level.
	//   2. First entry of `PluginSettings::AvailableMaps` — its SlotCount
	//      drives the seed, and SelectedMap defaults to its Map. This is
	//      the common case for the lobby/menu map: designer ships a list of
	//      playable maps and the lobby boots into the first one.
	//   3. Fallback: `MaxPlayers` Open slots, no SelectedMap (designer
	//      hasn't configured AvailableMaps yet).
	if (Actor->Slots.IsEmpty())
	{
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		int32 SlotCount = 0;
		if (SlotCountOverride > 0)
		{
			SlotCount = SlotCountOverride;
			UE_LOG(LogSeinNet, Log,
				TEXT("[Lobby] EnsureLobbyActor: using GameMode-pushed slot count override (%d slots)."),
				SlotCount);
		}
		else if (Settings && Settings->AvailableMaps.Num() > 0)
		{
			const FSeinLobbyMapEntry& First = Settings->AvailableMaps[0];
			Actor->SelectedMap = First.Map;
			SlotCount = First.SlotCount;
			UE_LOG(LogSeinNet, Log,
				TEXT("[Lobby] EnsureLobbyActor: defaulting to first AvailableMaps entry '%s' (%d slots)."),
				*First.DisplayName.ToString(), SlotCount);
		}
		if (SlotCount <= 0)
		{
			SlotCount = (Settings && Settings->MaxPlayers > 0) ? Settings->MaxPlayers : 4;
		}
		Actor->Slots.Reserve(SlotCount);
		for (int32 i = 1; i <= SlotCount; ++i)
		{
			FSeinLobbySlotState S;
			S.SlotIndex = i;
			S.State = ESeinSlotState::Open;
			S.DisplayName = FText::FromString(TEXT("Open"));
			Actor->Slots.Add(S);
		}
		Actor->ForceNetUpdate();
		UE_LOG(LogSeinNet, Log, TEXT("[Lobby] EnsureLobbyActor: spawned actor + seeded %d Open slot(s)."), SlotCount);
	}
}

bool USeinLobbySubsystem::CanAcceptConnection(const FUniqueNetIdRepl& UniqueId) const
{
	// First connection — no lobby actor yet. Allow; OnPostLogin will lazy-spawn
	// the lobby and seat this PC.
	const ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor) return true;

	// Walk slots once: look for either a free Open slot OR a disconnected
	// slot whose LastClaimantNetID matches the connecting PC's UniqueId
	// (reconnect path — they get their old slot back).
	for (const FSeinLobbySlotState& Slot : Actor->Slots)
	{
		// Free Open slot — accept any connecting PC.
		if (Slot.State == ESeinSlotState::Open && !Slot.bClaimed && !Slot.bDisconnected)
		{
			return true;
		}
		// Reconnect match — UniqueId-bound slot in disconnect-grace.
		if (Slot.bDisconnected && Slot.LastClaimantNetID.IsValid() && UniqueId.IsValid()
			&& Slot.LastClaimantNetID == UniqueId)
		{
			return true;
		}
	}

	// Every slot is Human/AI/Closed-claimed and no reconnect candidate. Reject.
	return false;
}

bool USeinLobbySubsystem::CanAcceptConnectionAtSlot(
	FSeinPlayerID Slot,
	const APlayerController* Controller) const
{
	if (!Slot.IsValid())
	{
		return false;
	}
	const ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor)
	{
		return true;
	}
	const int32 TargetSlot = Slot.Value;
	const FSeinLobbySlotState* State = Actor->FindSlot(TargetSlot);
	if (!State)
	{
		return false;
	}

	bool bSameController = false;
	for (const TPair<TWeakObjectPtr<APlayerController>, int32>& Pair :
		ControllerToSlot)
	{
		const APlayerController* Existing = Pair.Key.Get();
		if (!Existing)
		{
			continue;
		}
		if (Existing == Controller)
		{
			if (Pair.Value != TargetSlot)
			{
				return false;
			}
			bSameController = true;
		}
		else if (Pair.Value == TargetSlot)
		{
			return false;
		}
	}
	if (bSameController)
	{
		return true;
	}
	return (State->State == ESeinSlotState::Open
			&& !State->bClaimed && !State->bDisconnected)
		|| (State->State == ESeinSlotState::Human
			&& State->bDisconnected);
}

int32 USeinLobbySubsystem::PickNextFreeSlot() const
{
	const ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor) return 0;
	for (const FSeinLobbySlotState& Slot : Actor->Slots)
	{
		if (Slot.State != ESeinSlotState::Open) continue;
		if (Slot.bClaimed) continue;          // human or AI bound
		if (Slot.bDisconnected) continue;      // reserved for reconnecting player
		return Slot.SlotIndex;
	}
	return 0;
}

int32 USeinLobbySubsystem::FindDisconnectedSlotForPC(APlayerController* PC) const
{
	if (!PC || !PC->PlayerState) return 0;
	const ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor) return 0;

	const FUniqueNetIdRepl PCNetID = PC->PlayerState->GetUniqueId();
	if (!PCNetID.IsValid()) return 0;

	for (const FSeinLobbySlotState& Slot : Actor->Slots)
	{
		if (!Slot.bDisconnected) continue;
		if (!Slot.LastClaimantNetID.IsValid()) continue;
		if (Slot.LastClaimantNetID == PCNetID)
		{
			return Slot.SlotIndex;
		}
	}
	return 0;
}

void USeinLobbySubsystem::CommitSlotState(int32 SlotIndex, const FSeinLobbySlotState& NewState)
{
	ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor) return;

	// Mutating the array in-place was already done by callers (FindSlotMutable
	// returns a live pointer); this method is the post-commit broadcast +
	// replication-poke. Kept as a separate hook so future Phase 3c additions
	// (per-slot RepNotify, faction validation) have a single chokepoint.
	Actor->ForceNetUpdate();
	Actor->OnLobbyStateChanged.Broadcast();

	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Lobby] CommitSlotState: slot %d  State=%d  bClaimed=%d  bDisc=%d  bReady=%d  Faction=%u  ClaimedBy=%u"),
		SlotIndex, (int32)NewState.State, (int32)NewState.bClaimed, (int32)NewState.bDisconnected,
		(int32)NewState.bReady, NewState.FactionID.Value, NewState.ClaimedBy.Value);
}

void USeinLobbySubsystem::ScheduleSlotReclaim(int32 SlotIndex)
{
	// Cancel any prior pending timer for this slot first (defensive: shouldn't
	// happen, but harmless if it does).
	CancelSlotReclaim(SlotIndex);

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const float Grace = Settings ? Settings->LobbyReconnectGraceSeconds : 60.0f;
	if (Grace <= 0.0f) return;

	TWeakObjectPtr<USeinLobbySubsystem> WeakSelf(this);
	const FTSTicker::FDelegateHandle Handle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakSelf, SlotIndex](float)
		{
			if (USeinLobbySubsystem* Self = WeakSelf.Get())
			{
				Self->ExpireDisconnectedSlot(SlotIndex);
			}
			return false; // one-shot
		}), Grace);

	PendingReclaimTimers.Add(SlotIndex, Handle);
}

void USeinLobbySubsystem::CancelSlotReclaim(int32 SlotIndex)
{
	if (FTSTicker::FDelegateHandle* Handle = PendingReclaimTimers.Find(SlotIndex))
	{
		FTSTicker::GetCoreTicker().RemoveTicker(*Handle);
		PendingReclaimTimers.Remove(SlotIndex);
	}
}

void USeinLobbySubsystem::ExpireDisconnectedSlot(int32 SlotIndex)
{
	PendingReclaimTimers.Remove(SlotIndex);

	ASeinLobbyState* Actor = LobbyStateActor.Get();
	if (!Actor) return;

	FSeinLobbySlotState* Slot = Actor->FindSlotMutable(SlotIndex);
	if (!Slot || !Slot->bDisconnected) return; // already reconnected / handled

	Slot->State = ESeinSlotState::Open;
	Slot->bClaimed = false;
	Slot->bDisconnected = false;
	Slot->bReady = false;
	Slot->ClaimedBy = FSeinPlayerID::Neutral();
	Slot->DisplayName = FText::FromString(TEXT("Open"));
	Slot->LastClaimantNetID = FUniqueNetIdRepl();
	Slot->RemoteAddress.Empty();

	UE_LOG(LogSeinNet, Log,
		TEXT("[Lobby] ExpireDisconnectedSlot: slot %d grace expired — slot opened."),
		SlotIndex);

	CommitSlotState(SlotIndex, *Slot);
}

#include "Classes/SAFGameMode.h"
#include "Classes/SAFGameState.h"
#include "Classes/SAFPlayerController.h"
#include "Classes/SAFPlayerState.h"
#include "Classes/SAFHUD.h"
#include "Classes/SAFCameraPawn.h"
#include "Interfaces/SAFPlayerInterface.h"
#include "Interfaces/SAFActorInterface.h"
#include "Utils/SAFLibrary.h"
#include "EngineUtils.h"
#include "Debug/SAFDebugTool.h"

#if WITH_EDITOR
#include "Editor.h"
#include "UnrealEdMisc.h"
#endif

ASAFGameMode::ASAFGameMode() {
	GameStateClass = ASAFGameState::StaticClass();
	PlayerControllerClass = ASAFPlayerController::StaticClass();
	PlayerStateClass = ASAFPlayerState::StaticClass();
	HUDClass = ASAFHUD::StaticClass();
	DefaultPawnClass = ASAFCameraPawn::StaticClass();
}

void ASAFGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) {
	Super::InitGame(MapName, Options, ErrorMessage);

#if WITH_EDITOR
	// In PIE mode, read the "Number of Players" setting from editor preferences
	if (GetWorld() && GetWorld()->IsPlayInEditor()) {
		ULevelEditorPlaySettings* PlaySettings = GetMutableDefault<ULevelEditorPlaySettings>();
		if (PlaySettings) {
			int32 PIENumPlayers = 1;
			PlaySettings->GetPlayNumberOfClients(PIENumPlayers);
			
			EPlayNetMode NetMode;
			PlaySettings->GetPlayNetMode(NetMode);			
			if (PIENumPlayers > 1) {
				NumPlayers = PIENumPlayers;
				SAFDEBUG_SUCCESS(FORMATSTR("PIE Mode: Overriding NumPlayers to match editor setting: %d", NumPlayers));
			}
		}
	}
#endif
}

// Init override handles building the array of teams
void ASAFGameMode::InitGameState() {
	Super::InitGameState();

	// Compute max players per team
	int32 Remainder = NumPlayers % NumTeams;
	NumPlayersPerTeam = NumPlayers / NumTeams;
	if (Remainder != 0) SAFDEBUG_WARNING("Uneven players per team, excess players will not be able to play!");

	ASAFGameState* SAFGameState = GetGameState<ASAFGameState>();
	if (!IsValid(SAFGameState)) return;

	SAFGameState->bResourceSharingEnabled = bResourceSharingEnabled;
	SAFGameState->Teams.Reset();
	for (int32 i = 0; i < NumTeams; ++i) {
		FSAFTeam Team; 
		Team.Players.SetNumZeroed(NumPlayersPerTeam);
		SAFGameState->Teams.Add(Team);
	}

	SAFGameState->ForceNetUpdate();
}

// Override handles team assignments on player login.
void ASAFGameMode::PostLogin(APlayerController* NewPlayer) {
	Super::PostLogin(NewPlayer);
	ASAFGameState* SAFGameState = GetGameState<ASAFGameState>();
	ASAFPlayerState* SAFPlayerState = Cast<ASAFPlayerState>(NewPlayer->PlayerState);
	ASAFPlayerController* SAFPlayerController = Cast<ASAFPlayerController>(NewPlayer);
	if (!IsValid(SAFGameState) || !IsValid(SAFPlayerState) || !IsValid(SAFPlayerController)) return;

	SAFGameState->bResourceSharingEnabled = bResourceSharingEnabled;
	int32 DesiredTeam = SAFPlayerController->DesiredTeamID;
	int32 DesiredSlot = SAFPlayerController->DesiredPlayerID;

	bool bSlotted = false;
	if (DesiredTeam > 0) bSlotted = AddPlayerToTeamAtSlot(SAFGameState, SAFPlayerState, DesiredTeam, DesiredSlot);
	else bSlotted = AddPlayer(SAFGameState, SAFPlayerState);
	if (!bSlotted) SAFDEBUG_ERROR("PostLogin error: failed to slot player!");

	SAFPlayerController->OnServerReady.AddDynamic(this, &ASAFGameMode::HandlePlayerReady);
}

// Player Login, Init / Choose Team Helpers
// =================================================================================================================================
// Chooses the next logical team of a player to join by iterating over the teams,
// finding the team with the fewest players, and assigning that team.
int32 ASAFGameMode::ChooseTeam(ASAFGameState* SAFGameState) const {
	int32 TeamID = 0;
	int32 TeamCount  = INT_MAX;

	for (int32 i = 1; i <= SAFGameState->Teams.Num(); ++i) {
		const FSAFTeam* Team = SAFGameState->FindTeam(i);
		if (!Team) continue;

		int32 Occupied = 0;
		for (APlayerState* PlayerState : Team->Players) if (IsValid(PlayerState)) ++Occupied;

		if (Occupied < TeamCount && Occupied < NumPlayersPerTeam) {
			TeamCount  = Occupied;
			TeamID = i;
		}
	}

	return TeamID;
}

// Add a player to the next team selected by ChooseTeam().
bool ASAFGameMode::AddPlayer(ASAFGameState* SAFGameState, ASAFPlayerState* SAFPlayerState) {
	const int32 TeamID = ChooseTeam(SAFGameState);
	if (TeamID == 0) { SAFDEBUG_WARNING("AddPlayerToTeam failed: all teams are full."); return false; }
	return AddPlayerToTeam(SAFGameState, SAFPlayerState, TeamID);
}

// Adds a player to the associated team with the passed in ID, if able.
bool ASAFGameMode::AddPlayerToTeam(ASAFGameState* SAFGameState, ASAFPlayerState* SAFPlayerState, int32 DesiredTeamID) {
	if (!IsValid(SAFGameState) || !IsValid(SAFPlayerState)) return false;
	if (FSAFTeam* Team = SAFGameState->FindTeam(DesiredTeamID)) {
		if (!Team) return false;
		for (int32 i = 0; i < Team->Players.Num(); i++) {
			if (!IsValid(Team->Players[i])) {
				Team->Players[i] = SAFPlayerState;
				SAFPlayerState->TeamID = DesiredTeamID;
				SAFGameState->ForceNetUpdate();
				return true;
			}
		}
	}

	return false;
}

// Add a player to a specific team at a specific player slot on that team (useful for fixed spawn modes).
// Falls back to simply attempting to add to the desired team (at any slot) and if that fails rejects with warning.
bool ASAFGameMode::AddPlayerToTeamAtSlot(ASAFGameState* SAFGameState, ASAFPlayerState* SAFPlayerState, int32 DesiredTeamID, int32 DesiredPlayerID) {
	FSAFTeam* DesiredTeam = SAFGameState->FindTeam(DesiredTeamID);
	if (DesiredTeam) {
		bool bValidSlot = DesiredTeam->Players.IsValidIndex(DesiredPlayerID);
		if (!bValidSlot) SAFDEBUG_WARNING(FORMATSTR("AddPlayerToTeamAtSlot called with invalid PlayerID '%d' on TeamID '%d'. (if you left PlayerID 0, this will cause this player to slot at a random slot on the DesiredTeam)", DesiredPlayerID, DesiredTeamID));
		APlayerState* OccupyingPlayer = SAFGameState->FindPlayer(DesiredTeamID, DesiredPlayerID);

		// If all is good
		if (!IsValid(OccupyingPlayer) && bValidSlot) {
			DesiredTeam->Players[DesiredPlayerID] = SAFPlayerState;
			return true;
		} 

		else if (AddPlayerToTeam(SAFGameState, SAFPlayerState, DesiredTeamID)) SAFDEBUG_INFO("AddPlayerToTeamAtSlot: player was slotted on desired team, but at a different available slot.");
		else SAFDEBUG_WARNING("AddPlayerToTeamAtSlot failed: all player slots on desired team were occupied. Team will be automatically reassigned.");
	} else SAFDEBUG_WARNING("AddPlayerToTeamAtSlot failed: requested team didn't exist. Team will be automatically reassigned.");

	AddPlayer(SAFGameState, SAFPlayerState);
	return false;
}

// Handles the event when a player broadcasts they are ready on the server.
void ASAFGameMode::HandlePlayerReady(ASAFPlayerController* PlayerController) {
	ASAFPlayerController* SAFPlayerController = Cast<ASAFPlayerController>(PlayerController);
	if (!IsValid(SAFPlayerController)) { SAFDEBUG_ERROR("HandlePlayerReady aborted: invalid controller."); return; }
	TryStartSAFMatch();
}

// Sets the resource sharing mode for this game mode instance.
void ASAFGameMode::SetResourceSharingEnabled(bool bEnabled) {
	if (!HasAuthority()) return;
	bResourceSharingEnabled = bEnabled;

	if (ASAFGameState* SAFGameState = GetGameState<ASAFGameState>()) {
		if (SAFGameState->bResourceSharingEnabled != bEnabled) {
			SAFGameState->bResourceSharingEnabled = bEnabled;
			SAFGameState->ForceNetUpdate();
		}
	}
}

// Try/Start Match
// =================================================================================================================
// Tries to start the match after all players reported ready. 
bool ASAFGameMode::TryStartSAFMatch() {
	if (bSAFMatchStarted) { SAFDEBUG_WARNING("TryStartSAFMatch: match already started. Discarding."); return false; }
	if (!HasAuthority()) { SAFDEBUG_WARNING("TryStartSAFMatch: called on non-authority."); return false; } // Line 140

	ASAFGameState* SAFGameState = GetGameState<ASAFGameState>();
	if (!IsValid(SAFGameState)) { SAFDEBUG_ERROR("StartSAFMatch aborted: GameState is incorrect type."); return false; }

	int32 ReadyCount = 0;
	int32 ConnectedCount = 0;
	for (APlayerState* PlayerState : SAFGameState->PlayerArray) {
		// Check connected
		if (!IsValid(PlayerState)) continue;
		++ConnectedCount;

		// Check ready
		const ASAFPlayerState* SAFPlayerState = Cast<ASAFPlayerState>(PlayerState);
		if (IsValid(SAFPlayerState) && SAFPlayerState->bIsReady) ++ReadyCount;
	}

	// Determine if we should start: this will depend on standalone vs networked mode,
	// if in standalone we start as soon as one player is ready, in networked we wait for 
	// all expected players.
	bool bShouldStart = false;
	
	// In standalone mode, start as soon as the player is ready
	if (GetNetMode() == NM_Standalone) if (ReadyCount > 0) bShouldStart = true;

	// Networked mode - wait for all expected players
	else {
		SAFDEBUG_INFO(FORMATSTR("Player readiness: %d/%d (connected: %d)", ReadyCount, NumPlayers, ConnectedCount));
		if (ReadyCount == NumPlayers) bShouldStart = true;
		else if (ReadyCount < NumPlayers) return false;
		else return false;
	}
	
	// Start the match if conditions are met
	if (bShouldStart) {
		SAFDEBUG_SUCCESS("All expected players are ready. Starting SAF match...");
		bSAFMatchStarted = StartSAFMatch();
		if (bSAFMatchStarted) { SAFDEBUG_SUCCESS("SeinARTS match started successfully."); return true; }
		else { SAFDEBUG_ERROR("SeinARTS match failed to start."); return false; }
	}
	
	return false;
}

// Starts the SeinARTS Framework match (runs SAF Inits).
// Note: this function is independent of the regular GameMode StartMatch(). The way SeinARTS handles
// multiplayer matches is by letting Unreal's framework load up, and awaiting all expected players
// to PostLogin and BeginPlay. Then, for each PC, their BeginPlay broadcasts readiness on the client,
// which cascades to a broadcast on the server. The server handles the event, and once all expected 
// players have broadcasted, this function is called to initialize all the relevant SAF Actors.
// Returns true if match was started successfully.
bool ASAFGameMode::StartSAFMatch() {
	if (bSAFMatchStarted) { SAFDEBUG_WARNING("StartSAFMatch: match already started. Discarding."); return false; }
	if (!HasAuthority()) { SAFDEBUG_WARNING("StartSAFMatch: called on non-authority."); return false; }
	ASAFGameState* SAFGameState = GetGameState<ASAFGameState>();
	if (!IsValid(SAFGameState)) { SAFDEBUG_ERROR("StartSAFMatch aborted: GameState is incorrect type."); return false; }

	// Init players
	for (FConstPlayerControllerIterator PlayerItr = GetWorld()->GetPlayerControllerIterator(); PlayerItr; ++PlayerItr) {
		ASAFPlayerController* SAFPlayerController = Cast<ASAFPlayerController>(PlayerItr->Get());
		if (IsValid(SAFPlayerController)) ISAFPlayerInterface::Execute_InitPlayer(SAFPlayerController);
	}

	// Init extant units
	for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr) { 
		AActor* Actor = *ActorItr;
		if (SAFLibrary::IsActorPtrValidSeinARTSActor(Actor)) ISAFActorInterface::Execute_InitFromAsset(Actor, nullptr, nullptr, true /** Force reinit */);
	}

	// Return success flag
	return true;
}

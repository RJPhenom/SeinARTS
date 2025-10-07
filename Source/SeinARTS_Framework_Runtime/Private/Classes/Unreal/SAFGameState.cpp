#include "Classes/Unreal/SAFGameState.h"
#include "Classes/Unreal/SAFPlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Debug/SAFDebugTool.h"

ASAFGameState::ASAFGameState() {

}

// Logging
// =================================================================================================================
void ASAFGameState::LogTeamsAndPlayers() {
	for (int32 i = 0; i < Teams.Num(); i++) {
		SAFDEBUG_INFO(FORMATSTR("\tTeam %d:", i + 1));
		for (int32 j = 0; j < Teams[i].Players.Num(); j++) {
			if (Teams[i].Players[j]) SAFDEBUG_INFO(FORMATSTR("\t\tPlayer %d is registered.", j + 1));
			else SAFDEBUG_INFO(FORMATSTR("\t\tPlayer %d has not logged in yet.", j + 1));
		}
	}
}

// Find Team / Player
// =================================================================================================================
FSAFTeam* ASAFGameState::FindTeam(int32 TeamID) {
	return Teams.IsValidIndex(TeamID - 1) ? &Teams[TeamID - 1] : nullptr;
}
const FSAFTeam* ASAFGameState::FindTeam(int32 TeamID) const {
	return Teams.IsValidIndex(TeamID - 1) ? &Teams[TeamID - 1] : nullptr;
}

ASAFPlayerState* ASAFGameState::FindPlayer(int32 TeamID, int32 PlayerID) {
	SAFDEBUG_INFO(FORMATSTR("FindPlayer called for Team %d, Player %d. Current SAFGameState:", TeamID, PlayerID));
	LogTeamsAndPlayers();
	FSAFTeam* Team = FindTeam(TeamID);
	if (!Team) return nullptr;
	return Team->Players.IsValidIndex(PlayerID - 1) ? Team->Players[PlayerID - 1] : nullptr;
}

const ASAFPlayerState* ASAFGameState::FindPlayer(int32 TeamID, int32 PlayerID) const {
	const FSAFTeam* Team = FindTeam(TeamID);
	if (!Team) return nullptr;
	return Team->Players.IsValidIndex(PlayerID -1) ? Team->Players[PlayerID - 1] : nullptr;
}

// Replication
// ==================================================================================================
void ASAFGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASAFGameState, Teams);
	DOREPLIFETIME(ASAFGameState, MatchReady);
	DOREPLIFETIME(ASAFGameState, bResourceSharingEnabled);
}
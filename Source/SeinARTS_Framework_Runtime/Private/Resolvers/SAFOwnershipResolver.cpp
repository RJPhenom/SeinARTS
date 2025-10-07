#include "Resolvers/SAFOwnershipResolver.h"
#include "Classes/Unreal/SAFGameState.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Debug/SAFDebugTool.h"

using namespace SAFOwnershipResolver;

/**
 * SAFOwnershipResolver
 *
 * Utility for resolving an owning APlayerState from team/player IDs via the SAFGameState.
 * This does not perform HasAuthority checks; call it only on the authority path if needed.
 *
 * Return contract:
 *  - Returns true and writes OutPlayer when a valid player is found.
 *  - Returns false otherwise. Some failure paths log warnings/errors as noted below.
 */
bool SAFOwnershipResolver::ResolveOwner(
	const UWorld* World, 
	int32 TeamID, int32 PlayerID, 
	ASAFPlayerState*& OutPlayer
) {
	OutPlayer = nullptr;

	// Neutral/unowned requested: no owner, no log.
	if (TeamID == 0 && PlayerID == 0) return false;

	// Partial specification is considered a designer error: warn and stop.
	if (TeamID == 0 || PlayerID == 0) {
		SAFDEBUG_WARNING(
			"ResolveOwner called with only one of TeamID/PlayerID set. "
			"(You must change BOTH PlayerID and TeamID at design time or the unit will be initialized as neutral)");
		return false;
	}

	// Validation
	if (!World) { SAFDEBUG_ERROR("ResolveOwner failed: World was null."); return false; }

	const AGameStateBase* const GameState = World->GetGameState();
	if (!GameState) { SAFDEBUG_ERROR("ResolveOwner failed: could not retrieve GameState."); return false; }

	const ASAFGameState* const SAFGameState = Cast<ASAFGameState>(GameState);
	if (!SAFGameState) { SAFDEBUG_ERROR("ResolveOwner failed: SAFGameState was invalid."); return false; }

	const ASAFPlayerState* const Found = SAFGameState->FindPlayer(TeamID, PlayerID);
	if (Found == nullptr) { SAFDEBUG_ERROR("ResolveOwner failed: could not find owner."); return false; }

	// Out
	OutPlayer = const_cast<ASAFPlayerState*>(Found);
	return true;
}

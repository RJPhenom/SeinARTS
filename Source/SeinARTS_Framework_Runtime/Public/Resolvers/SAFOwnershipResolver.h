#pragma once

#include "CoreMinimal.h"

class UWorld;
class ASAFPlayerState;

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
namespace SAFOwnershipResolver {
	SEINARTS_FRAMEWORK_RUNTIME_API bool ResolveOwner(
		const UWorld* World, 
		int32 TeamID, int32 PlayerID, 
		ASAFPlayerState*& OutPlayer
	);
}

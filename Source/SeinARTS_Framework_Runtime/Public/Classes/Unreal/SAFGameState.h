#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "SAFGameState.generated.h"

class ASAFPlayerState;

USTRUCT(BlueprintType)
struct FSAFTeam {
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadOnly)
  TArray<TObjectPtr<ASAFPlayerState>> Players;
};

/**
 * SAFGameState
 * 
 * Base GameState class in the SeinARTS Framework. Primarily used for tracking
 * teams of players, which is used by units to resolve ownership and is used by
 * systems and classes when they care about who owns what or what team it is on.
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Game State"))
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFGameState : public AGameStateBase {

	GENERATED_BODY()

public:

	ASAFGameState();

  // ==========================================================================
	//                              Game State
	// ==========================================================================

  // Replicated teams array.
  UPROPERTY(VisibleAnywhere, Replicated)
  TArray<FSAFTeam> Teams;

  // Replicated flag showing match is set ready.
  UPROPERTY(VisibleAnywhere, Replicated)
  bool MatchReady = false;

  // Shared-resource mode (mirrors server-authoritative flag from GameMode).
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated)
  bool bResourceSharingEnabled = false;

  // Checks if the bResourceSharingEnabled flag is set to on.
  UFUNCTION(BlueprintPure, Category="SeinARTS|Economy")
  bool IsResourceSharingEnabled() const { return bResourceSharingEnabled; }

  // Debug utility, for logging GameState 'Teams' which tracks logged in/initalized players on each team.
  void LogTeamsAndPlayers();

  // Finds and returns a reference to the Team struct with the associated ID from the SAFGameState, if any.
  FSAFTeam* FindTeam(int32 TeamID);
  const FSAFTeam* FindTeam(int32 TeamID) const;

  // Finds the player on team TeamID (if any) at slot PlayerId (if any) and returns their APlayerState.
  ASAFPlayerState* FindPlayer(int32 TeamID, int32 PlayerID);
  const ASAFPlayerState* FindPlayer(int32 TeamID, int32 PlayerID) const;

  // ==========================================================================
	//                              Replication
	// ==========================================================================

  virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

};
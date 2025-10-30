#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SAFGameMode.generated.h"

class ASAFGameState;
class ASAFPlayerState;
class AActor;
class AController;

/**
 * SAFGameMdoe
 * 
 * Base GameMode class in the SeinARTS Framework. Since no particular game mode 
 * is the default game mode, this is a data-only class for setting the default
 * classes for Unreal's Game Framework to SeinARTS classes.
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Game Mode"))
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFGameMode : public AGameModeBase {

	GENERATED_BODY()

public:

	ASAFGameMode();
	virtual void InitGameState() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot) override;

	/** Number of teams in the match. */
	UPROPERTY(EditDefaultsOnly, Category="SeinARTS", meta=(ClampMin="2", UIMin="2", ClampMax="8", UIMax="8"))
	int32 NumTeams = 4;

	/** Number of players in the match. */
	UPROPERTY(EditDefaultsOnly, Category="SeinARTS", meta=(ClampMin="2", UIMin="2", ClampMax="8", UIMax="8"))
	int32 NumPlayers = 4;

	/** Number of players per team in the match. */
	UPROPERTY(EditDefaultsOnly, Category="SeinARTS", meta=(ClampMin="1", UIMin="1", ClampMax="4", UIMax="4"))
	int32 NumPlayersPerTeam = 1;

	/** If true, all players on the same team share resources 
	 * (default: false). */
	UPROPERTY(EditDefaultsOnly, Category="SeinARTS")
	bool bResourceSharingEnabled = false;

private:

	int32 NextTeam = 0;

	/** Chooses the next logical team of a player to join by iterating over the teams,
	 * finding the team with the fewest players, and assigning that team. */
	int32 ChooseTeam(ASAFGameState* SAFGameState) const;

	/** Add a player to the next team selected by ChooseTeam(). */
	bool AddPlayer(ASAFGameState* SAFGameState, ASAFPlayerState* SAFPlayerState);

	/** Adds a player to the associated team with the passed in ID, if able. */
	bool AddPlayerToTeam(ASAFGameState* SAFGameState, ASAFPlayerState* SAFPlayerState, int32 TeamID);

	/** Add a player to a specific team at a specific player slot on that team (useful for fixed spawn modes). */
	bool AddPlayerToTeamAtSlot(ASAFGameState* SAFGameState, ASAFPlayerState* SAFPlayerState, int32 DesiredTeamID, int32 DesiredPlayerID);

	/** Handles the event when a player broadcasts they are ready on the server. */
	UFUNCTION()
	void HandlePlayerReady(ASAFPlayerController* PlayerController);

	/** Sets the resource sharing mode for this game mode instance. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Resource Sharing")
	void SetResourceSharingEnabled(bool bEnabled);	

	/** Tries to start the match after all players reported ready. */
	bool TryStartSAFMatch();

	// Starts the SeinARTS Framework match (runs SAF Inits).
	// Note: this function is independent of the regular GameMode StartMatch(). The way SeinARTS handles
	// multiplayer matches is by letting Unreal's framework load up, and awaiting all expected players
	// to PostLogin and BeginPlay. Then, for each PC, their BeginPlay broadcasts readiness on the client,
	// which cascades to a broadcast on the server. The server handles the event, and once all expected 
	// players have broadcasted, this function is called to initialize all the relevant SAF Actors.
	// Returns true if match was started successfully.
	bool StartSAFMatch();
	bool bSAFMatchStarted = false;
};

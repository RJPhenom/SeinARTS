/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinGameMode.h
 * @brief   Authority-side Unreal match shell and manifest controller routing.
 */

#pragma once

#include "CoreMinimal.h"
#include "Data/SeinMatchSettings.h"
#include "GameFramework/GameModeBase.h"
#include "SeinGameMode.generated.h"

class ASeinPlayerController;
class ASeinPlayerStart;

/**
 * Default RTS GameMode. It owns Unreal's controller, pawn, HUD, admission,
 * and authored-start routing responsibilities. Deterministic player/entity
 * materialization belongs to USeinMatchBootstrapSubsystem on every peer.
 */
UCLASS(Blueprintable)
class SEINARTSFRAMEWORK_API ASeinGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASeinGameMode();

	virtual void InitGame(
		const FString& MapName,
		const FString& Options,
		FString& ErrorMessage) override;
	virtual void PreLogin(
		const FString& Options,
		const FString& Address,
		const FUniqueNetIdRepl& UniqueId,
		FString& ErrorMessage) override;
	virtual void HandleStartingNewPlayer_Implementation(
		APlayerController* NewPlayer) override;

	/** Frozen slot count when this world has a manifest, otherwise the project ceiling. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|GameMode")
	int32 GetEffectiveMaxPlayers() const;

	/** Find the unique authored start for an exact positive manifest slot. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|GameMode")
	ASeinPlayerStart* FindPlayerStartForSlot(int32 SlotIndex) const;

	/** Canonical immutable controller-routing manifest staged during InitGame. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|GameMode")
	FSeinMatchSettings ResolvedMatchSettings;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|GameMode")
	bool bMatchSettingsResolved = false;

protected:
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/** Lobby snapshot first; otherwise a direct-PIE manifest from PlayerStarts. */
	const FSeinMatchSettings* ResolveMatchSettingsForWorld() const;

	mutable FSeinMatchSettings SynthesizedMatchSettings;

private:
	const FSeinMatchSlot* FindManifestSlot(int32 SlotIndex) const;
	bool IsSlotClaimedByAnother(
		int32 SlotIndex,
		const ASeinPlayerController* Controller) const;

	/** Authority-only routing claims. They never create or mutate sim state. */
	TMap<int32, TWeakObjectPtr<ASeinPlayerController>> ClaimedSlots;
};

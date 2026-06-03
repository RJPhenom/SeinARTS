/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinScenarioBPFL.h
 * @brief   Thin utility layer for scenarios (DESIGN §17). Scenarios
 *          themselves are abstract sim entities (§1 bIsAbstract) with
 *          ability components; this BPFL provides sim-pause + cinematic
 *          helpers designers drop into scenario ability graphs.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "SeinScenarioBPFL.generated.h"

class USeinWorldSubsystem;

/**
 * How a pre-rendered cinematic can be skipped by players (DESIGN §17).
 * Used by `USeinScenarioBPFL::SeinPlayPreRenderedCinematic`.
 */
UENUM(BlueprintType)
enum class ESeinCinematicSkipMode : uint8
{
	/** Each player's local video stops on their skip. Non-blocking-sim only. */
	Individual,
	/** Uses §18 voting primitive; threshold reached → EndCinematic fires globally. */
	VoteToSkip,
	/** Only the host can skip. */
	HostOnly,
	/** Skip input ignored. */
	NoSkip,
};

UCLASS(meta = (DisplayName = "SeinARTS Scenario Library"))
class SEINARTSCOREENTITY_API USeinScenarioBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Global sim pause gate (DESIGN §17). Paused sim halts the tick-system
	 *  loop; commands continue to accumulate in the buffer (Tactical pause —
	 *  Session 5.3 adds a Hard mode). Visual events flush normally. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Scenario",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Set Sim Paused"))
	static void SeinSetSimPaused(const UObject* WorldContextObject, bool bPaused);

	/** Is the sim currently paused? */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Scenario",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Is Sim Paused"))
	static bool SeinIsSimPaused(const UObject* WorldContextObject);

	/** Fire a `PlayPreRenderedCinematic` visual event. If `bBlocksSim` is
	 *  true, the sim pauses first (so UI + cinematic timing align across
	 *  clients). Skip-mode + vote-threshold ride in the event payload.
	 *  `CinematicID` is a gameplay tag designers use to key their cinematic
	 *  assets (SeinARTS convention: `MyGame.Cinematic.Intro`, etc.). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Scenario",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Play Pre-Rendered Cinematic"))
	static void SeinPlayPreRenderedCinematic(
		const UObject* WorldContextObject,
		FGameplayTag CinematicID,
		bool bBlocksSim = true,
		ESeinCinematicSkipMode SkipMode = ESeinCinematicSkipMode::VoteToSkip,
		int32 VoteThreshold = 1);

	/** Fire an `EndCinematic` visual event + resume the sim if it was paused
	 *  by a blocking cinematic. Scenario / skip-vote handlers call this when
	 *  the cinematic ends or the skip threshold is reached. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Scenario",
		meta = (WorldContext = "WorldContextObject", DisplayName = "End Cinematic"))
	static void SeinEndCinematic(const UObject* WorldContextObject, FGameplayTag CinematicID, bool bResumeSimIfPaused = true);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};

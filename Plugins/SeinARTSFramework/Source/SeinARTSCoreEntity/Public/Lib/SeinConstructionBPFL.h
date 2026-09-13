/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinConstructionBPFL.h
 * @author       RJ Macklem
 * @created      02 Jun 2026
 * @latest       11 Sep 2026
 * @brief        Exposes explicit deterministic construction jobs and read-only status.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Components/SeinConstructionTypes.h"
#include "Core/SeinPlayerID.h"
#include "Types/Transform.h"
#include "SeinConstructionBPFL.generated.h"

class ASeinActor;
class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Construction Library", SeinDeterministic))
class SEINARTSCOREENTITY_API USeinConstructionBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Read work measurement stored on the current construction job. Valid is false before a job has been queued. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Construction", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Construction Work Status"))
	static FSeinConstructionWorkStatus SeinGetConstructionWorkStatus(const UObject* WorldContextObject, FSeinEntityHandle Entity);
	/** Read the current state, stage and job. Invalid entities or missing settings return Valid=false. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Construction", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Construction Status"))
	static FSeinConstructionStatus SeinGetConstructionStatus(const UObject* WorldContextObject, FSeinEntityHandle Entity);

	/** True for Queued, Building and Paused. False for Complete or invalid entities. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Construction", meta = (WorldContext = "WorldContextObject", DisplayName = "Is Under Construction"))
	static bool SeinIsUnderConstruction(const UObject* WorldContextObject, FSeinEntityHandle Entity);

	/** Work fraction for a widget. Missing construction or an unqueued entity returns zero; completion does not change work. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Construction", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Construction Work Percent"))
	static FFixedPoint SeinGetConstructionPercent(const UObject* WorldContextObject, FSeinEntityHandle Entity);

	/** Prepare this existing entity as an unfinished site. Returns its job without resetting an already unfinished site.
	 *  Captures the completion effect, clears its stage, resets contributed work, and owns one UnderConstruction tag grant.
	 *  Does not spawn an entity, assign workers, pay resources or start work. Call from an authorized simulation callback. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Construction", meta = (WorldContext = "WorldContextObject", DisplayName = "Queue Construction"))
	static ESeinConstructionResult SeinQueueConstruction(const UObject* WorldContextObject, FSeinEntityHandle Entity, FSeinConstructionHandle& Construction);

	/** Start a queued job or resume a paused one. Work already meeting its requirement returns Work Complete; lifecycle remains Building.
	 *  Repeated calls never reset progress. An old job handle cannot start a newer job. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Construction", meta = (WorldContext = "WorldContextObject", DisplayName = "Start Construction"))
	static ESeinConstructionResult SeinStartConstruction(const UObject* WorldContextObject, FSeinConstructionHandle Construction);

	/** Add positive work in units chosen by the build ability while Building. Uses the work fields on Sein Construction.
	 * Clamps without overflow. Work Complete reports the threshold; it never changes lifecycle or applies completion effects. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Construction", meta = (WorldContext = "WorldContextObject", DisplayName = "Add Construction Work"))
	static ESeinConstructionResult SeinAdvanceConstruction(const UObject* WorldContextObject, FSeinConstructionHandle Construction, FFixedPoint Amount);

	/** Pause a Building job, preserving its progress and stage. Start Construction resumes it. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Construction", meta = (WorldContext = "WorldContextObject", DisplayName = "Pause Construction"))
	static ESeinConstructionResult SeinPauseConstruction(const UObject* WorldContextObject, FSeinConstructionHandle Construction);

	/** Complete any unfinished current job. The ability decides whether work, HP, materials or other requirements are met.
	 * Releases only its own tag grant and schedules its captured effect once. Does not fill or alter contributed work. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Construction", meta = (WorldContext = "WorldContextObject", DisplayName = "Complete Construction"))
	static ESeinConstructionResult SeinCompleteConstruction(const UObject* WorldContextObject, FSeinConstructionHandle Construction);

	/** Legacy alias for Complete Construction, which now accepts any unfinished current job. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Construction", meta = (WorldContext = "WorldContextObject", DisplayName = "Force Complete Construction", DeprecatedFunction, DeprecationMessage = "Use Complete Construction; the calling ability owns completion requirements."))
	static ESeinConstructionResult SeinForceCompleteConstruction(const UObject* WorldContextObject, FSeinConstructionHandle Construction);

	/** Set a designer milestone on an unfinished job. Empty clears it; equal tags produce no change event.
	 *  The stage is construction data, not an entity tag grant. It persists through pause and completion. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Construction", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Construction Stage"))
	static ESeinConstructionResult SeinSetConstructionStage(const UObject* WorldContextObject, FSeinConstructionHandle Construction, FGameplayTag Stage);

	/** Spawn an entity as a construction site before its passive abilities and spawn observers run.
	 *  Requires construction settings on Actor Class. Initial State overrides Start Queued for Construction.
	 *  Resource payment and worker orders remain in the calling ability. Invalid spawn returns an invalid Entity. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Construction", meta = (WorldContext = "WorldContextObject", DisplayName = "Spawn Construction Site"))
	static FSeinEntityHandle SeinSpawnConstructionSite(const UObject* WorldContextObject, TSubclassOf<ASeinActor> ActorClass,
		const FFixedTransform& SpawnTransform, FSeinPlayerID OwnerPlayerID,
		ESeinConstructionInitialState InitialState, FSeinConstructionHandle& Construction);

	/** Legacy advance-and-complete convenience for an already Building job. Use explicit job operations in new graphs. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Construction", meta = (WorldContext = "WorldContextObject", DisplayName = "Add Construction Progress",
		DeprecatedFunction, DeprecationMessage = "Use Add Construction Work and Complete Construction with a construction job handle."))
	static bool SeinAddConstructionProgress(const UObject* WorldContextObject, FSeinEntityHandle Entity, FFixedPoint Amount);

	/** Legacy force completion by entity. Use Complete Construction with a job handle in new graphs. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Construction", meta = (WorldContext = "WorldContextObject", DisplayName = "Finish Construction",
		DeprecatedFunction, DeprecationMessage = "Use Complete Construction with a construction job handle."))
	static void SeinFinishConstruction(const UObject* WorldContextObject, FSeinEntityHandle Entity);

	/** Spawn-only initialization after tag seeding and before ability activation. Never used for snapshot restoration. */
	static void InitializeAtSpawn(USeinWorldSubsystem& World, FSeinEntityHandle Entity,
		const ESeinConstructionInitialState* Override = nullptr);
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimMutationBPFL.h
 * @brief   Restricted-access Blueprint Function Library for mutating sim-side
 *          component state. Callable only from `USeinAbility` and `USeinEffect`
 *          Blueprint graphs (enforced at BP-compile time by RestrictedToClasses).
 *          Each function additionally calls `SEIN_CHECK_SIM()` as a runtime
 *          backstop — functions invoked from outside a sim tick assert in
 *          dev builds.
 *
 *          Prefer field-level setters over whole-struct setters when a field-
 *          level version exists: whole-struct setters clobber every field and
 *          are footguns if another system is also mutating the same entity on
 *          the same tick.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "StructUtils/InstancedStruct.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinProductionComponent.h"
#include "Components/SeinChildTransformsComponent.h"
#include "Types/Quat.h"
#include "Types/Transform.h"
#include "GameplayTagContainer.h"
#include "SeinSimMutationBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Sim Mutation Library", RestrictedToClasses = "SeinAbility,SeinEffect"))
class SEINARTSCOREENTITY_API USeinSimMutationBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Whole-struct setters (escape hatches — clobber every field; prefer field-level where available)
	// ====================================================================================================

	// (The starter combat substrate — FSeinCombatComponent + a combat BPFL —
	//  was removed 2026-06-02; combat will be rebuilt from scratch later.)

	// Movement whole-struct setter intentionally removed — designers use the
	// generic `K2Node_SeinSetComponent` against `FSeinMovementComponent` /
	// `FSeinNavigationComponent`. The Phase-5 decomposition split movement
	// authoring across two structs, and a single whole-struct setter no
	// longer makes sense.

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Ability Data"))
	static bool SeinSetAbilityData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, const FSeinAbilityComponent& NewData);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Production", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Production Data"))
	static bool SeinSetProductionData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, const FSeinProductionComponent& NewData);

	// Passive resource income removed in the Phase-5 refactor — resources
	// flow exclusively through abilities + effects now. If you need passive
	// income, model it as an effect tagged with the resource grant.

	/** Generic whole-struct overwrite — escape hatch for designer-authored
	 *  UDS sim components. Prefer typed setters for known component types. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Component", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Component"))
	static bool SeinSetComponent(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, UScriptStruct* StructType, const FInstancedStruct& NewData);

	// Field-level setters
	// ====================================================================================================
	//
	// NOTE: Ability field-level setters are intentionally omitted —
	//   mutating another ability's runtime state is a footgun. Abilities control
	//   their own lifecycle via OnTick/OnEnd and the activate/cancel command path.
	//   (Combat field-level setters previously lived in the opt-in SeinARTSCombat
	//    module, removed 2026-06-02; combat is TBD.)

	// ─── Movement field-level (removed) ───
	//
	// SetMoveSpeed / SetMovementTarget / SetAcceleration / SetTurnRate were
	// removed as part of the Phase-5 movement decomposition. Designers
	// mutate FSeinMovementComponent fields directly via the generic
	// `K2Node_SeinSetComponent` node — same code path that drives every
	// other sim-component mutation. If a hot-path field-level setter becomes
	// worth the BP API surface area later, add it back here against the new
	// component type.

	// ─── Production field-level ───

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Production", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Rally Point"))
	static bool SeinSetRallyPoint(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FFixedVector NewRallyPoint);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Production", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Current Build Progress"))
	static bool SeinSetCurrentBuildProgress(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FFixedPoint NewProgress);

	// Squad mutations moved to USeinSquadMutationBPFL in the SeinARTSSquad module — keeps CoreEntity independent of the opt-in squad module.

	// ─── Child transforms field-level ───
	//
	// Mutate the per-entity child-transform tree (turret rotation, MG mount
	// aim, hatch state, etc.). Lookups are by tag — DFS through the tree;
	// see USeinChildTransformsBPFL for the read side.

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|ChildTransforms", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Child Local Rotation"))
	static bool SeinSetChildLocalRotation(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag, FFixedQuaternion NewRotation);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|ChildTransforms", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Child Local Transform"))
	static bool SeinSetChildLocalTransform(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag, FFixedTransform NewTransform);

	/** Workhorse turret-aim helper. Composes the named child's current
	 *  WORLD rotation, derives the desired yaw to face `WorldTarget`,
	 *  ShortestAngleDelta + clamp at TurnRateRadPerSec × DeltaTime, then
	 *  writes the new LOCAL rotation accounting for the parent's world
	 *  rotation. One call per turret per ability tick. Returns false if
	 *  handle / component / tag don't resolve. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|ChildTransforms", meta = (WorldContext = "WorldContextObject", DisplayName = "Turn Child Toward"))
	static bool SeinTurnChildToward(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag,
		FFixedVector WorldTarget, FFixedPoint TurnRateRadPerSec, FFixedPoint DeltaTime);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};

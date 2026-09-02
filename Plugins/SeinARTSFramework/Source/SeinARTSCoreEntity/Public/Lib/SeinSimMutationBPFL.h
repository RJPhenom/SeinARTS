/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimMutationBPFL.h
 * @brief   Restricted-access Blueprint Function Library for mutating sim-side
 *          component state. Callable only from `USeinAbility` and `USeinEffect`
 *          Blueprint graphs (enforced at BP-compile time by RestrictedToClasses).
 *          Every function passes the functional all-build world-mutation gate:
 *          stopped tick-zero Applying materialization and running fixed-tick
 *          callbacks are accepted; post-seal/off-tick calls are rejected in
 *          Shipping as well as development builds.
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
#include "Core/SeinPlayerID.h"
#include "StructUtils/InstancedStruct.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Components/SeinAbilityPayload.h"
#include "Components/SeinProductionPayload.h"
#include "Components/SeinChildTransformsPayload.h"
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

	// (Combat is designer-owned: there is no framework vitals/weapon schema.
	//  Damage, healing, suppression, and any other stat drain are `Apply Field
	//  Delta` calls against the designer's own component struct — see below.)

	// Movement whole-struct setter intentionally removed — designers use the
	// generic `K2Node_SeinSetComponent` against `FSeinMovementPayload` /
	// `FSeinNavigationPayload`. The Phase-5 decomposition split movement
	// authoring across two structs, and a single whole-struct setter no
	// longer makes sense.

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Ability Data"))
	static bool SeinSetAbilityData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, const FSeinAbilityPayload& NewData);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Production", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Production Data"))
	static bool SeinSetProductionData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, const FSeinProductionPayload& NewData);

	// Passive resource income removed in the Phase-5 refactor — resources
	// flow exclusively through abilities + effects now. If you need passive
	// income, model it as an effect tagged with the resource grant.

	/** Generic whole-struct overwrite — escape hatch for designer-authored
	 *  UDS sim components. Prefer typed setters for known component types. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Component", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Component"))
	static bool SeinSetComponent(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, UScriptStruct* StructType, const FInstancedStruct& NewData);

	/** Saturating add-then-clamp on ONE fixed-point field of any sim component
	 *  — the schema-agnostic "change a stat" verb. Damage, healing, suppression
	 *  build-up, morale drain, ammo spend, and shield recharge are all this
	 *  node against the designer's own struct (native or UDS), so the framework
	 *  never has to know what "health" is.
	 *
	 *  Semantics (deterministic, fixed-point only):
	 *   - `Delta` is added with saturation (no wraparound). Each clamp bound
	 *     applies only while its flag is on: `bClampMin` floors the result at
	 *     `MinValue`, `bClampMax` ceils it at `MaxValue`. Both flags default OFF,
	 *     so an unwired node is a plain saturating add — never a silent zeroing.
	 *     Typical damage: `bClampMin` on with `MinValue` 0; typical heal:
	 *     `bClampMax` on with `MaxValue` = the entity's own max field. With both
	 *     on, `MinValue > MaxValue` rejects the call.
	 *   - The field is written only when the result differs from the current
	 *     value, so a no-op never dirties the component's mutation revision.
	 *   - `FieldName` accepts the internal property name or, for UDS
	 *     components, the authored (display) name the designer typed (resolved
	 *     identically in editor and cooked builds).
	 *
	 *  Outputs: `NewValue` is the field after the call; `bChanged` is whether a
	 *  write happened; `bAtMin` / `bAtMax` report the field now sitting on an
	 *  ENABLED clamp bound (e.g. `bAtMin` with MinValue 0 = "health hit zero",
	 *  the designer's cue to Notify Death / Destroy Entity / apply a downed
	 *  effect — death is their call, not the framework's). All outputs are
	 *  zero/false whenever the call returns false.
	 *  @return false when the entity, component, or fixed-point field cannot be
	 *  resolved, or the caller lacks mutation authorization. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Component", meta = (WorldContext = "WorldContextObject", DisplayName = "Apply Field Delta"))
	static bool SeinApplyFieldDelta(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, UScriptStruct* StructType, FName FieldName, FFixedPoint Delta, bool bClampMin, FFixedPoint MinValue, bool bClampMax, FFixedPoint MaxValue, FFixedPoint& NewValue, bool& bChanged, bool& bAtMin, bool& bAtMax);

	// Field-level setters
	// ====================================================================================================
	//
	// NOTE: Ability field-level setters are intentionally omitted —
	//   mutating another ability's runtime state is a footgun. Abilities control
	//   their own lifecycle via OnTick/OnEnd and the activate/cancel command path.
	//   (There are deliberately no combat field-level setters: the framework
	//    ships no vitals/weapon schema. Use `Apply Field Delta` above against
	//    your own component struct.)

	// ─── Movement field-level (removed) ───
	//
	// SetMoveSpeed / SetMovementTarget / SetAcceleration / SetTurnRate were
	// removed as part of the Phase-5 movement decomposition. Designers
	// mutate FSeinMovementPayload fields directly via the generic
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

	// ─── Player-pair capabilities ───
	//
	// Directional runtime relationship mutation (ShareVision and any custom
	// capability tag) from ability/effect graphs — the designer-authored path
	// for diplomacy mechanics, treaty effects, and scripted vision sharing.
	// Direction matters: Source -> Target means Target may consume Source's
	// capability (for ShareVision, Target sees what Source sees). Grants are
	// refcounted per exact (kind, instance) source; revoke removes only the
	// matching ref. The lobby's team seeding provides the match-start default;
	// these nodes update it at runtime, asymmetrically if desired. The wire
	// SetPairCapability command stays MatchControl-scoped for admin/scenario
	// tooling — player-driven changes route through an ability like all other
	// gameplay.

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Relationship", meta = (WorldContext = "WorldContextObject", DisplayName = "Grant Pair Capability"))
	static bool SeinGrantPairCapability(const UObject* WorldContextObject,
		FSeinPlayerID SourcePlayer, FSeinPlayerID TargetPlayer,
		UPARAM(meta = (Categories = "SeinARTS.Relationship.Capability")) FGameplayTag CapabilityTag,
		UPARAM(meta = (Categories = "SeinARTS.Relationship.Source")) FGameplayTag SourceKindTag,
		int64 SourceInstanceID);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Relationship", meta = (WorldContext = "WorldContextObject", DisplayName = "Revoke Pair Capability"))
	static bool SeinRevokePairCapability(const UObject* WorldContextObject,
		FSeinPlayerID SourcePlayer, FSeinPlayerID TargetPlayer,
		UPARAM(meta = (Categories = "SeinARTS.Relationship.Capability")) FGameplayTag CapabilityTag,
		UPARAM(meta = (Categories = "SeinARTS.Relationship.Source")) FGameplayTag SourceKindTag,
		int64 SourceInstanceID);

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

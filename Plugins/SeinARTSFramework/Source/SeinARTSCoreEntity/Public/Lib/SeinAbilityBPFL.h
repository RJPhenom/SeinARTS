/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinAbilityBPFL.h
 * @author       RJ Macklem
 * @created      27 Mar 2026
 * @latest       14 Aug 2026
 * @brief        Exposes ability commands, availability, cooldown, and grant APIs to Blueprint.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Abilities/SeinAbilityTypes.h"
#include "Components/SeinAbilityPayload.h"
#include "SeinAbilityBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Ability Library", SeinDeterministic))
class SEINARTSCOREENTITY_API USeinAbilityBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Read Component Data
	// ====================================================================================================

	/** Read FSeinAbilityPayload for an entity. Returns false and logs a warning if the handle
	 *  is invalid or the entity lacks the component; OutData is untouched on failure. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Ability Data"))
	static bool SeinGetAbilityData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinAbilityPayload& OutData);

	/** Batch read FSeinAbilityPayload. Invalid/missing entities are skipped (warning logged); the
	 *  returned array may be shorter than the input. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Ability Data"))
	static TArray<FSeinAbilityPayload> SeinGetAbilityDataMany(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& EntityHandles);

	// Command
	// ====================================================================================================

	/** Activates an ability immediately without entering the command queue. This
	 *  bypasses command authority, tag/target/path/Can Activate checks,
	 *  Auto Move Then, cost deduction, cancellation arbitration, footprint
	 *  placement, and command-rejection reporting. It still obeys cooldown,
	 *  active-slot, owned-tag acquisition, and exact lifecycle ownership: indexes are
	 *  coherent during callbacks, and a live primary must be ended through the
	 *  normal cancellation-tag/broker policy before another primary can start.
	 *  Returns silently on failure (ability missing, on cooldown, already
	 *  active, or primary slot occupied).
	 *
	 *  Use this only for low-level scripting where the gates are intentionally
	 *  unwanted (cheats, debug commands, replay reconstruction). For ability
	 *  authoring (one ability triggering another), use `SeinIssueAbilityCommand`
	 *  below — it enqueues a command through ProcessCommands so all gates run
	 *  exactly as if the player had clicked. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Activate Ability (Direct)"))
	static void SeinActivateAbility(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag, FSeinEntityHandle TargetEntity, FFixedVector TargetLocation);

	/** Issues an ability command through the lockstep queue for the next
	 *  simulation tick. Command authority, cooldown, required/blocked tags,
	 *  target/range/visibility/pathability, Can Activate, owned-tag capacity,
	 *  cost, and cancellation arbitration run before activation. A targeter-
	 *  originated broker order also runs its footprint placement gate before
	 *  member dispatch. Rejections carry the failed gate's reason tag.
	 *
	 *  This is the right node for one ability chaining into another (e.g. a
	 *  PlaceBarracks ability automatically issuing a Build order on the placer
	 *  targeting the new barracks). The owning PlayerID is derived from
	 *  `EntityHandle`'s entity owner — caller doesn't supply it. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Issue Ability Command"))
	static void SeinIssueAbilityCommand(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag, FSeinEntityHandle TargetEntity, FFixedVector TargetLocation);

	/** Issue a broker-scope ability order from inside an ability graph.
	 *
	 *  Resolves the caller's broker (via `FSeinBrokerMembershipData::CurrentBrokerHandle`)
	 *  and enqueues a broker order with `PredeterminedAbilityTag = AbilityTag`.
	 *  On dispatch, the broker's resolver consults the ability's
	 *  `DispatchMode` policy to fan the order out — All to every capable
	 *  member, Single to one selected member, ByTag to a tag-matched subset.
	 *
	 *  Use this from chaining abilities that should trigger a broker-wide
	 *  reaction: e.g. SA_PlaceBarracks (fires Single on the leader) spawns
	 *  the barracks and then calls this BPFL with `SeinARTS.Ability.Build` so
	 *  every member of the leader's squad/selection moves to build the
	 *  barracks in parallel (when Build is authored with `DispatchMode: All`).
	 *
	 *  Falls back to `SeinIssueAbilityCommand` (single-entity dispatch) when
	 *  the caller has no broker membership — lone units still chain cleanly.
	 *
	 *  @param CallerEntity   The entity whose broker we address (typically `self`
	 *                        from inside an ability OnActivate graph).
	 *  @param AbilityTag     The ability to dispatch. Must match a tag granted
	 *                        on the broker's members (or the broker carrier
	 *                        entity itself, for squad-owned abilities).
	 *  @param TargetEntity   Optional target entity (e.g. the spawned barracks).
	 *  @param TargetLocation Target world location (build site, move
	 *                        destination, etc.).
	 *  @param bQueueCommand  true = shift-chained (append behind existing
	 *                        orders); false = inserted in normal queue order.
	 *                        Most ability-chain use cases want false. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Broker",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Issue Broker Order From Entity"))
	static void SeinIssueBrokerOrderFromEntity(
		const UObject* WorldContextObject,
		FSeinEntityHandle CallerEntity,
		FGameplayTag AbilityTag,
		FSeinEntityHandle TargetEntity,
		FFixedVector TargetLocation,
		bool bQueueCommand = false);

	/** Cancels the currently active primary ability on an entity. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Cancel Ability"))
	static void SeinCancelAbility(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	// ─── Runtime ability grant / revoke ───
	//
	// Lets designers add or remove ability instances from an entity at
	// runtime. Tech effects' OnApply BP graphs are the primary caller for
	// grant; effect-revoke or expiration calls revoke. The declarative
	// `GrantedAbilities` field on USeinEffect auto-drives these primitives
	// during effect application with class-tag fan-out across the player's
	// entities, but the primitives are useful standalone for scripted
	// faction-quest abilities, dynamic loadouts, commander-pick changes,
	// etc.
	//
	// Reference-counted semantics: Grant bumps a per-instance refcount;
	// default Revoke decrements it. The ability is only destroyed when the
	// count reaches zero. This protects natively-authored grants from
	// effect-driven revokes (and protects each effect's grant from other
	// effects' revokes when two effects grant the same class). Designers
	// who need to fully destroy an ability regardless of remaining holders
	// use the `ForceRevoke*` variants.
	//
	// Native grants seed at refcount=1 during `InitializeEntityAbilities`.
	// Each effect that grants the same class adds 1. Each revoke from one
	// of those sources subtracts 1. Instance stays alive as long as at
	// least one source still holds it.

	/** Grant an ability instance to an entity at runtime.
	 *
	 *  Instantiates the ability class on first grant: NewObject in the world
	 *  subsystem, InitializeAbility binds to the entity + world,
	 *  RegisterAbilityInstance gives it a pool ID. Passive abilities
	 *  auto-activate on first grant; primary abilities sit idle until
	 *  triggered through the normal command flow.
	 *
	 *  Refcounted: if the entity already holds this class, the call bumps
	 *  the existing instance's grant refcount and returns its pool ID. The
	 *  instance is only destroyed when a matching number of revoke calls
	 *  drops the count back to zero.
	 *
	 *  Marks the entity's broker (if any) capability-map dirty so the next
	 *  dispatch sees the new ability tag.
	 *
	 *  Returns the pool ID of the (new or existing) ability instance, or
	 *  INDEX_NONE on failure (invalid entity, no AbilityComponent, null
	 *  class, abstract class). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Grant Ability"))
	static int32 SeinGrantAbility(const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle,
		TSubclassOf<class USeinAbility> AbilityClass);

	/** Framework-internal source-aware grant used by active effects. The source
	 *  ID is committed before passive activation and is the only reference a
	 *  matching effect teardown may later consume. */
	static int32 SeinGrantAbilityFromEffect(const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle,
		TSubclassOf<class USeinAbility> AbilityClass,
		int64 EffectInstanceID);

	/** Consume one grant owned by `EffectInstanceID`. A missing source record is
	 *  a no-op even if the same class has since been granted by someone else. */
	static int32 SeinRevokeAbilityFromEffect(const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle,
		TSubclassOf<class USeinAbility> AbilityClass,
		int64 EffectInstanceID);

	/** Revoke one grant of every ability instance on the entity whose
	 *  AbilityTag matches. Consumes an anonymous/native grant first; if none
	 *  exists, consumes the oldest effect-owned source and prunes that effect's
	 *  live ledger claim. Decrements the grant refcount per matching
	 *  instance; only fully destroys the instance (cancel active → unregister
	 *  pool → remove from arrays) when the refcount hits zero. Dirties the
	 *  broker capability map when any instance is actually destroyed.
	 *
	 *  Returns the number of instances actually DESTROYED — not the number
	 *  decremented. Compare against `SeinHasAbility` / `SeinGetAbilityGrantCount`
	 *  if you need to tell whether a decrement happened without destroy. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Revoke Ability By Tag"))
	static int32 SeinRevokeAbilityByTag(const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle,
		FGameplayTag AbilityTag);

	/** Revoke one grant of the entity's instance of the given ability class.
	 *  Same anonymous-first/source-pruning semantics as `SeinRevokeAbilityByTag` — only
	 *  destroys at zero count. Use when the caller has a concrete class
	 *  reference (typical for effect-driven revokes that remember which
	 *  classes they granted).
	 *
	 *  Returns 1 if the instance was destroyed, 0 otherwise (decremented or
	 *  not held). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Revoke Ability By Class"))
	static int32 SeinRevokeAbilityByClass(const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle,
		TSubclassOf<class USeinAbility> AbilityClass);

	/** Force-destroy every ability instance on the entity whose AbilityTag
	 *  matches, **ignoring the grant refcount**. Cancels active, frees pool
	 *  slot, removes the class from GrantedAbilities. Use sparingly — the
	 *  designer is asserting that no remaining holders need this ability.
	 *  Effect-driven holders that later try to revoke will silently no-op
	 *  (their refcount was zeroed under them).
	 *
	 *  Returns the number of instances destroyed. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Force Revoke Ability By Tag"))
	static int32 SeinForceRevokeAbilityByTag(const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle,
		FGameplayTag AbilityTag);

	/** Force-destroy the entity's instance of the given ability class,
	 *  ignoring the grant refcount. See `SeinForceRevokeAbilityByTag` for
	 *  the "use sparingly" caveat.
	 *
	 *  Returns 1 if the instance was destroyed, 0 if the entity didn't
	 *  hold it. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Force Revoke Ability By Class"))
	static int32 SeinForceRevokeAbilityByClass(const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle,
		TSubclassOf<class USeinAbility> AbilityClass);

	/** Check whether an entity holds an instance of the given ability class.
	 *  Companion to `SeinHasAbility` (which queries by tag). Cheaper than
	 *  fetching the full ability component just to test existence. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Ability",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Has Ability Of Class"))
	static bool SeinHasAbilityOfClass(const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle,
		TSubclassOf<class USeinAbility> AbilityClass);

	/** Read the outstanding grant refcount for an entity's ability of the
	 *  given class. Returns 0 if the entity doesn't hold it, otherwise the
	 *  number of outstanding grants (1 = single source, 2 = two sources,
	 *  etc.). Useful for designer scripts that want to know "am I the last
	 *  holder?" before deciding to revoke. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Ability",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Ability Grant Count"))
	static int32 SeinGetAbilityGrantCount(const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle,
		TSubclassOf<class USeinAbility> AbilityClass);

	/** Check whether a specific ability is on cooldown */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Is Ability On Cooldown"))
	static bool SeinIsAbilityOnCooldown(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag);

	/** Get the remaining cooldown time for a specific ability */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Cooldown Remaining"))
	static FFixedPoint SeinGetCooldownRemaining(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag);

	/** Check whether an entity has an ability with the given tag */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Has Ability"))
	static bool SeinHasAbility(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag);

	// Availability
	// ====================================================================================================

	/** Returns an advisory availability snapshot for one ability on one entity. Matches the
	 *  shape of USeinProductionBPFL::SeinGetProductionAvailability for uniform UI
	 *  handling. It checks cooldown, required/blocked tags, optional target/range/
	 *  visibility, Can Activate, and affordability, then reports the first failure.
	 *  The queued command remains authoritative and may additionally reject for
	 *  authority, pathability, footprint placement, owned-tag capacity, or changed
	 *  state before its simulation tick executes.
	 *
	 *  Target context:
	 *    - Pass invalid `OptionalTargetEntity` AND zero `OptionalTargetLocation` to
	 *      skip target-validation gates (range / LOS / ValidTargetTags). UI callers
	 *      use this mode for "is this ability button enabled in principle" queries.
	 *    - Pass a valid Target OR a non-zero Location to evaluate per-click
	 *      preview ("would this succeed against THIS target / at THIS point"). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Ability Availability"))
	static FSeinAbilityAvailability SeinGetAbilityAvailability(
		const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle,
		FGameplayTag AbilityTag,
		FSeinEntityHandle OptionalTargetEntity,
		FFixedVector OptionalTargetLocation);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};

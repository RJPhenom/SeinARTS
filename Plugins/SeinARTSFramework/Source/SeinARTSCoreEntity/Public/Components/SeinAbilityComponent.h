#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Abilities/SeinAbility.h"
#include "Abilities/SeinAbilityTypes.h"
#include "Components/SeinComponent.h"
#include "SeinAbilityComponent.generated.h"

class USeinWorldSubsystem;

/** Deterministic ownership of one runtime ability instance. Ordinary/native
 *  grants are anonymous; effect grants carry their world-global effect ID so
 *  teardown can consume only its own reference after callback-driven reuse. */
USTRUCT(meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinAbilityGrantOwnership
{
	GENERATED_BODY()

	UPROPERTY()
	int32 AnonymousGrantCount = 0;

	/** One entry per committed effect grant. Duplicates are legal when an
	 *  authored effect lists the same ability class more than once. */
	UPROPERTY()
	TArray<int64> EffectInstanceIDs;
};

FORCEINLINE uint32 GetTypeHash(const FSeinAbilityGrantOwnership& Ownership)
{
	uint32 Hash = GetTypeHash(Ownership.AnonymousGrantCount);
	Hash = HashCombine(Hash, GetTypeHash(Ownership.EffectInstanceIDs.Num()));
	for (int64 EffectID : Ownership.EffectInstanceIDs)
	{
		Hash = HashCombine(Hash, GetTypeHash(EffectID));
	}
	return Hash;
}

template<>
struct TStructOpsTypeTraits<FSeinAbilityGrantOwnership>
	: public TStructOpsTypeTraitsBase2<FSeinAbilityGrantOwnership>
{
	enum { WithGetTypeHash = true };
};

/**
 * Component tracking an entity's abilities.
 *
 * Holds granted ability classes (designer-authored + runtime-mutated),
 * runtime instances, active primary + passive tracking, and the command
 * resolver (DefaultCommands + FallbackAbilityTag). Designers author command
 * mappings here; a single edit surface for "which ability fires for which
 * input context."
 *
 * Component data is pure — no live UObject refs. Ability instances are
 * stored as `int32` IDs into a pool managed by `USeinWorldSubsystem`. This
 * makes:
 *   - State hash deterministic across processes (int32 IDs, not pointer values)
 *   - World snapshots portable (IDs survive disk/wire round-trips)
 *   - Reflection-driven hashing covers everything (no manual GetTypeHash needed)
 *
 * Pool registration happens in `USeinWorldSubsystem::InitializeEntityAbilities`;
 * unregister + free fires when the entity is destroyed. Walk sites use the
 * `Get*` accessors below (which take a `USeinWorldSubsystem&` so the pool
 * lookup happens at the call site rather than caching a pointer here).
 *
 * Grant lifecycle is source-aware and reference-counted. The three parallel
 * arrays identify each runtime instance, its cached total refcount, and its
 * anonymous/effect-ID owners. Effect teardown consumes only its own source ID;
 * callback-driven force-revoke/regrant therefore cannot steal a replacement
 * holder. `SeinForceRevokeAbility*` remains the explicit all-owner escape hatch.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinAbilityComponent : public FSeinComponent
{
	GENERATED_BODY()

	/** Ability classes granted to this entity (designer-authored on the
	 *  entity bridge's ComponentData array; instantiated at spawn into the
	 *  ability pool). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Ability")
	TArray<TSubclassOf<USeinAbility>> GrantedAbilities;

	/** Pool IDs for runtime ability instances. Indices into
	 *  `USeinWorldSubsystem::AbilityPool`. INDEX_NONE = no ability.
	 *  Parallel to `AbilityGrantCounts`. */
	UPROPERTY()
	TArray<int32> AbilityInstanceIDs;

	/** Outstanding grant refcount per instance, parallel to
	 *  `AbilityInstanceIDs`. Each `SeinGrantAbility` call on an already-held
	 *  class bumps the matching entry; each `SeinRevokeAbility*` call
	 *  decrements it. Instance is destroyed + index removed from both
	 *  arrays when count reaches zero. Natively-authored abilities seed at 1
	 *  during `InitializeEntityAbilities`; effect-driven grants add on top.
	 *  See class docstring for full lifecycle. */
	UPROPERTY()
	TArray<int32> AbilityGrantCounts;

	/** Source ownership parallel to `AbilityInstanceIDs`. The cached
	 *  `AbilityGrantCounts[i]` must equal AnonymousGrantCount plus the number of
	 *  EffectInstanceIDs; snapshot restore rejects any mismatch. */
	UPROPERTY()
	TArray<FSeinAbilityGrantOwnership> AbilityGrantOwnership;

	/** Pool ID of the currently-active primary ability (not passive).
	 *  INDEX_NONE = nothing active. The lifecycle owner publishes this before
	 *  OnActivate and clears it before OnEnd. A second primary cannot silently
	 *  displace a live one: cancellation-tag or broker arbitration must end the
	 *  current primary first. */
	UPROPERTY()
	int32 ActiveAbilityID = INDEX_NONE;

	/** Pool IDs for currently-running passive abilities, in activation order.
	 *  Each ID is present throughout OnActivate and removed before OnEnd. */
	UPROPERTY()
	TArray<int32> ActivePassiveIDs;

	/**
	 * Default command mappings for this entity. Maps input contexts (tag sets) to
	 * ability tags. The player controller's smart-command resolver runs these to
	 * pick which ability to activate on right-click. Sorted by priority at resolve
	 * time — highest priority match wins.
	 *
	 * See FSeinCommandMapping for detailed usage examples.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Ability")
	TArray<FSeinCommandMapping> DefaultCommands;

	/**
	 * Fallback ability tag when no command mapping matches. Typically
	 * `SeinARTS.Ability.Move` so unmapped contexts default to move.
	 * If empty, no command is issued for unmatched contexts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Ability")
	FGameplayTag FallbackAbilityTag;

	// ========== Accessors (pool lookup helpers) ==========

	/** Resolve `ActiveAbilityID` to a live ability ref via the world's pool.
	 *  Returns null on INDEX_NONE / unregistered ID / null world. */
	USeinAbility* GetActiveAbility(const USeinWorldSubsystem& World) const;

	/** Materialize the live ability instance array from `AbilityInstanceIDs`.
	 *  Result is sized + filled in iteration order; null entries indicate
	 *  freed pool slots (rare; typically the whole component is removed
	 *  before slot release). Hot-path callers should iterate IDs + call
	 *  `World.GetAbilityInstance(ID)` directly to avoid the array copy. */
	TArray<USeinAbility*> GetAbilityInstances(const USeinWorldSubsystem& World) const;

	/** Same shape for active passives. */
	TArray<USeinAbility*> GetActivePassives(const USeinWorldSubsystem& World) const;

	/** Find an ability instance by its AbilityTag. Walks `AbilityInstanceIDs`,
	 *  resolving each ID through the pool. */
	USeinAbility* FindAbilityByTag(const USeinWorldSubsystem& World, const FGameplayTag& Tag) const;

	/** Check whether this component has an ability with the given tag. */
	bool HasAbilityWithTag(const USeinWorldSubsystem& World, const FGameplayTag& Tag) const;

	/** Find the first ability instance on this entity flagged
	 *  `bIsMoveAbility = true`, or nullptr if none. Linear walk — ability
	 *  lists are short (typically <20). Used by the framework's auto-move
	 *  plumbing instead of a hardcoded tag lookup. */
	USeinAbility* FindMoveAbility(const USeinWorldSubsystem& World) const;

	/** Convenience: true iff `FindMoveAbility` would return non-null. */
	bool HasMoveAbility(const USeinWorldSubsystem& World) const;

	/** Check whether this component holds an instance of the given class. */
	bool HasAbilityOfClass(const USeinWorldSubsystem& World, const UClass* AbilityClass) const;

	/** Read the outstanding grant refcount for an instance of `AbilityClass`.
	 *  Returns 0 if the entity doesn't hold it; otherwise the number of
	 *  outstanding grants (1 = single source, 2 = two sources, etc.). */
	int32 GetAbilityGrantCount(const USeinWorldSubsystem& World, const UClass* AbilityClass) const;

	/**
	 * Resolve a command context to an ability tag. Walks DefaultCommands in
	 * priority order; returns FallbackAbilityTag if no mapping's RequiredContext
	 * is satisfied. Pure data lookup — doesn't need the world.
	 */
	FGameplayTag ResolveCommandContext(const FGameplayTagContainer& Context) const;
};

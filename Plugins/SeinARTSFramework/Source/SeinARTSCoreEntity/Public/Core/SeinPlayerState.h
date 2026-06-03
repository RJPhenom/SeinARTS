/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPlayerState.h
 * @brief   Sim-side player data (NOT APlayerState). Tracks faction, team,
 *          resources, and elimination status for a single player slot.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinFactionID.h"
#include "Types/FixedPoint.h"
#include "GameplayTagContainer.h"
#include "Data/SeinResourceTypes.h"
#include "Effects/SeinActiveEffect.h"
#include "SeinPlayerState.generated.h"

/**
 * Sim-side player state. One per player slot in the lockstep simulation.
 * Holds faction assignment, team, tag-keyed resources, and elimination flag.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinPlayerState
{
	GENERATED_BODY()

	/** Unique player identifier */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Player")
	FSeinPlayerID PlayerID;

	/** Faction this player is playing as */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Player")
	FSeinFactionID FactionID;

	/** Team index for alliance grouping (0 = no team / FFA) */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Player")
	uint8 TeamID = 0;

	/**
	 * Player resources keyed by resource gameplay tag (under SeinARTS.Resource.*).
	 * Catalog entries in USeinARTSCoreSettings + per-faction ResourceKit entries
	 * determine which tags are populated on player registration.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Economy",
		meta = (Categories = "SeinARTS.Resource"))
	TMap<FGameplayTag, FFixedPoint> Resources;

	/**
	 * Per-resource cap overrides. Catalog entries supply the default cap;
	 * faction kit entries + runtime modifiers may override. If a tag is absent
	 * here the catalog/kit default applies.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Economy",
		meta = (Categories = "SeinARTS.Resource"))
	TMap<FGameplayTag, FFixedPoint> ResourceCaps;

	/** Whether this player has been eliminated from the match */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Player")
	bool bEliminated = false;

	/** Whether this player has signalled ready (pre-game) */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Player")
	bool bReady = false;

	/** If true, `ProcessCommands` rejects any sim-mutating command from this
	 *  player — only observer commands (camera, chat, ping) are accepted (DESIGN §18). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Player")
	bool bIsSpectator = false;

	/** If true, this player slot is driven by an AI controller (DESIGN §16 / §18).
	 *  Used by host-migration + dropped-player AI takeover plumbing. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Player")
	bool bIsAI = false;

	// --- Player Tags (DESIGN §10 — refcounted, mirrors entity FSeinEntityTagState) ---

	/** Refcount per tag. A tag is present (in `PlayerTags`) iff its count > 0.
	 *  Maintained by `USeinWorldSubsystem::GrantPlayerTag` / `UngrantPlayerTag`
	 *  on apply/remove of Class + Player scope effects. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Tech")
	TMap<FGameplayTag, int32> PlayerTagRefCounts;

	/** Cached presence set mirroring `PlayerTagRefCounts`. Queries (`HasTag`
	 *  / `HasAll`) route through this container for hot-path lookups. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Tech")
	FGameplayTagContainer PlayerTags;

	// --- Effects (DESIGN §8 3-scope) ---

	/** Class-scope effects applied to this player. Each effect's CDO modifiers
	 *  target entity component fields filtered by `TargetClassTag` at attribute
	 *  resolve time; every owned entity with the matching tag receives the influence. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Effects")
	TArray<FSeinActiveEffect> ClassEffects;

	/** Player-scope effects applied to this player. Modifiers target player-state
	 *  sub-structs (resource income rates, caps, pop cap, upkeep) via the dedicated
	 *  player-attribute resolver path. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Effects")
	TArray<FSeinActiveEffect> PlayerEffects;

	/** Monotonically increasing ID counter for effect instances on this player (not
	 *  globally unique — scoped to `ClassEffects` + `PlayerEffects`). */
	uint32 NextEffectInstanceID = 1;

	FSeinPlayerState() = default;
	explicit FSeinPlayerState(FSeinPlayerID InPlayerID, FSeinFactionID InFactionID, uint8 InTeamID = 0);

	// --- Resource helpers ---

	/** Plain balance-comparison affordability check. Treats every cost entry as
	 *  `DeductFromBalance` — does NOT consult the catalog's `CostDirection` or
	 *  `SpendBehavior`. For catalog-aware checks (pop / supply / AddTowardCap
	 *  resources or allow-debt behavior) call `USeinResourceBPFL::SeinCanAfford`
	 *  instead. Kept on the struct for hot-path call sites that authored their
	 *  costs as plain stockpile spend. */
	bool CanAfford(const FSeinResourceCost& Cost) const;

	/** Deducts resources if affordable (plain balance math, see CanAfford). */
	bool DeductResources(const FSeinResourceCost& Cost);

	/** Adds resources to the player. Creates entries for new resource tags. No
	 *  cap clamping — use `USeinResourceBPFL::SeinGrantIncome` for cap-aware adds. */
	void AddResources(const FSeinResourceCost& Amount);

	/** Returns the current amount of a tagged resource, or zero if not present. */
	FFixedPoint GetResource(FGameplayTag ResourceTag) const;

	/** Sets a tagged resource to an exact value. Creates the entry if needed. */
	void SetResource(FGameplayTag ResourceTag, FFixedPoint Amount);

	// --- Tag / Tech helpers ---

	/** True if the specified tag is currently present on this player (refcount > 0). */
	bool HasPlayerTag(FGameplayTag Tag) const;

	/** True if every tag in the container is currently present. Empty container = true. */
	bool HasAllPlayerTags(const FGameplayTagContainer& Required) const;

	/** Alias for `HasAllPlayerTags` — kept for call-site familiarity during the tech
	 *  migration. "Tech tags" are just player tags under the §10 unification. */
	bool HasAllTechTags(const FGameplayTagContainer& Required) const { return HasAllPlayerTags(Required); }
};

FORCEINLINE uint32 GetTypeHash(const FSeinPlayerState& State)
{
	return GetTypeHash(State.PlayerID);
}

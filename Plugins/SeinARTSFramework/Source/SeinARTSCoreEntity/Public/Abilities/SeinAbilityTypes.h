#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "SeinAbilityTypes.generated.h"

/**
 * Determines what kind of target an ability requires.
 */
UENUM(BlueprintType)
enum class ESeinAbilityTargetType : uint8
{
	None,		// No target needed
	Self,		// Auto-targets self
	Entity,		// Targets another entity
	Point,		// Targets a world location
	Area,		// Location + radius
	Passive		// Always active
};

/**
 * When the ability's cooldown begins ticking.
 * DESIGN §7 Q4c.
 */
UENUM(BlueprintType)
enum class ESeinCooldownStartTiming : uint8
{
	/** Cooldown begins immediately on successful activation (sprint buffs, most abilities). */
	OnActivate,
	/** Cooldown begins when the ability ends (grenade throw — cooldown starts after
	 *  animation + projectile spawn). */
	OnEnd
};

/**
 * Where the ability's cooldown is applied when activated by a member of a squad.
 * Read by the cooldown system after a successful activation.
 *
 * Has no effect on lone (non-squad-member) entities — they always behave as Member.
 */
UENUM(BlueprintType)
enum class ESeinCooldownScope : uint8
{
	/** Only the activating member's instance of the ability goes on cooldown.
	 *  Stackable per-member abilities — e.g., each soldier has their own
	 *  "throw frag" cooldown so all 4 grenadiers can throw in close succession. */
	Member,

	/** All squad members' instances of the ability tag go on cooldown when ANY
	 *  member activates. Default — matches the "the squad threw the grenade,
	 *  the whole squad waits before throwing again" semantic. For non-squad entities this
	 *  collapses to Member behavior automatically. */
	Squad
};

/**
 * What happens when an ability is activated with a target that's outside its MaxRange.
 * DESIGN §7 Q7.
 */
UENUM(BlueprintType)
enum class ESeinOutOfRangeBehavior : uint8
{
	/** Ability fails if out of range (grenade, snipe). */
	Reject,
	/** Framework auto-queues a move-to-range prefix; ability re-attempts on arrival
	 *  (attack, harvest). Integrates with §5 CommandBrokers in Phase 4 — pre-Phase 4
	 *  this behaves identically to Reject. */
	AutoMoveThen
};

// FSeinAbilityRequirements removed (refactored 2026-05-06): tag-gating
// is now expressed directly on USeinAbility via three Arbitration containers
// — RequiredEntityTags, RequiredPlayerTags, BlockedTags — instead of the
// nested struct. Single-level authoring is clearer in the details panel
// and the previous Requirements struct was never actually consulted by any
// activation gate.

/**
 * A single mapping from a command context (set of gameplay tags describing the
 * click/input context) to the ability that should be activated.
 *
 * Lives on FSeinAbilityComponent::DefaultCommands (per DESIGN §7 Q9 — moved off the
 * entity bridge's `FSeinAbilityComponent::DefaultCommands` array). When the player right-clicks,
 * the controller builds a context tag set and finds the highest-priority mapping
 * whose RequiredContext is a subset of the actual context.
 *
 * Example for a Medic:
 *   Priority 100: {RightClick, Target.Friendly, Target.Transport} → Ability.Embark
 *   Priority  50: {RightClick, Target.Friendly}                   → Ability.Heal
 *   Priority  50: {RightClick, Target.Enemy}                      → Ability.Attack
 *   Priority   0: {RightClick, Target.Ground}                     → Ability.Move
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinCommandMapping
{
	GENERATED_BODY()

	/** Context tags that must ALL be present for this mapping to match. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Command")
	FGameplayTagContainer RequiredContext;

	/** Ability tag to activate when this mapping matches. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Command")
	FGameplayTag AbilityTag;

	/** Higher priority mappings are checked first. Most specific mapping should have highest priority. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Command")
	int32 Priority = 0;
};

/**
 * Reason an ability is currently unavailable for activation. Mirrors the gate
 * ordering in ProcessCommands::ActivateAbility so UI can surface the specific
 * blocker to the player.
 */
UENUM(BlueprintType)
enum class ESeinAbilityUnavailableReason : uint8
{
	None,					// Ability is fully available
	UnknownAbility,			// Entity has no ability matching this tag
	OnCooldown,
	BlockedByTag,			// Entity carries one of BlockedTags (or missing RequiredEntityTags / RequiredPlayerTags)
	OutOfRange,
	InvalidTarget,			// Target fails ValidTargetTags query
	NoLineOfSight,
	CanActivateFailed,		// USeinAbility::CanActivate returned false
	Unaffordable,			// FSeinResourceCost not covered by player balances
	PathUnreachable,		// Nav abstract graph has no path from source to goal
	GoalUnwalkable,			// Target cell is blocked / off-map for the agent
	FootprintBlocked		// Building footprint overlaps blocked cells (placement gate)
};

/**
 * Aggregate availability snapshot for a single ability on a single entity,
 * returned by USeinAbilityBPFL::SeinGetAbilityAvailability for UI binding.
 * Matches the shape of FSeinProductionAvailability for uniform UI handling.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinAbilityAvailability
{
	GENERATED_BODY()

	/** The ability tag this availability snapshot describes. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability")
	FGameplayTag AbilityTag;

	/** True if the ability can be activated right now. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability")
	bool bAvailable = false;

	/** Specific reason for unavailability (only meaningful when bAvailable is false). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability")
	ESeinAbilityUnavailableReason Reason = ESeinAbilityUnavailableReason::None;

	/** Time remaining on cooldown in sim-seconds (Zero if ready). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability")
	FFixedPoint CooldownRemaining = FFixedPoint::Zero;

	/** True if the owner can afford the ability's policy-resolved activation cost. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability")
	bool bCanAfford = false;

	/** True if the ability is currently active on the entity. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability")
	bool bIsActive = false;
};

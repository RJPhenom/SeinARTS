/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinAbilityTypes.h
 * @author       RJ Macklem
 * @created      02 Jun 2026
 * @latest       14 Aug 2026
 * @brief        Defines Blueprint-facing ability targeting, cooldown, and availability types.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

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
	/** Activates without capturing a target. */
	None,
	/** Uses the ability owner as its target. */
	Self,
	/** Captures another simulation entity. */
	Entity,
	/** Captures one fixed-point world location. */
	Point,
	/** Captures a fixed-point world location and uses the ability's Area Radius. */
	Area,
	/** Describes a passive target shape. bIsPassive controls auto-activation. */
	Passive
};

/**
 * When the ability's cooldown begins ticking.
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
 * What happens when an ability command targets something outside its Max Range.
 */
UENUM(BlueprintType)
enum class ESeinOutOfRangeBehavior : uint8
{
	/** Ability fails if out of range (grenade, snipe). */
	Reject,
	/** Queues the entity's ability marked Is Move Ability before retrying this
	 *  command. The click-time preflight checks affordability without deducting;
	 *  the follow-up reruns the ordinary gate and pays once if it can activate. */
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
	 * Lives on FSeinAbilityPayload::DefaultCommands. When the player right-clicks,
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
	/** The queried gates passed. */
	None,
	/** The entity has no ability matching the requested tag. */
	UnknownAbility,
	/** The ability's cooldown is still running. */
	OnCooldown,
	/** The entity has a blocked tag or lacks required entity/player tags. */
	BlockedByTag,
	/** The supplied target is outside Max Range and cannot auto-move. */
	OutOfRange,
	/** The supplied entity fails the Valid Target Tags query. */
	InvalidTarget,
	/** The authoritative visibility resolver cannot see the supplied target. */
	NoLineOfSight,
	/** The ability's Can Activate override returned false. */
	CanActivateFailed,
	/** The resolved resource payer cannot cover the activation cost. */
	Unaffordable,
	/** The navigation resolver found no route from source to goal. */
	PathUnreachable,
	/** The target cell is blocked or outside the navigable map for this agent. */
	GoalUnwalkable,
	/** The authoritative placement footprint overlaps blocked cells. */
	FootprintBlocked
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

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinAbility.h
 * @author       RJ Macklem
 * @created      02 Jun 2026
 * @latest       14 Aug 2026
 * @brief        Defines deterministic gameplay abilities and their authoring contract.
 *
 *               Ability instances are canonical simulation state. Player and
 *               scripted activation normally enters through the command queue;
 *               admitted latent actions provide checkpoint-safe continuation.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "GameplayTagContainer.h"
#include "Abilities/SeinAbilityTypes.h"
#include "Abilities/SeinTargeterTypes.h"
#include "Data/SeinResourceTypes.h"
#include "Types/Transform.h"
#include "SeinAbility.generated.h"

class ASeinActor;
class UTexture2D;
class USeinWorldSubsystem;
class USeinTargeterSpec;

// ============================================================================
// Dispatch policy — broker-scope ability dispatch
// ============================================================================
//
// When a player triggers an ability from a multi-member context (a squad with
// 5 members, a selection of 6 infantry, etc.), the broker's resolver looks
// up the ability's dispatch policy to decide WHO fires it. Policy lives on
// the ability itself, not on the squad — same authoring location regardless
// of whether the broker is a squad or an ephemeral selection group.
//
// Defaults to All, which matches the historical "every capable member fires"
// behavior: Move / Attack / Hold / Retreat all want this and need no
// authoring change. Designers override per-ability for special cases:
//   - "Sergeant throws Grenade"      → Mode: Single, Selector: ByTag, PreferredTag: Unit.Role.Officer
//   - "MG suppresses"                → Mode: Single, Selector: ByTag, PreferredTag: Unit.Role.MachineGunner, Fallback: Fail
//   - "All riflemen suppress"        → Mode: ByTag, PreferredTag: Unit.Role.Rifleman
//
// Tag matching walks the candidate's entity tags (BaseTags + dynamic tags)
// via USeinWorldSubsystem::HasTag. Squad-slot tags are NOT consulted —
// authors tag the unit itself, which works uniformly in squads and
// non-squad selections.

UENUM(BlueprintType)
enum class ESeinAbilityDispatchMode : uint8
{
	/** Every broker member that holds an instance of this ability fires it.
	 *  Default — matches historic fan-out behavior. Use for movement, attack,
	 *  per-member actions everyone should perform. */
	All,

	/** Exactly one broker member fires; selector picks who. Use for
	 *  "the leader does it" / "the specialist does it" patterns. */
	Single,

	/** All broker members whose entity tags contain `DispatchPreferredTag`
	 *  fire it. Use for role-scoped fan-outs ("all riflemen suppress",
	 *  "all engineers repair"). Empty result silently no-ops. */
	ByTag,
};

UENUM(BlueprintType)
enum class ESeinAbilityDispatchSelector : uint8
{
	/** Squad broker: the squad's current Leader handle. Selection broker
	 *  (non-squad): the first member in broker.Members order. Falls
	 *  through to `DispatchFallback` when the leader doesn't hold the
	 *  ability. */
	Leader,

	/** First member (in dispatch order) whose entity tags contain
	 *  `DispatchPreferredTag`. Squad broker iterates slot-declaration
	 *  order; selection broker iterates broker.Members order. */
	ByTag,

	/** First member in dispatch order that holds the ability. Useful as
	 *  the simplest "any capable member" semantic. */
	FirstAvailable,
};

UENUM(BlueprintType)
enum class ESeinAbilityDispatchFallback : uint8
{
	/** Try Leader, then FirstAvailable. Sensible default — covers
	 *  "the specialist died but the next-in-line steps up." */
	LeaderFirst,

	/** First member in dispatch order that holds the ability. Skips the
	 *  leader-priority step. */
	FirstAvailable,

	/** Don't dispatch. Ability is silently unavailable this frame —
	 *  UI greys out, resolver emits no dispatch entries. Use for
	 *  hard-gated abilities ("no MG = no suppress, period"). */
	Fail,
};

/** Defines when an ability's authored ResourceCost becomes committed state. */
UENUM(BlueprintType)
enum class ESeinAbilityCostTiming : uint8
{
	/** Charge the full cost when activation commits. This is the safe default
	 *  for ordinary gameplay abilities and ignores production catalog timing. */
	Immediate,

	/** Split the cost by each resource's production timing. AtEnqueue entries
	 *  are charged on activation; AtCompletion entries transfer through
	 *  EnqueueProduction and are charged when the queue item completes. */
	ProductionQueue UMETA(DisplayName = "Production Queue"),
};

/**
 * Base class for all abilities in the SeinARTS framework.
 * Designers subclass this in Blueprint to define ability behavior.
 *
 * Abilities are deterministic and tick on the simulation side.
 * They support cooperative suspension via latent actions (no threads).
 */
UCLASS(Blueprintable, Abstract, meta = (SeinDeterministic))
class SEINARTSCOREENTITY_API USeinAbility : public UObject
{
	GENERATED_BODY()

public:
	// ─── Identity ───

	/** Player-facing name used by action buttons and other UI. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	FText AbilityName;

	/** Stable gameplay tag used to grant, resolve, dispatch, and activate this
	 *  ability. The SeinARTS Ability factory generates a unique tag from the
	 *  asset name; keep it valid and unique if you take manual ownership. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	FGameplayTag AbilityTag;

	/** Provenance flag for the auto-tag-generation system.
	 *    true  = AbilityTag was stamped by the framework (factory creation or
	 *            an explicit "Reset to Auto" action). The system maintains it:
	 *            renaming the asset regenerates the tag; the settings-level
	 *            Regenerate Auto-Generated Tags button re-stamps it.
	 *    false = designer manually edited AbilityTag. The system leaves it
	 *            alone on rename, and the non-destructive Regenerate button
	 *            skips it. Only Force Regenerate All overrides this.
	 *  Flips automatically: PostEditChangeProperty on AbilityTag → false;
	 *  factory + Reset-to-Auto → true. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability",
		AdvancedDisplay)
	bool bAutoGeneratedTag = false;

	/** Icon shown on action-bar buttons / queue slots / radial menus that
	 *  surface this ability. Optional — UI fallbacks render the AbilityName
	 *  text when null. Designers ship a 64x64 (or whatever your HUD calls
	 *  for) Texture2D per ability. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	TObjectPtr<UTexture2D> Icon = nullptr;

	/** Target input this ability expects. This drives action-slot target capture;
	 *  bIsPassive, not the Passive enum value alone, controls auto-activation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	ESeinAbilityTargetType TargetType = ESeinAbilityTargetType::None;

	/** Runs this ability automatically when its first grant is committed. Passive
	 *  abilities occupy the passive set instead of the single primary slot and
	 *  remain active until revoked or explicitly ended. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	bool bIsPassive = false;

	/** Tags this ability as the "move-equivalent" for its owning entity. The
	 *  framework's auto-move-then-X plumbing (production-spawn auto-move,
	 *  range-then-attack pre-pending, etc.) looks for an ability with this
	 *  flag set on the activating entity and uses its AbilityTag as the
	 *  dispatch key — replaces the previous hardcoded `Ability_Move` tag
	 *  reference in framework C++. Designers set this on whatever ability
	 *  represents "move" for the entity (typically SA_Move). If multiple
	 *  abilities on an entity have it set, the first instance wins
	 *  (deterministic insertion order). Zero entries on an entity opts the
	 *  entity out of auto-move plumbing — production completion does not
	 *  auto-issue a move, range gates fail with OutOfRange instead of
	 *  auto-prefixing a move. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	bool bIsMoveAbility = false;

	// ─── Cost + cooldown ───

	/** Resources committed when activation succeeds. Cost Timing decides whether
	 *  production-catalog completion amounts are deferred to the queue item. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "SeinARTS|Ability|Cost",
		meta = (SeinPoolStateIgnore))
	FSeinResourceCost ResourceCost;

	/** Payment timing for ResourceCost. Keep Immediate for ordinary abilities.
	 *  Use Production Queue only when OnActivate transfers its funding snapshot
	 *  through EnqueueProduction. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Cost")
	ESeinAbilityCostTiming CostTiming = ESeinAbilityCostTiming::Immediate;

	/** Refunds the exact activation-cost snapshot when the ability is cancelled.
	 *  Leave enabled when cancellation should undo the committed spend; disable
	 *  it when cancellation intentionally consumes the cost. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Cost")
	bool bRefundCostOnCancel = true;

	/** Cooldown duration in sim-seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Cost")
	FFixedPoint Cooldown;

	/** Chooses whether the cooldown begins on successful activation or natural end. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Cost")
	ESeinCooldownStartTiming CooldownStartTiming = ESeinCooldownStartTiming::OnActivate;

	/** Whether the cooldown applies to the activating member only or to the whole
	 *  squad (when the activating entity is a squad member). Default Squad — matches
	 *  a single squad-wide throw cooldown. Flip to Member for stackable per-soldier
	 *  abilities. No effect on lone (non-squad-member) entities. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Cost")
	ESeinCooldownScope CooldownScope = ESeinCooldownScope::Squad;

	/** Clears a cooldown started by this activation when the ability is cancelled.
	 *  Leave disabled when a mid-use cancel should retain its cooldown; enable it
	 *  for a free pre-commit cancel, commonly with cooldown start set to On End. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Cost")
	bool bRefundCooldownOnCancel = false;

	// ─── Dispatch policy (broker-scope; see ESeinAbilityDispatchMode docblock) ───

	/** How this ability fans out across broker members when triggered from a
	 *  multi-member context. Default `All`: every member that holds an
	 *  instance fires it. See the ESeinAbilityDispatchMode docblock for the
	 *  full pattern catalog. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Dispatch")
	ESeinAbilityDispatchMode DispatchMode = ESeinAbilityDispatchMode::All;

	/** When `DispatchMode == Single`, picks which member fires. Ignored for
	 *  All / ByTag. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Dispatch",
		meta = (EditCondition = "DispatchMode == ESeinAbilityDispatchMode::Single", EditConditionHides))
	ESeinAbilityDispatchSelector DispatchSelector = ESeinAbilityDispatchSelector::Leader;

	/** Tag the chosen member's entity tags must contain. Consulted when
	 *  `DispatchSelector == ByTag` (Single) or `DispatchMode == ByTag`.
	 *  Tag-container `.HasTag` match — exact tag, no hierarchy descent. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Dispatch",
		meta = (EditCondition = "(DispatchMode == ESeinAbilityDispatchMode::Single && DispatchSelector == ESeinAbilityDispatchSelector::ByTag) || DispatchMode == ESeinAbilityDispatchMode::ByTag", EditConditionHides))
	FGameplayTag DispatchPreferredTag;

	/** What to do when `DispatchMode == Single` AND the selector produced no
	 *  candidate. Ignored for All / ByTag (those have natural empty-set
	 *  semantics: silently no-op). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Dispatch",
		meta = (EditCondition = "DispatchMode == ESeinAbilityDispatchMode::Single", EditConditionHides))
	ESeinAbilityDispatchFallback DispatchFallback = ESeinAbilityDispatchFallback::LeaderFirst;

	// ─── Target validation ───

	/** Maximum activation distance from owner to target (location or entity). Zero = unlimited. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Targeting")
	FFixedPoint MaxRange = FFixedPoint::Zero;

	/** Tag query the target entity must satisfy. Empty query = any target allowed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Targeting")
	FGameplayTagQuery ValidTargetTags;

	/** Require line-of-sight from owner to target. Gated on Fog-of-War visibility:
	 *  when a fog impl is active, activation fails (NoLineOfSight) unless the
	 *  target is visible to the owner's player. Permissive when no fog is bound
	 *  (tests / fog-less games). Wired via USeinWorldSubsystem::LineOfSightResolver,
	 *  bound by USeinFogOfWarSubsystem. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Targeting")
	bool bRequiresLineOfSight = false;

	/** Require a nav-reachable target. When true, ProcessCommands::ActivateAbility
	 *  invokes USeinWorldSubsystem's registered pathable-target resolver (provided
	 *  by USeinNavigationSubsystem) before activation; on failure, the command is
	 *  rejected with SeinARTS.Command.Reject.PathUnreachable. Pre-reject skipped
	 *  if no resolver is registered (keeps tests + nav-less games working). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Targeting")
	bool bRequiresPathableTarget = false;

	/** Require an unobstructed footprint at the targeter's captured location.
	 *  When true, ProcessCommands invokes USeinWorldSubsystem's registered
	 *  FootprintPlacementResolver (provided by USeinNavigationSubsystem) before
	 *  activation; on failure, rejected with SeinARTS.Command.Reject.FootprintBlocked.
	 *
	 *  The footprint shape is read at activation time from the ability's
	 *  TargeterSpec — currently only USeinPointFacingTargeterSpec carries a
	 *  BuildingClass whose CDO's USeinExtentsComponent provides the shape.
	 *  Abilities with no compatible spec or no extents on the target class
	 *  silently bypass this gate (no-op, not a hard reject). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Targeting")
	bool bRequiresFreeFootprint = false;

	/** Chooses whether an out-of-range command is rejected or deterministically
	 *  queues the entity's move ability before retrying this ability. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Targeting")
	ESeinOutOfRangeBehavior OutOfRangeBehavior = ESeinOutOfRangeBehavior::Reject;

	/** AoE radius in world units. Drives the default Point-targeter spec's preview
	 *  ring when non-zero, and is read by ability OnActivate logic for AoE casts.
	 *  Zero = single-target / no ring preview. Kept on the ability (not on the
	 *  spec) so it stays accessible to non-targeter activation paths and to
	 *  ability validation that doesn't care which spec is attached. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Targeting")
	FFixedPoint AreaRadius = FFixedPoint::Zero;

	/** Optional targeter specification — declarative data describing how the
	 *  targeter UI captures target points for this ability. Only consulted when
	 *  the ability is invoked from the action-slot trigger flow (USeinTargeterBPFL::
	 *  TriggerAbilityFromActionSlot); right-click smart commands ignore it (they
	 *  already have the click point).
	 *
	 *  Null = ability is not action-slot-targetable. Point and Area abilities
	 *  triggered from an action slot require an explicit Point Targeter Spec or
	 *  Point + Facing Targeter Spec. Line and corridor targeters are not
	 *  currently shipped. */
	UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly,
		Category = "SeinARTS|Ability|Targeting",
		meta = (SeinPoolStateIgnore))
	TObjectPtr<USeinTargeterSpec> TargeterSpec;

	// ─── Arbitration ───

	/** Tags this ability grants to the entity while active. Granted on activate,
	 *  ungranted on deactivate. Tag presence is refcounted —
	 *  overlapping grants from BaseTags, other abilities, or effects
	 *  stay present until every source has released the tag.
	 *  Used with CancelAbilitiesWithTag for cross-ability arbitration. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Arbitration")
	FGameplayTagContainer GrantedTags;

	/** Tags the OWNING ENTITY must have to activate. Empty = no entity-tag gate.
	 *  Use for entity-state preconditions: a Heal ability that requires
	 *  `SeinARTS.State.Damaged` on the caster, a Garrison-exit ability that
	 *  requires `SeinARTS.State.InTransport`, etc. Entity tags are the union of
	 *  the entity's BaseTags + GrantedTags from active abilities/effects. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Arbitration")
	FGameplayTagContainer RequiredEntityTags;

	/** Tags the OWNING PLAYER must have to activate. Empty = no player-tag gate.
	 *  This is where tech prerequisites land for production abilities — a
	 *  SA_TrainTank ability lists `SeinARTS.Tech.VehicleAccess` here, and the
	 *  activation gate rejects with `SeinARTS.Command.Reject.BlockedByTag` if
	 *  the owning player hasn't unlocked it.
	 *
	 *  Distinct from RequiredEntityTags which gates on entity-level tags. Player
	 *  tags are source-aware, refcounted canonical player state. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Arbitration")
	FGameplayTagContainer RequiredPlayerTags;

	/** If the entity has ANY of these tags, this ability refuses to activate.
	 *  Combine with GrantedTags on the same ability to self-block (e.g., a grenade
	 *  that grants Ability.State.Channeling and blocks on the same tag cannot
	 *  reissue while a throw is in progress). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Arbitration")
	FGameplayTagContainer BlockedTags;

	/** On activate, cancel any currently-active ability (including this one) whose
	 *  GrantedTags intersect this set. Including this ability's own GrantedTag
	 *  here gives you self-cancelling reissue (e.g., repeat-right-click Move). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Arbitration")
	FGameplayTagContainer CancelAbilitiesWithTag;

	// ─── Runtime State (set by system) ───

	/** Entity that owns and executes this runtime ability instance. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	FSeinEntityHandle OwnerEntity;

	/** Entity supplied by the current activation. Invalid for location-only casts. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	FSeinEntityHandle TargetEntity;

	/** Fixed-point world location supplied by the current activation. For an
	 *  entity target this is the command's captured location, not a live pointer. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	FFixedVector TargetLocation;

	/** Captured targeter points when the ability was activated via the targeter
	 *  subsystem (action-slot trigger flow). Empty when the ability was activated
	 *  from a right-click smart command — TargetLocation alone is sufficient and
	 *  is always populated regardless of trigger path.
	 *
	 *  For multi-target abilities (Spec->TargetCount > 1), this array contains
	 *  one entry per captured point. Index 0 always mirrors TargetLocation for
	 *  single-point convenience. OnActivate iterates this array for multi-cast
	 *  semantics (e.g., spawn 3 grenade projectiles, register 3 patrol waypoints).
	 *
	 *  Cleared when the ability deactivates so a re-activation via right-click
	 *  doesn't see stale points from a prior targeter cast. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	TArray<FSeinTargeterPoint> TargeterPoints;

	/** Remaining cooldown in simulation seconds. Zero means ready. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	FFixedPoint CooldownRemaining;

	/** True while this instance owns an active primary or passive lifecycle. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	bool bIsActive = false;

	/**
	 * World-global deterministic identity of this instance's current/most recent
	 * activation. Zero means it has never activated in this timeline.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	int64 AbilityActivationID = 0;

	int64 GetActivationID() const { return AbilityActivationID; }

	/** Exact tag references acquired by the current activation. This is kept
	 *  separate from the authored GrantedTags so a failed saturated grant can
	 *  never make teardown consume another system's reference. */
	UPROPERTY()
	FGameplayTagContainer CommittedGrantedTags;

	/** Snapshot of the cost actually deducted on activation. Used for cancellation
	 *  refunds without re-resolving authored policy. For Production Queue abilities,
	 *  this contains only the catalog's AtEnqueue bucket. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime",
		meta = (SeinPoolStateIgnore))
	FSeinResourceCost DeductedCost;

	/** Principal whose resources funded this activation. Snapshotted so later
	 *  cancellation and production completion never re-resolve a changed policy. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	FSeinPlayerID ResourcePayer;

	/** Deferred catalog bucket for a Production Queue activation. EnqueueProduction
	 *  transfers it to the queue entry; Immediate abilities always leave it empty. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime",
		meta = (SeinPoolStateIgnore))
	FSeinResourceCost PendingCompletionCost;

	/** Whether the cooldown has been started for the current activation. Drives
	 *  the bRefundCooldownOnCancel + CooldownStartTiming == OnEnd interaction. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	bool bCooldownStarted = false;

	/** World subsystem reference (set on initialization) */
	UPROPERTY(Transient)
	TObjectPtr<USeinWorldSubsystem> WorldSubsystem;

	/** Route GetWorld() through the cached WorldSubsystem so BP graphs in
	 *  ability subclasses can call WorldContext-tagged BPFLs (Spawn Entity,
	 *  Get Entity Owner, etc.) without manually wiring the World Context Object
	 *  pin — UE's BP node hides the pin when self->GetWorld() resolves. */
	virtual UWorld* GetWorld() const override;

#if WITH_EDITOR
	/** Watches manual edits to AbilityTag in the BP details panel and flips
	 *  `bAutoGeneratedTag = false` so the auto-tag-generation system knows
	 *  the designer is now the owner of this tag. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Surfaces `AbilityTag` onto this asset's FAssetData (key
	 *  `SeinAssetTagKeys::AbilityTag`, bare `ToString()` form) so editor tooling
	 *  — the auto-tag collision check in particular — can read it WITHOUT loading
	 *  the Blueprint + CDO. Additive: chains Super first, never replaces engine
	 *  tags. Harvested at save / FAssetData construction; the registry value is
	 *  therefore the last-saved tag (callers that need in-memory edits read the
	 *  loaded CDO directly). */
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;

	// ─── Lifecycle (Blueprint implementable) ───

	/** Adds a final deterministic activation rule. Return false to reject the
	 *  command; the default returns true.
	 *
	 *  Runs after cooldown, tag, target, line-of-sight, and pathability checks,
	 *  but before resource deduction, cancellation arbitration, and On Activate. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Ability")
	bool CanActivate();
	virtual bool CanActivate_Implementation() { return true; }

	/** Starts the committed ability. Initialize deterministic state and launch
	 *  admitted actions here; the default does nothing.
	 *
	 *  This event does not complete the ability. Call End Ability on natural
	 *  completion or Cancel Ability for forced termination. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Ability")
	void OnActivate();
	virtual void OnActivate_Implementation() {}

	/** Advances the ability once per fixed simulation tick. Delta Time is in
	 *  simulation seconds; the default does nothing. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Ability")
	void OnTick(FFixedPoint DeltaTime);
	virtual void OnTick_Implementation(FFixedPoint DeltaTime) {}

	/** Performs final cleanup after the active index, owned tags, funding policy,
	 *  and latent actions have been finalized. The default does nothing.
	 *
	 *  bWasCancelled is true for forced termination and false after End Ability. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Ability")
	void OnEnd(bool bWasCancelled);
	virtual void OnEnd_Implementation(bool bWasCancelled) {}

	// ─── Control (callable from BP ability scripts) ───

	/** Ends this ability normally and calls On End with bWasCancelled false. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability",
		meta = (SeinContinuationSafe))
	void EndAbility();

	/** Forces this ability to terminate and calls On End with bWasCancelled true. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability",
		meta = (SeinContinuationSafe))
	void CancelAbility();

	/** Explicit write barrier for custom Blueprint code that mutates this
	 *  ability from outside its own activation/tick/end callback. Ordinary
	 *  self-state changes are tracked automatically by the framework. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability|State",
		meta = (DisplayName = "Mark Deterministic State Dirty",
			SeinContinuationSafe))
	void MarkDeterministicStateDirty();

	// ─── Internal ───

	void InitializeAbility(FSeinEntityHandle Owner, USeinWorldSubsystem* Subsystem);

	/** Activate with target entity + location. TargetLocation runtime field is
	 *  populated; TargeterPoints is left empty (right-click / direct-activation path). */
	bool ActivateAbility(FSeinEntityHandle Target, FFixedVector Location);

	/** Activate with targeter-captured points. TargetLocation is set to Points[0].Location
	 *  for single-target convenience; TargeterPoints is populated with the full array.
	 *  When Points is empty this degrades to the basic ActivateAbility(Target, Location). */
	bool ActivateAbilityWithTargeterPoints(FSeinEntityHandle Target, FFixedVector Location,
		const TArray<FSeinTargeterPoint>& Points);

	/** Preflight for command processing. Activation still performs a transactional
	 *  acquisition so direct/native callers receive the same safety guarantee. */
	bool CanCommitGrantedTags() const;

	void TickAbility(FFixedPoint DeltaTime);
	void DeactivateAbility(bool bCancelled);
	void TickCooldown(FFixedPoint DeltaTime);
	bool IsOnCooldown() const;

	/** Stamp the cost snapshot on activation. Called by ProcessCommands after
	 *  a successful USeinResourceBPFL::SeinDeduct. */
	void RecordDeductedCost(const FSeinResourceCost& Cost)
	{
		DeductedCost = Cost;
		MarkDeterministicStateDirty();
	}
	void RecordResourcePayer(FSeinPlayerID Payer)
	{
		ResourcePayer = Payer;
		MarkDeterministicStateDirty();
	}

	/** Resolve the exact cost charged by activation and any amount transferred to
	 *  production completion. Shared by command preflight, commit, and UI reads. */
	void ResolveActivationCosts(const UObject* WorldContextObject,
		FSeinResourceCost& OutActivationCost,
		FSeinResourceCost& OutProductionCompletionCost) const;

	/** Stamp the Production Queue completion bucket on activation. */
	void RecordPendingCompletionCost(const FSeinResourceCost& Cost)
	{
		PendingCompletionCost = Cost;
		MarkDeterministicStateDirty();
	}

	// ─── BP-callable convenience methods (production / rally) ───
	//
	// One-arg surfaces for ability BP graphs. The ability is the trigger; the
	// actual production data (build time, refund policy, research effect) lives
	// on the producible's `FSeinProducibleComponent` and is read at enqueue
	// time. The ability supplies cost (its own ResourceCost) and the producer
	// (its own OwnerEntity).

	/** Append a queue entry on the ability's owner for `ProducibleClass`. Reads
	 *  build time, refund policy, and research effect from the class's
	 *  `FSeinProducibleComponent`. The activation's deducted cost becomes the
	 *  queue's refundable AtEnqueue principal; a Production Queue ability also
	 *  transfers its deferred bucket. No-op (with warning) if owner has no
	 *  FSeinProductionComponent, queue
	 *  is full, or the producible has no FSeinProducibleComponent.
	 *
	 *  BP usage: OnActivate → Self.EnqueueProduction(SU_Rifleman) → End Ability. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability|Production",
		meta = (DisplayName = "Enqueue Production"))
	void EnqueueProduction(TSubclassOf<ASeinActor> ProducibleClass);

	/** Set the ability owner's rally point to a world transform. Produced units
	 *  rally to `Transform.GetLocation()` facing `Transform.GetRotation()`.
	 *  No-op if owner has no FSeinProductionComponent component.
	 *
	 *  BP usage: OnActivate → Self.SetRallyPoint(MakeFixedTransform(Self.TargetLocation)). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability|Production",
		meta = (DisplayName = "Set Rally Point"))
	void SetRallyPoint(const FFixedTransform& Transform);

	/** Set the ability owner's rally target to chase an entity. Produced units
	 *  path to the entity's current transform at dispatch time. No-op if owner
	 *  has no FSeinProductionComponent component. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability|Production",
		meta = (DisplayName = "Set Rally Entity"))
	void SetRallyEntity(FSeinEntityHandle RallyEntity);

	/** Clear the ability owner's rally target. Produced units stay at the
	 *  deploy offset beside the owner until given a manual order. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability|Production",
		meta = (DisplayName = "Clear Rally Point"))
	void ClearRallyPoint();

	/** Derived O(1) pool locator; validated against the world's slot on use. */
	int32 GetRuntimePoolID() const { return RuntimePoolID; }

private:
	int32 RuntimePoolID = INDEX_NONE;

	bool ActivateAbilityInternal(FSeinEntityHandle Target,
		FFixedVector Location,
		const TArray<FSeinTargeterPoint>* Points);
	bool AcquireGrantedTags();
	void ReleaseCommittedGrantedTags();

	/** Set CooldownRemaining + bCooldownStarted on this instance, and (when
	 *  CooldownScope == Squad and the owner is a squad member) propagate the
	 *  same cooldown to every squadmate's instance of this ability tag. Called
	 *  by the three cooldown-start sites in ActivateAbility / DeactivateAbility. */
	void StartCooldownInternal();

	friend class USeinWorldSubsystem;
};

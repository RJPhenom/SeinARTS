#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Core/SeinEntityHandle.h"
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

/**
 * Base class for all abilities in the SeinARTS framework.
 * Designers subclass this in Blueprint to define ability behavior.
 *
 * Abilities are deterministic and tick on the simulation side.
 * They support cooperative suspension via latent actions (no threads).
 */
UCLASS(Blueprintable, Abstract)
class SEINARTSCOREENTITY_API USeinAbility : public UObject
{
	GENERATED_BODY()

public:
	// ─── Identity ───

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	FText AbilityName;

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	ESeinAbilityTargetType TargetType = ESeinAbilityTargetType::None;

	/** Passive abilities tick continuously without explicit activation. */
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

	/** Resource cost to activate (unified cost struct per DESIGN §6). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Cost")
	FSeinResourceCost ResourceCost;

	/** Refund the deducted cost when the ability is cancelled. Default true — matches
	 *  typical RTS economy where cancelling "didn't happen" from the ledger's view.
	 *  Set false for punitive-cancel abilities. DESIGN §7 Q2a. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Cost")
	bool bRefundCostOnCancel = true;

	/** Cooldown duration in sim-seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Cost")
	FFixedPoint Cooldown;

	/** When the cooldown begins. DESIGN §7 Q4c. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Cost")
	ESeinCooldownStartTiming CooldownStartTiming = ESeinCooldownStartTiming::OnActivate;

	/** Whether the cooldown applies to the activating member only or to the whole
	 *  squad (when the activating entity is a squad member). Default Squad — matches
	 *  CoH "one squad-wide throw cooldown." Flip to Member for stackable per-soldier
	 *  abilities. No effect on lone (non-squad-member) entities. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Cost")
	ESeinCooldownScope CooldownScope = ESeinCooldownScope::Squad;

	/** Reset the cooldown when the ability is cancelled. Default false — "you used it
	 *  recently" still applies to mid-use cancels. Designers opt in to true for
	 *  abilities where pre-commit cancel should be free (usually paired with
	 *  CooldownStartTiming == OnEnd). DESIGN §7 Q3c. */
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

	// ─── Target validation (declarative — DESIGN §7 Q5c/Q6) ───

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

	/** What to do when activation is attempted with a target outside MaxRange. */
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
	 *  Null = ability is not action-slot-targetable, or designer prefers the
	 *  default spec inferred from TargetType (Point → simple click, Area →
	 *  click + AreaRadius preview). Set explicitly to a USeinPointTargeterSpec /
	 *  USeinPointFacingTargeterSpec / USeinLineTargeterSpec subclass to capture
	 *  drag data, footprint placement, line corridor, etc.
	 *
	 *  Phase 1: only USeinPointTargeterSpec is implemented. */
	UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category = "SeinARTS|Ability|Targeting")
	TObjectPtr<USeinTargeterSpec> TargeterSpec;

	// ─── Arbitration (DESIGN §3 + §7) ───

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
	 *  tags are refcounted via USeinWorldSubsystem::GrantPlayerTag (DESIGN §10
	 *  unified tech). */
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

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	FSeinEntityHandle OwnerEntity;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	FSeinEntityHandle TargetEntity;

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

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	FFixedPoint CooldownRemaining;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	bool bIsActive = false;

	/** Snapshot of the cost actually deducted on activation. Empty unless currently
	 *  active. Used to drive refund-on-cancel without re-resolving cost at deactivate time.
	 *  For production abilities this is only the AtEnqueue portion — the AtCompletion
	 *  portion is held in `PendingCompletionCost` and never deducted at activation. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	FSeinResourceCost DeductedCost;

	/** AtCompletion-bucket snapshot populated by the activation gate when this is
	 *  a production ability. The activation gate splits ResourceCost via the
	 *  resource catalog: AtEnqueue resources are deducted immediately (recorded
	 *  in DeductedCost), AtCompletion resources are held here for the
	 *  USeinProductionBPFL::SeinEnqueueProduction call to seed the queue entry's
	 *  AtCompletionCost. The production system deducts it at spawn time.
	 *
	 *  Empty unless this is a production ability with at least one AtCompletion-
	 *  marked resource in the cost map. Designers don't read this directly —
	 *  EnqueueProduction handles the wiring. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
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

	/** Override to add custom activation checks beyond the declarative target
	 *  validation (range / ValidTargetTags / LOS). Run after declarative checks. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Ability")
	bool CanActivate();
	virtual bool CanActivate_Implementation() { return true; }

	/** Called when the ability activates. Set up initial state here. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Ability")
	void OnActivate();
	virtual void OnActivate_Implementation() {}

	/** Called every sim tick while the ability is active */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Ability")
	void OnTick(FFixedPoint DeltaTime);
	virtual void OnTick_Implementation(FFixedPoint DeltaTime) {}

	/** Called when the ability ends, either naturally or via cancellation */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Ability")
	void OnEnd(bool bWasCancelled);
	virtual void OnEnd_Implementation(bool bWasCancelled) {}

	// ─── Control (callable from BP ability scripts) ───

	/** End this ability normally */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability")
	void EndAbility();

	/** Cancel this ability (forced termination) */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability")
	void CancelAbility();

	// ─── Internal ───

	void InitializeAbility(FSeinEntityHandle Owner, USeinWorldSubsystem* Subsystem);

	/** Activate with target entity + location. TargetLocation runtime field is
	 *  populated; TargeterPoints is left empty (right-click / direct-activation path). */
	void ActivateAbility(FSeinEntityHandle Target, FFixedVector Location);

	/** Activate with targeter-captured points. TargetLocation is set to Points[0].Location
	 *  for single-target convenience; TargeterPoints is populated with the full array.
	 *  When Points is empty this degrades to the basic ActivateAbility(Target, Location). */
	void ActivateAbilityWithTargeterPoints(FSeinEntityHandle Target, FFixedVector Location,
		const TArray<FSeinTargeterPoint>& Points);

	void TickAbility(FFixedPoint DeltaTime);
	void DeactivateAbility(bool bCancelled);
	void TickCooldown(FFixedPoint DeltaTime);
	bool IsOnCooldown() const;

	/** Stamp the cost snapshot on activation. Called by ProcessCommands after
	 *  a successful USeinResourceBPFL::SeinDeduct. */
	void RecordDeductedCost(const FSeinResourceCost& Cost) { DeductedCost = Cost; }

	/** Stamp the AtCompletion-bucket snapshot on activation. Called by
	 *  ProcessCommands after the catalog-aware split. Empty for abilities
	 *  whose ResourceCost contains no AtCompletion-marked resources (the
	 *  typical non-production ability case). Production abilities consume
	 *  this in `EnqueueProduction` to seed the queue entry's AtCompletionCost. */
	void RecordPendingCompletionCost(const FSeinResourceCost& Cost) { PendingCompletionCost = Cost; }

	// ─── BP-callable convenience methods (production / rally) ───
	//
	// One-arg surfaces for ability BP graphs. The ability is the trigger; the
	// actual production data (build time, refund policy, research effect) lives
	// on the producible's `FSeinProducibleComponent` and is read at enqueue
	// time. The ability supplies cost (its own ResourceCost) and the producer
	// (its own OwnerEntity).

	/** Append a queue entry on the ability's owner for `ProducibleClass`. Reads
	 *  build time, refund policy, and research effect from the class's
	 *  `FSeinProducibleComponent`. Cost is taken from this ability's `ResourceCost`
	 *  (the activation gate has already deducted the AtEnqueue portion;
	 *  AtCompletion sits in `PendingCompletionCost` and is handed to the queue
	 *  entry). No-op (with warning) if owner has no FSeinProductionComponent, queue
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

private:
	/** Set CooldownRemaining + bCooldownStarted on this instance, and (when
	 *  CooldownScope == Squad and the owner is a squad member) propagate the
	 *  same cooldown to every squadmate's instance of this ability tag. Called
	 *  by the three cooldown-start sites in ActivateAbility / DeactivateAbility. */
	void StartCooldownInternal();
};

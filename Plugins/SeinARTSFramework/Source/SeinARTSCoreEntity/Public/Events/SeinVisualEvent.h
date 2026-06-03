/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisualEvent.h
 * @brief   One-way visual events from simulation to render layer.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "SeinVisualEvent.generated.h"

/**
 * Types of visual events the simulation can emit.
 * The render layer uses these to trigger VFX, audio, UI updates, etc.
 */
UENUM(BlueprintType)
enum class ESeinVisualEventType : uint8
{
	EntitySpawned,
	EntityDestroyed,
	DamageApplied,
	HealApplied,
	Death,
	Kill,
	AbilityActivated,
	AbilityEnded,
	EffectApplied,
	EffectRemoved,
	ProductionStarted,
	ProductionCompleted,
	ProductionStalled,
	/** Entity entered or exited under-construction state. `Value` = 1.0 means
	 *  entering construction (placement visual should appear, main mesh hidden);
	 *  `Value` = 0.0 means leaving construction (placement visual destroyed,
	 *  main mesh restored). Routed to the entity's USeinConstructionComponent
	 *  on the bridged actor. */
	ConstructionStateChanged,
	ResourceChanged,
	SquadMemberDied,
	SquadMemberAdded,
	SquadLeaderChanged,
	TechResearched,
	TechRevoked,
	Ping,
	CommandRejected,
	NavLinkChanged,
	EntityEnteredVision,
	EntityExitedVision,
	TerrainMutated,
	EntityEnteredContainer,
	EntityExitedContainer,
	AttachmentSlotFilled,
	AttachmentSlotEmptied,
	PlayPreRenderedCinematic,
	EndCinematic,
	MatchStarting,
	MatchStarted,
	MatchPaused,
	MatchResumed,
	MatchEnding,
	MatchEnded,
	VoteStarted,
	VoteProgress,
	VoteResolved,

	/** Two Overlap-responding colliders began overlapping this tick.
	 *  PrimaryEntity = A, SecondaryEntity = B (canonical: A.Index < B.Index). */
	CollisionOverlapBegin,
	/** Two colliders stopped overlapping (separated, or one was destroyed). */
	CollisionOverlapEnd,
};

/**
 * A single visual event emitted by the simulation for the render layer to consume.
 * These are strictly one-way: simulation -> render. The render layer never writes back.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinVisualEvent
{
	GENERATED_BODY()

	/** The type of visual event */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|VisualEvent")
	ESeinVisualEventType Type = ESeinVisualEventType::EntitySpawned;

	/** Primary entity involved in this event */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|VisualEvent")
	FSeinEntityHandle PrimaryEntity;

	/** Secondary entity involved (e.g. damage source, target) */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|VisualEvent")
	FSeinEntityHandle SecondaryEntity;

	/** Numeric value associated with the event (damage amount, resource delta, etc.) */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|VisualEvent")
	FFixedPoint Value;

	/** Gameplay tag for ability, effect, or entity-identity identification */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|VisualEvent")
	FGameplayTag Tag;

	/** World location relevant to the event */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|VisualEvent")
	FFixedVector Location;

	/** Player who owns or triggered this event */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|VisualEvent")
	FSeinPlayerID PlayerID;

	/**
	 * Optional reason-tag for CommandRejected events — a gameplay tag under
	 * SeinARTS.Command.Reject.* (Unaffordable / OnCooldown / PathUnreachable /
	 * GoalUnwalkable / etc.) letting UI surface "why" alongside "which command".
	 * Empty for non-CommandRejected events.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|VisualEvent")
	FGameplayTag ReasonTag;

	/** Create an entity spawn event */
	static FSeinVisualEvent MakeSpawnEvent(FSeinEntityHandle Entity, FFixedVector Location);

	/** Create an entity destroy event */
	static FSeinVisualEvent MakeDestroyEvent(FSeinEntityHandle Entity);

	/** Create a DamageApplied event (per-hit). `Tag` carries the damage-type tag. */
	static FSeinVisualEvent MakeDamageAppliedEvent(FSeinEntityHandle Target, FSeinEntityHandle Source, FFixedPoint Amount, FGameplayTag DamageType);

	/** Create a HealApplied event (per-hit). `Tag` carries the heal-type tag. */
	static FSeinVisualEvent MakeHealAppliedEvent(FSeinEntityHandle Target, FSeinEntityHandle Source, FFixedPoint Amount, FGameplayTag HealType);

	/** Create a Death event — entity's health crossed zero. Consumed by UI / render
	 *  for death animation routing; the entity is queued for destroy elsewhere. */
	static FSeinVisualEvent MakeDeathEvent(FSeinEntityHandle Dying, FSeinEntityHandle Killer);

	/** Create a Kill event — counterpart to Death, surfaces the killer side of a
	 *  kill for kill-feed / scoreboard UI. */
	static FSeinVisualEvent MakeKillEvent(FSeinEntityHandle Killer, FSeinEntityHandle Killed, FSeinPlayerID KillerPlayer);

	/** Create an ability activation or deactivation event */
	static FSeinVisualEvent MakeAbilityEvent(FSeinEntityHandle Entity, FGameplayTag AbilityTag, bool bActivated);

	/** Create a production started or completed event */
	static FSeinVisualEvent MakeProductionEvent(FSeinEntityHandle Building, FGameplayTag IdentityTag, bool bCompleted);

	/** Create an effect applied or removed event */
	static FSeinVisualEvent MakeEffectEvent(FSeinEntityHandle Target, FGameplayTag EffectTag, bool bApplied);

	/** Create a SquadMemberAdded event. `PrimaryEntity` = squad, `SecondaryEntity`
	 *  = newly-added member, `Tag` = the slot's discriminator tag. */
	static FSeinVisualEvent MakeSquadMemberAddedEvent(FSeinEntityHandle Squad, FSeinEntityHandle Member, FGameplayTag SlotTag);

	/** Create a SquadMemberDied event. `PrimaryEntity` = squad, `SecondaryEntity`
	 *  = dead member, `Tag` = the slot's discriminator tag. */
	static FSeinVisualEvent MakeSquadMemberDiedEvent(FSeinEntityHandle Squad, FSeinEntityHandle Member, FGameplayTag SlotTag);

	/** Create a SquadLeaderChanged event — fires when promotion happens (leader
	 *  death, manual promote). `PrimaryEntity` = squad, `SecondaryEntity` = new
	 *  leader (Invalid if the squad has no remaining members). */
	static FSeinVisualEvent MakeSquadLeaderChangedEvent(FSeinEntityHandle Squad, FSeinEntityHandle NewLeader);

	/** Create a tech researched event (for UI refresh). */
	static FSeinVisualEvent MakeTechResearchedEvent(FSeinPlayerID Player, FGameplayTag TechTag);

	/** Create a tech revoked event (complement to TechResearched). */
	static FSeinVisualEvent MakeTechRevokedEvent(FSeinPlayerID Player, FGameplayTag TechTag);

	/** Create a production-stalled event (front queue entry at 100% waiting on an
	 *  AtCompletion resource — typically pop cap — to free up). */
	static FSeinVisualEvent MakeProductionStalledEvent(FSeinEntityHandle Building, FGameplayTag IdentityTag);

	/** Create a ConstructionStateChanged event. `bUnderConstruction = true` for
	 *  spawn-time entry into construction state; false for completion. The
	 *  bridged actor's USeinConstructionComponent consumes this to drive the
	 *  placement-visual swap (hide main mesh + spawn placement visual on enter,
	 *  reverse on exit). */
	static FSeinVisualEvent MakeConstructionStateChangedEvent(FSeinEntityHandle Entity, bool bUnderConstruction);

	/** Create a ping event at a world location, optionally targeting an entity */
	static FSeinVisualEvent MakePingEvent(FSeinPlayerID Player, FFixedVector Location, FSeinEntityHandle OptionalTarget = FSeinEntityHandle());

	/** Create a CommandRejected event for UI feedback. `CommandTag` goes in
	 *  `Tag`; optional `ReasonTag` (under SeinARTS.Command.Reject.*) goes in
	 *  the dedicated `ReasonTag` field for richer UI feedback. */
	static FSeinVisualEvent MakeCommandRejectedEvent(
		FSeinPlayerID Player,
		FSeinEntityHandle Entity,
		FGameplayTag CommandTag,
		FGameplayTag ReasonTag = FGameplayTag());

	/** Create a NavLinkChanged event — fires when SeinSetNavLinkEnabled toggles
	 *  a link. `Value` encodes the new enabled state as 0 or 1; `Tag` unused. */
	static FSeinVisualEvent MakeNavLinkChangedEvent(int32 NavLinkID, bool bEnabled);

	/** Create a TerrainMutated event — fires when runtime cell mutations occur
	 *  (shelling, footprint register/unregister, nav-tag stamping). `Location` =
	 *  mutation anchor; `Value` = affected-cell count (debug / render hints). */
	static FSeinVisualEvent MakeTerrainMutatedEvent(FFixedVector Location, int32 CellCount);

	/** Create an EntityEnteredContainer event (DESIGN §14). `PrimaryEntity` is
	 *  the container, `SecondaryEntity` the new occupant, `Value` the assigned
	 *  visual-slot index (-1 if tracking off). */
	static FSeinVisualEvent MakeEntityEnteredContainerEvent(FSeinEntityHandle Container, FSeinEntityHandle Occupant, int32 VisualSlotIndex);

	/** Create an EntityExitedContainer event. `Location` = exit world position. */
	static FSeinVisualEvent MakeEntityExitedContainerEvent(FSeinEntityHandle Container, FSeinEntityHandle Occupant, FFixedVector ExitLocation);

	/** Create an AttachmentSlotFilled event. `Tag` carries the slot tag. */
	static FSeinVisualEvent MakeAttachmentSlotFilledEvent(FSeinEntityHandle Container, FSeinEntityHandle Occupant, FGameplayTag SlotTag);

	/** Create an AttachmentSlotEmptied event. `Tag` carries the slot tag. */
	static FSeinVisualEvent MakeAttachmentSlotEmptiedEvent(FSeinEntityHandle Container, FSeinEntityHandle Occupant, FGameplayTag SlotTag);

	/** Create a PlayPreRenderedCinematic event (DESIGN §17). Renderer reads
	 *  `Tag` as the cinematic ID (an FName packed into tag-string form), and
	 *  `Value` encodes `(SkipMode & 0xFF) | (VoteThreshold << 8)` as a compact
	 *  combined parameter carrier. Soft-object-ptr payloads are carried through
	 *  designer-side binding; V1 keeps the event struct fixed-shape. */
	static FSeinVisualEvent MakePlayPreRenderedCinematicEvent(FGameplayTag CinematicIDTag, int32 SkipMode, int32 VoteThreshold);

	/** Create an EndCinematic event. `Tag` = cinematic ID (same mapping as
	 *  PlayPreRenderedCinematic). */
	static FSeinVisualEvent MakeEndCinematicEvent(FGameplayTag CinematicIDTag);

	/** Create a match flow event (DESIGN §18). `Type` supplied by caller.
	 *  Optional `Winner` / `Reason` carried on the primary-entity / tag slots. */
	static FSeinVisualEvent MakeMatchFlowEvent(ESeinVisualEventType Type, FSeinPlayerID Winner = FSeinPlayerID(), FGameplayTag Reason = FGameplayTag());

	/** Create a VoteStarted event (DESIGN §18). `Tag` = VoteType; `PlayerID` = Initiator;
	 *  `Value` = RequiredThreshold (fixed-point-encoded int). */
	static FSeinVisualEvent MakeVoteStartedEvent(FGameplayTag VoteType, FSeinPlayerID Initiator, int32 RequiredThreshold);

	/** Create a VoteProgress event. `Tag` = VoteType; `Value` encodes
	 *  `(YesCount & 0xFFFF) | (NoCount << 16)`. */
	static FSeinVisualEvent MakeVoteProgressEvent(FGameplayTag VoteType, int32 YesCount, int32 NoCount);

	/** Create a VoteResolved event. `Tag` = VoteType; `Value` = 1 if passed, 0 if failed. */
	static FSeinVisualEvent MakeVoteResolvedEvent(FGameplayTag VoteType, bool bPassed);

	/** Create a CollisionOverlapBegin event — two Overlap-responding colliders
	 *  started overlapping. PrimaryEntity = A, SecondaryEntity = B. */
	static FSeinVisualEvent MakeCollisionOverlapBeginEvent(FSeinEntityHandle A, FSeinEntityHandle B);

	/** Create a CollisionOverlapEnd event — counterpart to begin. */
	static FSeinVisualEvent MakeCollisionOverlapEndEvent(FSeinEntityHandle A, FSeinEntityHandle B);
};

/**
 * Queue of visual events. The simulation enqueues events during its tick,
 * and the render layer drains them each frame via Flush().
 */
USTRUCT(meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinVisualEventQueue
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSeinVisualEvent> Events;

	/** Add an event to the queue */
	void Enqueue(const FSeinVisualEvent& Event);

	/** Return all queued events and clear the queue */
	TArray<FSeinVisualEvent> Flush();

	/** Return the number of events currently queued */
	int32 Num() const;
};

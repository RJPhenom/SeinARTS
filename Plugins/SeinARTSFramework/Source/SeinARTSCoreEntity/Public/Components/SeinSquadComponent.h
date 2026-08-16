/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadComponent.h
 * @brief   Squad component for the persistent squad entity that owns
 *          a heterogeneous slot list. Slots are canonical;
 *          the live member list is derived from non-invalid occupants.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "Types/Transform.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "Actor/SeinActor.h"
#include "Components/SeinComponent.h"
#include "Data/SeinResourceTypes.h"
#include "SeinSquadComponent.generated.h"

class USeinCommandBrokerResolver;
class USeinFormation;

/**
 * Whether the squad enters a container as a single entity (one occupant
 * contributing Size = SlotCount to the container's CurrentLoad â€” the squad
 * actor itself enters, members hide) or as N entities (each member enters
 * individually). Designer-chosen per squad. DESIGN Â§14.
 */
UENUM(BlueprintType)
enum class ESeinSquadContainmentMode : uint8
{
	/** Squad enters as one entity. Squad actor occupies the container; members
	 *  are hidden / parked relative to it. Garrison behavior. */
	AsOne,

	/** Each squad member enters the container individually. Container sees N
	 *  occupants. Useful for large transports that load each soldier discretely. */
	AsN,
};

/**
 * What happens to still-queued reinforcement charges when the squad entity is
 * destroyed or disbanded. A wiped-but-rebuilding squad (all members dead,
 * queue still ticking) keeps its queue and is unaffected; explicit
 * cancellation always refunds exactly, independent of this policy.
 */
UENUM(BlueprintType)
enum class ESeinSquadReinforceRefundPolicy : uint8
{
	/** Every queued entry reverses its snapshotted deduction. */
	Refund,

	/** Queued charges are lost with the squad — committed resources are spent. */
	Forfeit,

	/** Each queued entry refunds PartialRefundPercent of its snapshotted cost. */
	PartialRefund,
};


// NOTE: per-squad ability dispatch policy was relocated to USeinAbility
// (its own DispatchMode / DispatchSelector / DispatchPreferredTag /
// DispatchFallback fields, plus the ApplyAbilityDispatchPolicy helper on
// USeinCommandBrokerResolver). Authoring lives on the ability itself so the
// same policy works uniformly in squad brokers and selection brokers — the
// "Grenade is thrown by one member" decision is a property of the ability,
// not of each squad that grants it. See ESeinAbilityDispatchMode in
// SeinAbility.h for the new home.


/**
 * One slot in a squad's canonical recipe + runtime occupancy. Heterogeneous:
 * each slot defines its own entity class, formation offset, reinforce cost and
 * timings. Runtime identity is the slot's declaration index. `SlotTags` are
 * descriptive role/query metadata and may be shared by multiple slots.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinSquadSlot
{
	GENERATED_BODY()

	/** Descriptive role/query tags. They may be shared by multiple slots;
	 *  `FSeinSquadMemberComponent::SlotIndex` and reinforcement
	 *  `RequestedSlotIndex` are the exact runtime identities. Tag-based helper
	 *  APIs retain first-match compatibility and should be used only when that
	 *  behavior is intended. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Squad")
	FGameplayTagContainer SlotTags;

	/** Entity Blueprint class spawned to fill this slot at squad-create time
	 *  and at reinforce time. Heterogeneous squads pick different classes per
	 *  slot (one sergeant, four riflemen, one machine-gunner).
	 *
	 *  Renamed from `Archetype` (2026-05-19) â€” "archetype" was outdated
	 *  legacy nomenclature from the pre-Phase-5 USeinArchetypeDefinition
	 *  pattern. The new naming reflects the post-refactor reality: each slot
	 *  spawns a SeinARTS entity (the BP-class instance is "the unit"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Squad")
	TSubclassOf<ASeinActor> Entity;

	/** Formation offset relative to the squad centroid + facing. Position +
	 *  rotation. Squad dispatch resolver rotates by anchor facing and adds to
	 *  the squad's anchor when computing per-member move targets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Squad")
	FFixedTransform OffsetTransform;

	/** Resource cost to reinforce this slot. Tag-keyed (matches the framework
	 *  cost convention used on `USeinAbility::ResourceCost` etc.). Heterogeneous
	 *  â€” a sergeant slot may cost more manpower than a rifleman slot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Squad")
	FSeinResourceCost ReinforceCost;

	/** Time (sim-seconds) to build a reinforcement for this slot once queued.
	 *  Indexed per-slot â€” special members can take longer. Default 0 = instant. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Squad")
	FFixedPoint ReinforceBuildTime = FFixedPoint::Zero;

	/** Cooldown (sim-seconds) gating subsequent reinforces of THIS slot after a
	 *  member arrives. Default 0 = no cooldown (the common default). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Squad")
	FFixedPoint ReinforceCooldown = FFixedPoint::Zero;

	/** Runtime: handle of the entity currently occupying this slot. Invalid =
	 *  empty (eligible for reinforce if `bCanReinforce` is true on the squad). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Squad")
	FSeinEntityHandle CurrentOccupant;

	/** Runtime: cooldown remaining before this slot can be queued for reinforce.
	 *  Decremented by FSeinSquadSystem each tick. Zero = ready. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Squad")
	FFixedPoint CurrentCooldown = FFixedPoint::Zero;

	/** Lowest valid tag by exact tag-name string. Shared by runtime mutation,
	 *  event metadata, and snapshot validation; FName index/numeric ordering is
	 *  never part of this canonical contract. */
	FGameplayTag GetCanonicalSlotTag() const
	{
		FGameplayTag Result;
		FString ResultName;
		for (const FGameplayTag Candidate : SlotTags)
		{
			if (!Candidate.IsValid()) continue;
			const FString CandidateName = Candidate.GetTagName().ToString();
			if (!Result.IsValid() || CandidateName < ResultName)
			{
				Result = Candidate;
				ResultName = CandidateName;
			}
		}
		return Result;
	}
};

FORCEINLINE uint32 GetTypeHash(const FSeinSquadSlot& Slot)
{
	uint32 Hash = GetTypeHash(Slot.Entity.Get());
	Hash = HashCombine(Hash, GetTypeHash(Slot.CurrentOccupant));
	Hash = HashCombine(Hash, GetTypeHash(Slot.CurrentCooldown));
	Hash = HashCombine(Hash, GetTypeHash(Slot.ReinforceCost));
	for (const FGameplayTag& Tag : Slot.SlotTags)
	{
		Hash = HashCombine(Hash, GetTypeHash(Tag));
	}
	return Hash;
}

/**
 * One pending reinforcement on the squad's reinforce queue. Build progress
 * ticked by FSeinSquadSystem; on completion, the slot's entity class is spawned
 * at the squad's transform and walks to its slot offset.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinSquadReinforceEntry
{
	GENERATED_BODY()

	/** Monotonic identity unique within the owning squad. Never reused. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Squad")
	int64 RequestID = 0;

	/** Exact slot declaration index captured at enqueue. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Squad")
	int32 RequestedSlotIndex = INDEX_NONE;

	/** Canonically selected tag-name metadata for UI/events. Not identity. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Squad")
	FGameplayTag SlotTag;

	/** Build progress in sim-seconds (0 â†’ TotalBuildTime). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Squad")
	FFixedPoint BuildProgress = FFixedPoint::Zero;

	/** Snapshot of the slot's ReinforceBuildTime at enqueue. Mid-build
	 *  modifier changes don't affect already-queued entries (matches the
	 *  production system's snapshot-at-enqueue convention). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Squad")
	FFixedPoint TotalBuildTime = FFixedPoint::Zero;

	/** Snapshot of the cost actually deducted from the player at enqueue.
	 *  Drives refund-on-cancel without re-resolving cost at cancel time. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Squad")
	FSeinResourceCost DeductedCost;

	/** Funding principal captured at enqueue so ownership changes cannot redirect refunds. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Squad")
	FSeinPlayerID ResourcePayer;
};

FORCEINLINE uint32 GetTypeHash(const FSeinSquadReinforceEntry& Entry)
{
	uint32 Hash = GetTypeHash(Entry.RequestID);
	Hash = HashCombine(Hash, GetTypeHash(Entry.RequestedSlotIndex));
	Hash = HashCombine(Hash, GetTypeHash(Entry.SlotTag));
	Hash = HashCombine(Hash, GetTypeHash(Entry.BuildProgress));
	Hash = HashCombine(Hash, GetTypeHash(Entry.TotalBuildTime));
	Hash = HashCombine(Hash, GetTypeHash(Entry.DeductedCost));
	Hash = HashCombine(Hash, GetTypeHash(Entry.ResourcePayer));
	return Hash;
}

/**
 * Persistent squad component. Lives on the squad entity (a real lightweight
 * `ASeinActor` â€” not abstract â€” so widget components can follow the squad
 * centroid for banners / nameplates). Slots are the canonical member list;
 * the live members array is derived via `GetLiveMembers()`.
 *
 * The squad reuses the existing CommandBroker primitive for member dispatch
 * by carrying `FSeinCommandBrokerData` alongside this component, with the
 * broker's `bSelfCullOnEmpty` flag flipped off so the squad persists past
 * member-list emptiness (squad is destroyed by `FSeinSquadSystem` when its
 * last slot empties AND no reinforces are pending).
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinSquadComponent : public FSeinComponent
{
	GENERATED_BODY()

	/** The formation this squad lays its members out with, around the squad's anchor. EMPTY = the slot
	 *  formation (each member at its authored per-slot OffsetTransform — the default). Point at
	 *  USeinGridFormation / USeinWedgeFormation / USeinRingFormation / a custom USeinFormation to lay the
	 *  members out by footprint instead (the per-slot OffsetTransform is then ignored). The slot list is
	 *  STILL the squad's roster (entity classes, reinforce cost, identity tags) for EVERY formation —
	 *  only the per-slot OffsetTransform is specific to the slot formation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Squad",
		meta = (DisplayName = "Formation Class"))
	TSoftClassPtr<USeinFormation> FormationClass;

	/** Canonical slot list. Each slot is heterogeneous (own entity class, cost,
	 *  formation offset). Mutating this array at runtime requires routing
	 *  through the mutation BPFL so member slot-index back-refs stay consistent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Squad")
	TArray<FSeinSquadSlot> Slots;

	/** Handle of the current squad leader. Single source of truth â€” members
	 *  query their leadership status via `World->GetSquadLeader(SquadEntity) == MyHandle`.
	 *  Auto-promoted by `FSeinSquadSystem` on leader death (next live occupant
	 *  in slot order, or the first slot tagged `Squad.Slot.Leader` if any). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Squad")
	FSeinEntityHandle Leader;

	/** Designer toggle: can this squad currently be reinforced? Game-side logic
	 *  flips this (in/out of friendly territory, locked by mission script, etc.).
	 *  Reinforce ability checks this in its CanActivate gate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Squad")
	bool bCanReinforce = true;

	/** Destruction/disband settlement for still-queued reinforcement charges
	 *  (see ESeinSquadReinforceRefundPolicy). Applied by the deterministic
	 *  entity-teardown sweep; cancellation refunds exactly regardless. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Squad",
		meta = (DisplayName = "Reinforce Refund On Destruction"))
	ESeinSquadReinforceRefundPolicy ReinforceRefundPolicy =
		ESeinSquadReinforceRefundPolicy::Refund;

	/** PartialRefund only: fraction (0..1) of each queued entry's snapshotted
	 *  cost returned on destruction. Clamped at settlement time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Squad",
		meta = (EditCondition =
			"ReinforceRefundPolicy == ESeinSquadReinforceRefundPolicy::PartialRefund"))
	FFixedPoint PartialRefundPercent = FFixedPoint::Half;

	/** Generic squad-level opt-in for ANY preview/visualization system that
	 *  wants to render "where would members land if I clicked here" decals
	 *  while the squad is selected (the SeinARTSCover module's destination
	 *  preview is the framework's reference consumer; other plugins are free
	 *  to read this flag too).
	 *
	 *  Lives here on the squad component â€” not on each member's
	 *  FSeinNavigationComponent â€” because squad selection is at the SQUAD
	 *  level (per the framework rule: clicking a squad member selects the
	 *  squad, not the member). A squad opting out via this flag suppresses
	 *  previews for ALL of its members in one place; designers don't have to
	 *  flip every member's per-unit nav-preview opt-out. Members selected
	 *  outside a squad context (lone units / mixed selections that include
	 *  non-squad units) still respect their own
	 *  FSeinNavigationComponent::bShowNavigationPreview.
	 *
	 *  Default true â€” most squads benefit from the hover preview. Set false
	 *  for ambient/scripted squads where the visualization noise hurts more
	 *  than it helps (spawning waves, scenario-driven garrisons, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Squad")
	bool bShowFormationPreview = true;

	/** How this squad enters containers. AsOne = squad actor enters as a single
	 *  occupant contributing Size = SlotCount. AsN = each member enters
	 *  individually. DESIGN Â§14. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Squad")
	ESeinSquadContainmentMode ContainmentMode = ESeinSquadContainmentMode::AsOne;

	/** Pathing-only coherency hint. Members further than this from the squad
	 *  centroid are nudged back during pathing. Gameplay effects of being out
	 *  of coherency are designer-side (effects keyed off a tag the squad system
	 *  applies). Zero = no coherency enforcement. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Squad")
	FFixedPoint CoherencyRadius = FFixedPoint::Zero;

	/** Per-squad slot RE-MATCH on the LATERAL (left/right) axis. OPT-IN, default false.
	 *
	 *  When the squad moves and the formation rotates, members are re-matched to their slots by
	 *  current left/right rank so nobody crosses paths trading flanks â€” the leftmost member fills
	 *  the leftmost slot, etc. Front/back ordering is left untouched, so a wedge/arrow keeps its tip
	 *  (its authored front-vs-back roles) while only the flanks re-rank. This is the "1-D" behavior.
	 *
	 *  Leave OFF to pin authored slot roles to their members (each member always walks to its own
	 *  authored slot, even if that means crossing a squadmate on a hard turn). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Squad",
		meta = (DisplayName = "Reassign Slots Lateral"))
	bool bReassignSlotsLateral = false;

	/** Per-squad slot RE-MATCH on the DEPTH (front/back) axis. OPT-IN, default false.
	 *
	 *  Enable ALONGSIDE Reassign Slots Lateral to get a full 2-D nearest-slot assignment: any member
	 *  can take any slot, picked to minimise crossing as the squad converges. Good for a wide
	 *  line/block (Napoleonic ranks) where slot roles are interchangeable. Enabling depth ALONE
	 *  re-ranks front/back only (left/right pinned) â€” a niche mirror of the lateral case.
	 *
	 *  Slot assignment only; the movement system decides backward-walk animation independently. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Squad",
		meta = (DisplayName = "Reassign Slots Depth"))
	bool bReassignSlotsDepth = false;

	/** OBSTACLE-SIDE avoidance opinion (OPT-IN, default false): should OTHER units route around this
	 *  squad as one cohesive body instead of threading through the gaps between its members?
	 *
	 *  On = a foreign unit crossing this squad sees ONE disc obstacle (the squad's centroid + extent)
	 *  and slides around it, instead of aiming at the moving gap between members and orbiting. Two
	 *  squads that both enable it sidestep each other as whole bodies. Off (default) = per-member
	 *  avoidance (a unit may pass through the formation's interior). Propagated to the squad's
	 *  command broker each tick by the squad system. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Squad",
		meta = (DisplayName = "Avoid As Blob"))
	bool bAvoidAsBlob = false;

	/** Per-squad override for the dispatch resolver class used by this squad's
	 *  CommandBroker. Defaults to the framework's `USeinSquadDispatchResolver`
	 *  (leader-first dispatch + per-slot transform formations). Designers
	 *  override per-squad for project-specific dispatch policy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Squad")
	TSubclassOf<USeinCommandBrokerResolver> DispatchResolverClass;

	// Per-ability dispatch policy moved to USeinAbility (DispatchMode /
	// DispatchSelector / DispatchPreferredTag / DispatchFallback). The squad
	// no longer carries a per-tag override table. See the migration note at
	// the top of this file.

	/** Pending reinforcements. Ticked by `FSeinSquadSystem`; on entry build
	 *  completion, the exact declaration-index slot's entity class spawns at the
	 *  squad's transform and the member walks to its slot offset. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Squad")
	TArray<FSeinSquadReinforceEntry> ReinforceQueue;

	/** Next monotonic request identity. Canonical state; starts at one and never wraps. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Squad")
	int64 NextReinforceRequestID = 1;

	// â”€â”€â”€ Helpers (pure read; routed-mutations live in the mutation BPFL) â”€â”€â”€

	/** All currently-occupied slots' member handles. Walks Slots, skips invalid
	 *  occupants. Stable order = slot declaration order. */
	TArray<FSeinEntityHandle> GetLiveMembers() const;

	/** Count of slots with a valid occupant. */
	int32 GetLiveMemberCount() const;

	/** Total slot count (regardless of occupancy). */
	int32 GetMaxSquadSize() const { return Slots.Num(); }

	bool HasLeader() const { return Leader.IsValid(); }

	/** First declaration-order slot carrying the tag, or INDEX_NONE. This is a
	 *  compatibility/query helper only: tags may be shared and are not runtime
	 *  slot identity. Use exact slot-index APIs for mutation. */
	int32 IndexOfSlotByTag(FGameplayTag SlotTag) const;

	/** Find which slot a member occupies. INDEX_NONE if the member isn't in
	 *  any slot of this squad. */
	int32 IndexOfSlotByMember(FSeinEntityHandle Member) const;

	/** First empty slot in declaration order. INDEX_NONE if all slots are
	 *  occupied. Used as the default reinforce target. */
	int32 FindFirstEmptySlotIndex() const;

	/** Centroid of all live members in the slot list. Returns the squad's last
	 *  known position when the squad has no live members (caller decides what to
	 *  do with that â€” typically destroy the squad entity). */
	FFixedVector ComputeCentroid(const FFixedVector& Fallback) const;

};

FORCEINLINE uint32 GetTypeHash(const FSeinSquadComponent& Component)
{
	uint32 Hash = GetTypeHash(Component.Leader);
	Hash = HashCombine(Hash, GetTypeHash(Component.bCanReinforce));
	Hash = HashCombine(Hash,
		GetTypeHash(static_cast<uint8>(Component.ReinforceRefundPolicy)));
	Hash = HashCombine(Hash, GetTypeHash(Component.PartialRefundPercent));
	Hash = HashCombine(Hash, GetTypeHash(Component.bShowFormationPreview));
	Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Component.ContainmentMode)));
	Hash = HashCombine(Hash, GetTypeHash(Component.CoherencyRadius));
	Hash = HashCombine(Hash, GetTypeHash(Component.bReassignSlotsLateral));
	Hash = HashCombine(Hash, GetTypeHash(Component.bReassignSlotsDepth));
	Hash = HashCombine(Hash, GetTypeHash(Component.bAvoidAsBlob));
	Hash = HashCombine(Hash, GetTypeHash(Component.Slots.Num()));
	for (const FSeinSquadSlot& Slot : Component.Slots)
	{
		Hash = HashCombine(Hash, GetTypeHash(Slot));
	}
	for (const FSeinSquadReinforceEntry& Entry : Component.ReinforceQueue)
	{
		Hash = HashCombine(Hash, GetTypeHash(Entry));
	}
	Hash = HashCombine(Hash, GetTypeHash(Component.NextReinforceRequestID));
	return Hash;
}

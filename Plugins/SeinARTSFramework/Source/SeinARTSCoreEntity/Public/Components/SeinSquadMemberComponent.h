/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadMemberComponent.h
 * @brief   Component on individual squad member entities — back-references
 *          its owning squad and the stable slot ID it occupies.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Components/SeinComponent.h"
#include "SeinSquadMemberComponent.generated.h"

/**
 * Placed on each individual entity that belongs to a squad. Identifies the
 * squad and the stable slot it occupies. Leadership status is NOT stored
 * here — it is queried via the squad's `Leader` handle to keep a single
 * source of truth (slot mutations on upgrades / member-cap mods don't need
 * to walk every member to fix a duplicated bool).
 *
 * The slot's `OffsetTransform` (formation position) lives on the SQUAD's
 * `FSeinSquadSlot`, not here — members look it up via `SlotIndex` (primary,
 * always unique) or `SlotTag` (secondary, role-based queries). A single
 * mutation to the slot's offset is reflected for every member occupying it
 * without per-member writes.
 *
 * Index vs Tag: SlotIndex is the canonical identity (array position in
 * `FSeinSquadComponent::Slots`), unique by construction. SlotTag is metadata —
 * often shared across slots (e.g. five rifleman slots all carry
 * `Squad.Slot.Rifleman`) — and is for role queries like "find the leader
 * slot," NOT formation position lookup. Resolvers must use SlotIndex for
 * formation; using a shared tag collapses every member to the first
 * slot's offset.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinSquadMemberComponent : public FSeinComponent
{
	GENERATED_BODY()

	/** Handle to the squad entity this member belongs to. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Squad")
	FSeinEntityHandle SquadEntity;

	/** Canonical slot identity — array index in `FSeinSquadComponent::Slots`.
	 *  Always unique (by construction). Used by resolvers for formation
	 *  position lookup. INDEX_NONE = "not assigned to a slot" (legacy
	 *  data / pre-spawn / mid-tear-down state). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Squad")
	int32 SlotIndex = INDEX_NONE;

	/** Role metadata selected canonically by tag name from the slot's `SlotTags`
	 *  container at spawn time. Often shared across slots (multiple
	 *  rifleman slots all tagged `Squad.Slot.Rifleman`); use for role-
	 *  based queries ("find the leader slot," "find any medic slot"),
	 *  NOT for formation position lookup — that requires SlotIndex. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Squad")
	FGameplayTag SlotTag;
};

FORCEINLINE uint32 GetTypeHash(const FSeinSquadMemberComponent& Component)
{
	uint32 Hash = GetTypeHash(Component.SquadEntity);
	Hash = HashCombine(Hash, GetTypeHash(Component.SlotIndex));
	Hash = HashCombine(Hash, GetTypeHash(Component.SlotTag));
	return Hash;
}

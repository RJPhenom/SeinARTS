/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEntityPool.h
 * @brief   Generational entity pool with slot-based allocation.
 *
 * Provides O(1) acquire/release/lookup using a free list and generation counters.
 * Slot 0 is permanently reserved so that Index=0 always represents an invalid handle.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Transform.h"
#include "SeinEntityPool.generated.h"

class USeinWorldSubsystem;

/**
 * Complete state of one entity-pool slot, including slots that are currently
 * free or permanently retired. This is a native checkpoint contract rather
 * than a gameplay-authoring type.
 */
USTRUCT(meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinEntityPoolSlotState
{
	GENERATED_BODY()

	UPROPERTY()
	FSeinEntity Entity;

	UPROPERTY()
	int32 Generation = 0;

	UPROPERTY()
	FSeinPlayerID Owner;

	UPROPERTY()
	bool bRetired = false;

	bool operator==(const FSeinEntityPoolSlotState& Other) const
	{
		return Entity.ID == Other.Entity.ID
			&& Entity.Transform == Other.Entity.Transform
			&& Entity.Flags == Other.Entity.Flags
			&& Generation == Other.Generation
			&& Owner == Other.Owner
			&& bRetired == Other.bRetired;
	}
};

/**
 * Exact allocator topology for FSeinEntityPool. Slots includes the reserved
 * slot zero and FreeList preserves its LIFO order byte-for-byte. ActiveCount
 * is intentionally absent because it is derived while validating the slots.
 */
USTRUCT(meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinEntityPoolExactState
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Capacity = 0;

	UPROPERTY()
	TArray<FSeinEntityPoolSlotState> Slots;

	UPROPERTY()
	TArray<int32> FreeList;

	bool operator==(const FSeinEntityPoolExactState& Other) const
	{
		return Capacity == Other.Capacity
			&& Slots == Other.Slots
			&& FreeList == Other.FreeList;
	}
};

/**
 * Dense, slot-indexed entity pool with generational handles.
 *
 * Entities live in a flat TArray indexed by slot. Each slot has a generation
 * counter that is incremented on every Release, invalidating any outstanding
 * handles that still reference the old occupant.
 */
class SEINARTSCOREENTITY_API FSeinEntityPool
{
public:
	FSeinEntityPool();

	/** Pre-allocate arrays for the given capacity. Slot 0 is reserved. */
	void Initialize(int32 InitialCapacity);

	/**
	 * Acquire a new entity from the pool.
	 * Pops a free slot (or grows the pool if none available), sets up the entity,
	 * and returns a valid generational handle.
	 */
	FSeinEntityHandle Acquire(
		const FFixedTransform& Transform,
		FSeinPlayerID OwnerID);

	/**
	 * Release an entity back to the pool.
	 * The slot's generation is incremented, invalidating existing handles. A
	 * slot at the maximum generation is retired instead of wrapping into ABA.
	 */
	void Release(FSeinEntityHandle Handle);

	/** Get a mutable pointer to the entity at Handle, or nullptr if the handle is stale/invalid. */
	FSeinEntity* Get(FSeinEntityHandle Handle);

	/** Get a const pointer to the entity at Handle, or nullptr if the handle is stale/invalid. */
	const FSeinEntity* Get(FSeinEntityHandle Handle) const;

	/** Check whether a handle still refers to a live entity. */
	bool IsValid(FSeinEntityHandle Handle) const;

	/** Get the owner player ID for an entity. */
	FSeinPlayerID GetOwner(FSeinEntityHandle Handle) const;

	/** Set the owner player ID for an entity. */
	void SetOwner(FSeinEntityHandle Handle, FSeinPlayerID NewOwner);

	FORCEINLINE int32 GetActiveCount() const { return ActiveCount; }
	FORCEINLINE int32 GetCapacity() const { return Capacity; }

	/**
	 * Current generation counter for a slot index, or 0 (invalid) if the slot
	 * is out of range. Intended for snapshot validation and diagnostics; do not
	 * use it to replace the generation carried by an existing handle.
	 */
	FORCEINLINE int32 GetSlotGeneration(int32 SlotIndex) const
	{
		return Generations.IsValidIndex(SlotIndex) ? Generations[SlotIndex] : 0;
	}

	/** Reset pool to a clean state (keeps no allocations). */
	void Reset();

	/**
	 * Capture every slot and the exact allocator order. Quiescent checkpoints
	 * reject deferred-destroy tombstones unless the caller explicitly opts in.
	 * OutState is unchanged on failure.
	 */
	bool CaptureExactState(
		FSeinEntityPoolExactState& OutState,
		FString& OutError,
		bool bAllowDeferredDestroyTombstones = false) const;

	/**
	 * Validate and stage exact state into this pool. Capacity is caller-bounded
	 * before any destination allocation, and failure leaves this pool unchanged.
	 * ActiveCount is derived from the validated slot categories.
	 */
	bool TryStageExactState(
		const FSeinEntityPoolExactState& State,
		int32 MaxCapacity,
		FString& OutError,
		bool bAllowDeferredDestroyTombstones = false);

	/** Direct slot/generation/owner reconstruction for snapshot restore.
	 *  Caller passes a flat list of (SlotIndex, Generation, Transform, Owner, bAlive)
	 *  tuples; pool resets, sizes itself to fit, and writes each entry directly.
	 *  Free-list is rebuilt from any unused/dead slots in [1, MaxSlot]. */
	bool RebuildFromSnapshot(int32 MaxSlotIndex,
		const TArray<int32>& SlotIndices,
		const TArray<int32>& SlotGenerations,
		const TArray<FFixedTransform>& SlotTransforms,
		const TArray<FSeinPlayerID>& SlotOwners,
		const TArray<bool>& SlotAliveFlags);

	/**
	 * Iterate all alive entities.
	 * @param Callback  Signature: void(FSeinEntityHandle Handle, FSeinEntity& Entity)
	 */
	template<typename Func>
	void ForEachEntity(Func&& Callback)
	{
		for (int32 i = 1; i < Entities.Num(); ++i)
		{
			if (Entities[i].IsAlive())
			{
				FSeinEntityHandle Handle(i, Generations[i]);
				Callback(Handle, Entities[i]);
			}
		}
	}

	/** Const version of ForEachEntity. */
	template<typename Func>
	void ForEachEntity(Func&& Callback) const
	{
		for (int32 i = 1; i < Entities.Num(); ++i)
		{
			if (Entities[i].IsAlive())
			{
				FSeinEntityHandle Handle(i, Generations[i]);
				Callback(Handle, const_cast<const FSeinEntity&>(Entities[i]));
			}
		}
	}

private:
	friend class USeinWorldSubsystem;

	/**
	 * Deferred destruction marks gameplay life false before PostTick teardown.
	 * These exact-generation helpers are intentionally private: ordinary pool
	 * lookup remains live-only, while the world subsystem may finish teardown
	 * and release the still-owned tombstone slot.
	 */
	bool IsDeferredDestroyTombstone(FSeinEntityHandle Handle) const;
	const FSeinEntity* GetDeferredDestroyTombstone(
		FSeinEntityHandle Handle) const;
	FSeinPlayerID GetDeferredDestroyOwner(FSeinEntityHandle Handle) const;
	void ReleaseDeferredDestroy(FSeinEntityHandle Handle);

	/** Grow the pool by doubling (or to MinCapacity if larger). */
	void Grow(int32 MinCapacity);

	TArray<FSeinEntity> Entities;
	TArray<int32> Generations;
	TArray<FSeinPlayerID> OwnerPlayerIDs;
	/** Generation-exhausted slots. Kept separate from deferred-destroy
	 *  tombstones so an exact max-generation handle cannot be torn down twice. */
	TArray<uint8> RetiredSlots;
	TArray<int32> FreeList;

	int32 ActiveCount;
	int32 Capacity;
};

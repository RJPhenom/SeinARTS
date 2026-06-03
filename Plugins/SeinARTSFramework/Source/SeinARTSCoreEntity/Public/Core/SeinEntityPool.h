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
	 * The slot's generation is incremented, invalidating any existing handles.
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

	/** Reset pool to a clean state (keeps no allocations). */
	void Reset();

	/** Direct slot/generation/owner reconstruction for snapshot restore.
	 *  Caller passes a flat list of (SlotIndex, Generation, Transform, Owner, bAlive)
	 *  tuples; pool resets, sizes itself to fit, and writes each entry directly.
	 *  Free-list is rebuilt from any unused/dead slots in [1, MaxSlot]. */
	void RebuildFromSnapshot(int32 MaxSlotIndex,
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
	/** Grow the pool by doubling (or to MinCapacity if larger). */
	void Grow(int32 MinCapacity);

	TArray<FSeinEntity> Entities;
	TArray<int32> Generations;
	TArray<FSeinPlayerID> OwnerPlayerIDs;
	TArray<int32> FreeList;

	int32 ActiveCount;
	int32 Capacity;
};

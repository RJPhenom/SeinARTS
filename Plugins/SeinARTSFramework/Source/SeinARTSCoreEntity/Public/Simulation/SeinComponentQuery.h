/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinComponentQuery.h
 * @brief   Typed, hoisted component views + deterministic multi-storage joins.
 *
 * Every component storage is a flat array parallel to the entity pool, so all
 * storages share one index space: the entity slot. A system that resolves a
 * TSeinComponentView per type ONCE per tick can then join components by direct
 * slot arithmetic — replacing the per-entity pattern of
 * `World.GetComponent<T>(Handle)` (a UScriptStruct hash-map probe + virtual
 * call + handle revalidation for every entity, every type, every tick).
 *
 * Determinism: SeinQuery::ForEachAlive visits slots in ASCENDING order — the
 * same order as FSeinEntityPool::ForEachEntity and
 * ISeinComponentStorage::ForEachLiveComponent — and visits exactly the set
 * {alive pool entities that hold every listed component}, identical to the
 * legacy pool-iteration + GetComponent-null-check pattern. Bookkeeping
 * semantics (revision touch / escape flags) mirror the handle-keyed accessors
 * one-for-one; see the slot-addressed section of ISeinComponentStorage.
 *
 * Contract: visitors must NOT add or remove components of any viewed storage
 * during iteration (same rule as ForEachLiveComponent — the live bit-array is
 * walked in place, and Grow invalidates the bits reference). Deferring entity
 * destroys is fine. Views are cheap to construct but hold a raw storage
 * pointer: build them per tick, never cache across ticks.
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinEntityPool.h"
#include "Simulation/ComponentStorage.h"
#include "Simulation/SeinWorldSubsystem.h"

/**
 * Read-only typed view over one component type's storage. Constructible from
 * a const world; usable from observer/presentation callbacks.
 */
template<typename T>
struct TSeinComponentReadView
{
	const ISeinComponentStorage* Storage = nullptr;

	TSeinComponentReadView() = default;
	explicit TSeinComponentReadView(const USeinWorldSubsystem& World)
		: Storage(World.GetComponentStorageRaw(T::StaticStruct()))
	{}

	bool IsBound() const { return Storage != nullptr; }
	int32 Num() const { return Storage ? Storage->GetComponentCount() : 0; }
	const TBitArray<>& LiveBits() const { return Storage->GetLiveSlotBits(); }
	int32 GenerationAt(int32 Slot) const { return Storage ? Storage->GetGenerationAtSlot(Slot) : 0; }

	/** Payload at a live slot the caller has already validated (e.g. the query
	 *  driver slot). Nullptr when the slot is not live. */
	const T* GetConstAt(int32 Slot) const
	{
		return Storage ? static_cast<const T*>(Storage->GetRawAtSlot(Slot)) : nullptr;
	}

	/** Generation-validated optional join: payload iff this storage holds the
	 *  exact entity (slot AND generation). The slot-only overload above is for
	 *  the driver storage, whose generation the query already validated. */
	const T* GetConstAt(int32 Slot, FSeinEntityHandle Handle) const
	{
		return (Storage
			&& Storage->GetGenerationAtSlot(Slot) == Handle.Generation)
			? static_cast<const T*>(Storage->GetRawAtSlot(Slot))
			: nullptr;
	}
};

/**
 * Mutable typed view. Construction runs the world's mutable-state-access gate
 * once (unbound — all accessors return null/zero — from read-only observer
 * callbacks, exactly like the per-call handle-keyed accessors it replaces).
 */
template<typename T>
struct TSeinComponentView
{
	ISeinComponentStorage* Storage = nullptr;

	TSeinComponentView() = default;
	explicit TSeinComponentView(USeinWorldSubsystem& World)
		: Storage(World.GetComponentStorageMutable(T::StaticStruct()))
	{}

	bool IsBound() const { return Storage != nullptr; }
	int32 Num() const { return Storage ? Storage->GetComponentCount() : 0; }
	const TBitArray<>& LiveBits() const { return Storage->GetLiveSlotBits(); }
	int32 GenerationAt(int32 Slot) const { return Storage ? Storage->GetGenerationAtSlot(Slot) : 0; }

	/** Read access at a validated live slot — no revision bookkeeping. */
	const T* GetConstAt(int32 Slot) const
	{
		return Storage ? static_cast<const T*>(Storage->GetRawAtSlot(Slot)) : nullptr;
	}

	/** Generation-validated optional-join read (see TSeinComponentReadView). */
	const T* GetConstAt(int32 Slot, FSeinEntityHandle Handle) const
	{
		return (Storage
			&& Storage->GetGenerationAtSlot(Slot) == Handle.Generation)
			? static_cast<const T*>(Storage->GetRawAtSlot(Slot))
			: nullptr;
	}

	/** Mutable access with the conservative revision touch
	 *  (== GetComponentMutable semantics). */
	T* GetMutableAt(int32 Slot)
	{
		return Storage ? static_cast<T*>(Storage->GetMutableRawAtSlot(Slot)) : nullptr;
	}

	/** Deferred-revision mutable access for disjoint-write kernels
	 *  (== GetComponentForDeferredMutation semantics). Pair every actual write
	 *  with CommitDeferred on the serial spine. */
	T* GetDeferredAt(int32 Slot)
	{
		return Storage ? static_cast<T*>(Storage->GetDeferredMutationRawAtSlot(Slot)) : nullptr;
	}

	void CommitDeferred(FSeinEntityHandle Handle)
	{
		if (Storage)
		{
			Storage->CommitDeferredMutation(Handle);
		}
	}
};

namespace SeinQuery
{
	/**
	 * Visit every ALIVE entity that holds the driver component and every join
	 * component, in ascending slot order. Visitor signature:
	 *     void(FSeinEntityHandle Handle, int32 Slot)
	 * Component access goes through the views by Slot (driver: slot-only
	 * accessors; the joins listed here are generation-matched already).
	 *
	 * Put the RAREST component first as the driver — iteration walks the
	 * driver's live bits and filters the rest by generation compare, so the
	 * driver's population bounds the work. Join membership is checked as
	 * `GenerationAt(Slot) == driver generation` (non-live join slots report
	 * generation 0, which can never match a live generation), then the handle
	 * is validated against the pool so stale storage entries are skipped —
	 * identical to the legacy pattern's effective set.
	 */
	template<typename TDriver, typename FVisitor, typename... TJoin>
	void ForEachAlive(
		const FSeinEntityPool& Pool,
		TSeinComponentView<TDriver>& Driver,
		FVisitor&& Visitor,
		TSeinComponentView<TJoin>&... Joins)
	{
		if (!Driver.IsBound() || (... || !Joins.IsBound()))
		{
			return;
		}
		const TBitArray<>& Bits = Driver.LiveBits();
		for (TConstSetBitIterator<> It(Bits); It; ++It)
		{
			const int32 Slot = It.GetIndex();
			const int32 Generation = Driver.GenerationAt(Slot);
			if ((... || (Joins.GenerationAt(Slot) != Generation)))
			{
				continue;
			}
			const FSeinEntityHandle Handle(Slot, Generation);
			if (!Pool.IsValid(Handle))
			{
				continue;
			}
			Visitor(Handle, Slot);
		}
	}

	/** Read-only variant over read views (usable from const contexts). */
	template<typename TDriver, typename FVisitor, typename... TJoin>
	void ForEachAliveRead(
		const FSeinEntityPool& Pool,
		const TSeinComponentReadView<TDriver>& Driver,
		FVisitor&& Visitor,
		const TSeinComponentReadView<TJoin>&... Joins)
	{
		if (!Driver.IsBound() || (... || !Joins.IsBound()))
		{
			return;
		}
		const TBitArray<>& Bits = Driver.LiveBits();
		for (TConstSetBitIterator<> It(Bits); It; ++It)
		{
			const int32 Slot = It.GetIndex();
			const int32 Generation = Driver.GenerationAt(Slot);
			if ((... || (Joins.GenerationAt(Slot) != Generation)))
			{
				continue;
			}
			const FSeinEntityHandle Handle(Slot, Generation);
			if (!Pool.IsValid(Handle))
			{
				continue;
			}
			Visitor(Handle, Slot);
		}
	}
}

/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		SeinEntityPool.cpp
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of the generational entity pool.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#include "Core/SeinEntityPool.h"

namespace
{
	bool TryAdvanceGeneration(int32& Generation)
	{
		// Wrapping would make a very old handle valid again. Exhausted slots are
		// retired; pool growth supplies a fresh slot and generation.
		if (Generation == MAX_int32)
		{
			return false;
		}
		Generation = Generation <= 0 ? 1 : Generation + 1;
		return true;
	}

	bool ValidateExactState(
		const FSeinEntityPoolExactState& State,
		int32 MaxCapacity,
		bool bAllowDeferredDestroyTombstones,
		int32& OutActiveCount,
		FString& OutError)
	{
		OutActiveCount = 0;
		if (MaxCapacity < 0
			|| State.Capacity < 0
			|| State.Capacity == MAX_int32
			|| State.Capacity > MaxCapacity)
		{
			OutError = FString::Printf(
				TEXT("Entity-pool capacity %d exceeds the valid bound [0, %d]."),
				State.Capacity,
				MaxCapacity);
			return false;
		}

		const int32 ExpectedSlotCount = State.Capacity + 1;
		if (State.Slots.Num() != ExpectedSlotCount
			|| State.FreeList.Num() > State.Capacity)
		{
			OutError = FString::Printf(
				TEXT("Entity-pool topology has %d slots and %d free entries for capacity %d."),
				State.Slots.Num(),
				State.FreeList.Num(),
				State.Capacity);
			return false;
		}

		const FSeinEntityPoolSlotState& Reserved = State.Slots[0];
		if (Reserved.Entity.IsAlive()
			|| Reserved.Entity.Flags != 0
			|| Reserved.Entity.ID.IsValid()
			|| Reserved.Entity.Transform != FFixedTransform::Identity()
			|| Reserved.Generation != 0
			|| !Reserved.Owner.IsNeutral()
			|| Reserved.bRetired)
		{
			OutError = TEXT("Entity-pool slot zero is not the canonical reserved sentinel.");
			return false;
		}

		TBitArray<> FreeSlots(false, ExpectedSlotCount);
		for (const int32 SlotIndex : State.FreeList)
		{
			if (SlotIndex <= 0
				|| SlotIndex > State.Capacity
				|| FreeSlots[SlotIndex])
			{
				OutError = FString::Printf(
					TEXT("Entity-pool free list contains invalid or duplicate slot %d."),
					SlotIndex);
				return false;
			}
			FreeSlots[SlotIndex] = true;
		}

		for (int32 SlotIndex = 1; SlotIndex <= State.Capacity; ++SlotIndex)
		{
			const FSeinEntityPoolSlotState& Slot = State.Slots[SlotIndex];
			const bool bFree = FreeSlots[SlotIndex];
			if (Slot.Entity.IsAlive())
			{
				if (Slot.Generation <= 0 || Slot.bRetired || bFree)
				{
					OutError = FString::Printf(
						TEXT("Live entity-pool slot %d has invalid allocator metadata."),
						SlotIndex);
					return false;
				}
				++OutActiveCount;
				continue;
			}

			if (Slot.bRetired)
			{
				if (Slot.Generation != MAX_int32
					|| bFree
					|| Slot.Entity.Flags != 0
					|| !Slot.Owner.IsNeutral())
				{
					OutError = FString::Printf(
						TEXT("Retired entity-pool slot %d is not terminal and neutral."),
						SlotIndex);
					return false;
				}
				continue;
			}

			if (bFree)
			{
				if (Slot.Generation < 0
					|| Slot.Generation == MAX_int32
					|| Slot.Entity.Flags != 0
					|| !Slot.Owner.IsNeutral())
				{
					OutError = FString::Printf(
						TEXT("Free entity-pool slot %d has live or retired state."),
						SlotIndex);
					return false;
				}
				continue;
			}

			// A dead, non-retired, allocated slot is owned temporarily by the
			// world's deferred-destroy pass. It is not a quiescent checkpoint.
			if (!bAllowDeferredDestroyTombstones || Slot.Generation <= 0)
			{
				OutError = FString::Printf(
					TEXT("Entity-pool slot %d is an invalid deferred-destroy tombstone."),
					SlotIndex);
				return false;
			}
			++OutActiveCount;
		}

		OutError.Reset();
		return true;
	}
}

FSeinEntityPool::FSeinEntityPool()
	: ActiveCount(0)
	, Capacity(0)
{
}

void FSeinEntityPool::Initialize(int32 InitialCapacity)
{
	if (InitialCapacity < 0 || InitialCapacity == MAX_int32)
	{
		UE_LOG(LogTemp, Error,
			TEXT("EntityPool::Initialize rejected invalid capacity %d"),
			InitialCapacity);
		InitialCapacity = 0;
	}
	// +1 because slot 0 is reserved
	const int32 TotalSlots = InitialCapacity + 1;

	Entities.SetNum(TotalSlots);
	Generations.SetNum(TotalSlots);
	OwnerPlayerIDs.SetNum(TotalSlots);
	RetiredSlots.Init(0, TotalSlots);
	SlotMutationRevisions.Init(0, TotalSlots);
	FreeList.Empty(InitialCapacity);

	// Zero out generations
	FMemory::Memzero(Generations.GetData(), TotalSlots * sizeof(int32));

	// Slot 0 is reserved — mark it as dead
	Entities[0].SetAlive(false);
	Entities[0].Flags = 0;
	OwnerPlayerIDs[0] = FSeinPlayerID::Neutral();

	// Push free indices in reverse order so that lower indices are popped first
	for (int32 i = TotalSlots - 1; i >= 1; --i)
	{
		Entities[i].SetAlive(false);
		Entities[i].Flags = 0;
		OwnerPlayerIDs[i] = FSeinPlayerID::Neutral();
		FreeList.Push(i);
	}

	ActiveCount = 0;
	Capacity = InitialCapacity;
	BumpTopologyRevision();
}

FSeinEntityHandle FSeinEntityPool::Acquire(
	const FFixedTransform& Transform,
	FSeinPlayerID OwnerID)
{
	while (FreeList.Num() > 0
		&& (RetiredSlots[FreeList.Last()] != 0
			|| Generations[FreeList.Last()] == MAX_int32))
	{
		FreeList.Pop(EAllowShrinking::No);
	}
	if (FreeList.Num() == 0)
	{
		const int32 NewCapacity = FMath::Max(Capacity * 2, 64);
		Grow(NewCapacity);
	}
	if (FreeList.Num() == 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("EntityPool::Acquire could not allocate a reusable slot"));
		return FSeinEntityHandle::Invalid();
	}

	const int32 SlotIndex = FreeList.Pop(EAllowShrinking::No);

	// Increment generation (start at 1 so generation 0 is always invalid)
	int32& Gen = Generations[SlotIndex];
	if (!TryAdvanceGeneration(Gen))
	{
		UE_LOG(LogTemp, Error,
			TEXT("EntityPool::Acquire encountered exhausted slot %d"),
			SlotIndex);
		return FSeinEntityHandle::Invalid();
	}

	FSeinEntity& Entity = Entities[SlotIndex];
	Entity.Transform = Transform;
	Entity.Flags = FSeinEntity::FLAG_ALIVE;
	Entity.ID = FSeinID::Invalid(); // Legacy field, not used in handle-based flow

	RetiredSlots[SlotIndex] = 0;
	OwnerPlayerIDs[SlotIndex] = OwnerID;
	ActiveCount++;
	TouchSlot(SlotIndex);
	BumpTopologyRevision();

	UE_LOG(LogTemp, Verbose, TEXT("EntityPool: Acquired slot %d gen %d (active: %d)"),
		SlotIndex, Gen, ActiveCount);

	return FSeinEntityHandle(SlotIndex, Gen);
}

void FSeinEntityPool::Release(FSeinEntityHandle Handle)
{
	if (!Handle.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("EntityPool::Release called with invalid handle"));
		return;
	}

	if (!Entities.IsValidIndex(Handle.Index))
	{
		UE_LOG(LogTemp, Warning, TEXT("EntityPool::Release — index %d out of range"), Handle.Index);
		return;
	}

	if (Generations[Handle.Index] != Handle.Generation)
	{
		UE_LOG(LogTemp, Warning, TEXT("EntityPool::Release — stale handle %s (current gen: %d)"),
			*Handle.ToString(), Generations[Handle.Index]);
		return;
	}

	if (!Entities[Handle.Index].IsAlive())
	{
		UE_LOG(LogTemp, Warning, TEXT("EntityPool::Release — slot %d is already free"),
			Handle.Index);
		return;
	}

	FSeinEntity& Entity = Entities[Handle.Index];
	Entity.Flags = 0; // Clear all flags
	OwnerPlayerIDs[Handle.Index] = FSeinPlayerID::Neutral();

	// Increment generation to invalidate outstanding handles
	int32& Gen = Generations[Handle.Index];
	if (TryAdvanceGeneration(Gen))
	{
		FreeList.Push(Handle.Index);
	}
	else
	{
		RetiredSlots[Handle.Index] = 1;
		UE_LOG(LogTemp, Log,
			TEXT("EntityPool: Retired exhausted slot %d"),
			Handle.Index);
	}
	ActiveCount--;
	TouchSlot(Handle.Index);
	BumpTopologyRevision();

	UE_LOG(LogTemp, Verbose, TEXT("EntityPool: Released slot %d (active: %d)"),
		Handle.Index, ActiveCount);
}

FSeinEntity* FSeinEntityPool::Get(FSeinEntityHandle Handle)
{
	if (!Handle.IsValid()
		|| !Entities.IsValidIndex(Handle.Index)
		|| Generations[Handle.Index] != Handle.Generation
		|| !Entities[Handle.Index].IsAlive())
	{
		return nullptr;
	}
	TouchSlot(Handle.Index);
	return &Entities[Handle.Index];
}

FSeinEntity* FSeinEntityPool::GetForDeferredMutation(
	FSeinEntityHandle Handle)
{
	if (!Handle.IsValid()
		|| !Entities.IsValidIndex(Handle.Index)
		|| Generations[Handle.Index] != Handle.Generation
		|| !Entities[Handle.Index].IsAlive())
	{
		return nullptr;
	}
	return &Entities[Handle.Index];
}

void FSeinEntityPool::CommitDeferredMutation(
	FSeinEntityHandle Handle)
{
	if (IsValid(Handle))
	{
		TouchSlot(Handle.Index);
	}
}

const FSeinEntity* FSeinEntityPool::Get(FSeinEntityHandle Handle) const
{
	if (!Handle.IsValid()
		|| !Entities.IsValidIndex(Handle.Index)
		|| Generations[Handle.Index] != Handle.Generation
		|| !Entities[Handle.Index].IsAlive())
	{
		return nullptr;
	}
	return &Entities[Handle.Index];
}

bool FSeinEntityPool::IsValid(FSeinEntityHandle Handle) const
{
	return Handle.IsValid()
		&& Entities.IsValidIndex(Handle.Index)
		&& Generations[Handle.Index] == Handle.Generation
		&& Entities[Handle.Index].IsAlive();
}

uint64 FSeinEntityPool::GetMutationRevision(
	FSeinEntityHandle Handle) const
{
	return IsValid(Handle)
		&& SlotMutationRevisions.IsValidIndex(Handle.Index)
			? SlotMutationRevisions[Handle.Index]
			: 0;
}

bool FSeinEntityPool::IsDeferredDestroyTombstone(
	FSeinEntityHandle Handle) const
{
	return Handle.IsValid()
		&& Entities.IsValidIndex(Handle.Index)
		&& Generations[Handle.Index] == Handle.Generation
		&& RetiredSlots[Handle.Index] == 0
		&& !Entities[Handle.Index].IsAlive();
}

const FSeinEntity* FSeinEntityPool::GetDeferredDestroyTombstone(
	FSeinEntityHandle Handle) const
{
	return IsDeferredDestroyTombstone(Handle)
		? &Entities[Handle.Index]
		: nullptr;
}

FSeinPlayerID FSeinEntityPool::GetDeferredDestroyOwner(
	FSeinEntityHandle Handle) const
{
	return IsDeferredDestroyTombstone(Handle)
		? OwnerPlayerIDs[Handle.Index]
		: FSeinPlayerID::Neutral();
}

void FSeinEntityPool::ReleaseDeferredDestroy(FSeinEntityHandle Handle)
{
	if (!IsDeferredDestroyTombstone(Handle))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("EntityPool::ReleaseDeferredDestroy rejected non-tombstone handle %s"),
			*Handle.ToString());
		return;
	}

	Entities[Handle.Index].Flags = 0;
	OwnerPlayerIDs[Handle.Index] = FSeinPlayerID::Neutral();
	if (TryAdvanceGeneration(Generations[Handle.Index]))
	{
		FreeList.Push(Handle.Index);
	}
	else
	{
		RetiredSlots[Handle.Index] = 1;
		UE_LOG(LogTemp, Log,
			TEXT("EntityPool: Retired exhausted tombstone slot %d"),
			Handle.Index);
	}
	ActiveCount--;
	TouchSlot(Handle.Index);
	BumpTopologyRevision();
}

FSeinPlayerID FSeinEntityPool::GetOwner(FSeinEntityHandle Handle) const
{
	if (!IsValid(Handle))
	{
		return FSeinPlayerID::Neutral();
	}
	return OwnerPlayerIDs[Handle.Index];
}

void FSeinEntityPool::SetOwner(FSeinEntityHandle Handle, FSeinPlayerID NewOwner)
{
	if (!IsValid(Handle))
	{
		UE_LOG(LogTemp, Warning, TEXT("EntityPool::SetOwner — invalid handle %s"), *Handle.ToString());
		return;
	}
	OwnerPlayerIDs[Handle.Index] = NewOwner;
	TouchSlot(Handle.Index);
}

void FSeinEntityPool::Reset()
{
	Entities.Empty();
	Generations.Empty();
	OwnerPlayerIDs.Empty();
	RetiredSlots.Empty();
	SlotMutationRevisions.Empty();
	FreeList.Empty();
	ActiveCount = 0;
	Capacity = 0;
	BumpTopologyRevision();
}

bool FSeinEntityPool::CaptureExactState(
	FSeinEntityPoolExactState& OutState,
	FString& OutError,
	bool bAllowDeferredDestroyTombstones) const
{
	if (Capacity == 0
		&& ActiveCount == 0
		&& Entities.IsEmpty()
		&& Generations.IsEmpty()
		&& OwnerPlayerIDs.IsEmpty()
		&& RetiredSlots.IsEmpty()
		&& FreeList.IsEmpty())
	{
		FSeinEntityPoolExactState EmptyState;
		EmptyState.Slots.SetNum(1);
		EmptyState.Slots[0].Entity.Flags = 0;
		OutState = MoveTemp(EmptyState);
		OutError.Reset();
		return true;
	}

	if (Capacity < 0
		|| Capacity == MAX_int32
		|| Entities.Num() != Capacity + 1
		|| Generations.Num() != Entities.Num()
		|| OwnerPlayerIDs.Num() != Entities.Num()
		|| RetiredSlots.Num() != Entities.Num())
	{
		OutError = TEXT("Entity-pool internal arrays do not match its capacity.");
		return false;
	}

	FSeinEntityPoolExactState Candidate;
	Candidate.Capacity = Capacity;
	Candidate.Slots.SetNum(Entities.Num());
	Candidate.FreeList = FreeList;
	for (int32 SlotIndex = 0; SlotIndex < Entities.Num(); ++SlotIndex)
	{
		if (RetiredSlots[SlotIndex] > 1)
		{
			OutError = FString::Printf(
				TEXT("Entity-pool slot %d has a non-boolean retired marker."),
				SlotIndex);
			return false;
		}
		FSeinEntityPoolSlotState& Slot = Candidate.Slots[SlotIndex];
		Slot.Entity = Entities[SlotIndex];
		Slot.Generation = Generations[SlotIndex];
		Slot.Owner = OwnerPlayerIDs[SlotIndex];
		Slot.bRetired = RetiredSlots[SlotIndex] != 0;
	}

	int32 DerivedActiveCount = 0;
	if (!ValidateExactState(
		Candidate,
		Capacity,
		bAllowDeferredDestroyTombstones,
		DerivedActiveCount,
		OutError))
	{
		return false;
	}
	if (DerivedActiveCount != ActiveCount)
	{
		OutError = FString::Printf(
			TEXT("Entity-pool active count %d does not match derived count %d."),
			ActiveCount,
			DerivedActiveCount);
		return false;
	}

	OutState = MoveTemp(Candidate);
	OutError.Reset();
	return true;
}

bool FSeinEntityPool::TryStageExactState(
	const FSeinEntityPoolExactState& State,
	int32 MaxCapacity,
	FString& OutError,
	bool bAllowDeferredDestroyTombstones)
{
	int32 DerivedActiveCount = 0;
	if (!ValidateExactState(
		State,
		MaxCapacity,
		bAllowDeferredDestroyTombstones,
		DerivedActiveCount,
		OutError))
	{
		return false;
	}

	FSeinEntityPool Staged;
	Staged.Capacity = State.Capacity;
	Staged.ActiveCount = DerivedActiveCount;
	Staged.Entities.SetNum(State.Slots.Num());
	Staged.Generations.SetNum(State.Slots.Num());
	Staged.OwnerPlayerIDs.SetNum(State.Slots.Num());
	Staged.RetiredSlots.SetNum(State.Slots.Num());
	Staged.SlotMutationRevisions.SetNum(State.Slots.Num());
	for (int32 SlotIndex = 0; SlotIndex < State.Slots.Num(); ++SlotIndex)
	{
		const FSeinEntityPoolSlotState& Slot = State.Slots[SlotIndex];
		Staged.Entities[SlotIndex] = Slot.Entity;
		Staged.Generations[SlotIndex] = Slot.Generation;
		Staged.OwnerPlayerIDs[SlotIndex] = Slot.Owner;
		Staged.RetiredSlots[SlotIndex] = Slot.bRetired ? 1 : 0;
		Staged.TouchSlot(SlotIndex);
	}
	Staged.FreeList = State.FreeList;
	Staged.BumpTopologyRevision();

	*this = MoveTemp(Staged);
	OutError.Reset();
	return true;
}

bool FSeinEntityPool::RebuildFromSnapshot(int32 MaxSlotIndex,
	const TArray<int32>& SlotIndices,
	const TArray<int32>& SlotGenerations,
	const TArray<FFixedTransform>& SlotTransforms,
	const TArray<FSeinPlayerID>& SlotOwners,
	const TArray<bool>& SlotAliveFlags)
{
	const int32 N = SlotIndices.Num();
	if (MaxSlotIndex < 0 || MaxSlotIndex == MAX_int32
		|| N != SlotGenerations.Num()
		|| N != SlotTransforms.Num()
		|| N != SlotOwners.Num()
		|| N != SlotAliveFlags.Num())
	{
		UE_LOG(LogTemp, Error,
			TEXT("EntityPool::RebuildFromSnapshot rejected malformed topology."));
		return false;
	}
	TSet<int32> SeenSlots;
	for (int32 i = 0; i < N; ++i)
	{
		if (SlotIndices[i] <= 0 || SlotIndices[i] > MaxSlotIndex
			|| SlotGenerations[i] <= 0 || SeenSlots.Contains(SlotIndices[i]))
		{
			UE_LOG(LogTemp, Error,
				TEXT("EntityPool::RebuildFromSnapshot rejected invalid slot %d generation %d."),
				SlotIndices[i], SlotGenerations[i]);
			return false;
		}
		SeenSlots.Add(SlotIndices[i]);
	}

	FSeinEntityPoolExactState State;
	State.Capacity = MaxSlotIndex;
	State.Slots.SetNum(MaxSlotIndex + 1);
	for (FSeinEntityPoolSlotState& Slot : State.Slots)
	{
		Slot.Entity.Flags = 0;
	}

	for (int32 i = 0; i < N; ++i)
	{
		const int32 Idx = SlotIndices[i];
		FSeinEntityPoolSlotState& Slot = State.Slots[Idx];
		Slot.Generation = SlotGenerations[i];
		Slot.Entity.Transform = SlotTransforms[i];
		Slot.Entity.SetAlive(SlotAliveFlags[i]);
		Slot.Owner = SlotAliveFlags[i]
			? SlotOwners[i]
			: FSeinPlayerID::Neutral();
		Slot.bRetired =
			!SlotAliveFlags[i] && SlotGenerations[i] == MAX_int32;
	}

	// Legacy snapshots did not preserve allocator order. Retain their canonical
	// lower-index-first rebuild while routing all validation through exact state.
	for (int32 i = MaxSlotIndex; i >= 1; --i)
	{
		if (!State.Slots[i].Entity.IsAlive()
			&& !State.Slots[i].bRetired)
		{
			State.FreeList.Push(i);
		}
	}

	FString Error;
	if (!TryStageExactState(State, MaxSlotIndex, Error))
	{
		UE_LOG(LogTemp, Error,
			TEXT("EntityPool::RebuildFromSnapshot rejected state: %s"),
			*Error);
		return false;
	}
	return true;
}

void FSeinEntityPool::Grow(int32 MinCapacity)
{
	const int32 OldTotalSlots = Entities.Num();
	const int32 NewTotalSlots = MinCapacity + 1; // +1 for reserved slot 0

	if (NewTotalSlots <= OldTotalSlots)
	{
		return;
	}

	Entities.SetNum(NewTotalSlots);
	Generations.SetNum(NewTotalSlots);
	OwnerPlayerIDs.SetNum(NewTotalSlots);
	RetiredSlots.SetNum(NewTotalSlots);
	SlotMutationRevisions.SetNumZeroed(NewTotalSlots);

	// Reset leaves no arrays. Re-establish the permanently invalid sentinel
	// before exposing newly grown slots to Acquire.
	if (OldTotalSlots == 0)
	{
		Entities[0].SetAlive(false);
		Entities[0].Flags = 0;
		Generations[0] = 0;
		OwnerPlayerIDs[0] = FSeinPlayerID::Neutral();
		RetiredSlots[0] = 0;
	}

	// Initialize new slots and push to free list (reverse order for LIFO ordering)
	const int32 FirstNewSlot = FMath::Max(OldTotalSlots, 1);
	for (int32 i = NewTotalSlots - 1; i >= FirstNewSlot; --i)
	{
		Entities[i].SetAlive(false);
		Entities[i].Flags = 0;
		Generations[i] = 0;
		OwnerPlayerIDs[i] = FSeinPlayerID::Neutral();
		RetiredSlots[i] = 0;
		FreeList.Push(i);
	}

	Capacity = MinCapacity;
	BumpTopologyRevision();

	UE_LOG(LogTemp, Log, TEXT("EntityPool: Grew from %d to %d slots"), OldTotalSlots, NewTotalSlots);
}

void FSeinEntityPool::TouchSlot(int32 SlotIndex)
{
	if (!SlotMutationRevisions.IsValidIndex(SlotIndex))
	{
		return;
	}
	++MutationRevisionCounter;
	if (MutationRevisionCounter == 0)
	{
		++MutationRevisionCounter;
	}
	SlotMutationRevisions[SlotIndex] = MutationRevisionCounter;
}

void FSeinEntityPool::BumpTopologyRevision()
{
	++TopologyRevision;
	if (TopologyRevision == 0)
	{
		++TopologyRevision;
	}
}

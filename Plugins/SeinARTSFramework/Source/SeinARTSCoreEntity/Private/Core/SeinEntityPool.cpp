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

FSeinEntityPool::FSeinEntityPool()
	: ActiveCount(0)
	, Capacity(0)
{
}

void FSeinEntityPool::Initialize(int32 InitialCapacity)
{
	// +1 because slot 0 is reserved
	const int32 TotalSlots = InitialCapacity + 1;

	Entities.SetNum(TotalSlots);
	Generations.SetNum(TotalSlots);
	OwnerPlayerIDs.SetNum(TotalSlots);
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
}

FSeinEntityHandle FSeinEntityPool::Acquire(
	const FFixedTransform& Transform,
	FSeinPlayerID OwnerID)
{
	if (FreeList.Num() == 0)
	{
		const int32 NewCapacity = FMath::Max(Capacity * 2, 64);
		Grow(NewCapacity);
	}

	const int32 SlotIndex = FreeList.Pop(EAllowShrinking::No);

	// Increment generation (start at 1 so generation 0 is always invalid)
	int32& Gen = Generations[SlotIndex];
	Gen++;
	if (Gen == 0)
	{
		// Wrapped around — skip 0 since it means "invalid"
		Gen = 1;
	}

	FSeinEntity& Entity = Entities[SlotIndex];
	Entity.Transform = Transform;
	Entity.Flags = FSeinEntity::FLAG_ALIVE;
	Entity.ID = FSeinID::Invalid(); // Legacy field, not used in handle-based flow

	OwnerPlayerIDs[SlotIndex] = OwnerID;
	ActiveCount++;

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

	if (Handle.Index >= Entities.Num())
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

	FSeinEntity& Entity = Entities[Handle.Index];
	Entity.Flags = 0; // Clear all flags
	OwnerPlayerIDs[Handle.Index] = FSeinPlayerID::Neutral();

	// Increment generation to invalidate outstanding handles
	int32& Gen = Generations[Handle.Index];
	Gen++;
	if (Gen == 0)
	{
		Gen = 1;
	}

	FreeList.Push(Handle.Index);
	ActiveCount--;

	UE_LOG(LogTemp, Verbose, TEXT("EntityPool: Released slot %d (active: %d)"),
		Handle.Index, ActiveCount);
}

FSeinEntity* FSeinEntityPool::Get(FSeinEntityHandle Handle)
{
	if (!Handle.IsValid()
		|| Handle.Index >= Entities.Num()
		|| Generations[Handle.Index] != Handle.Generation)
	{
		return nullptr;
	}
	return &Entities[Handle.Index];
}

const FSeinEntity* FSeinEntityPool::Get(FSeinEntityHandle Handle) const
{
	if (!Handle.IsValid()
		|| Handle.Index >= Entities.Num()
		|| Generations[Handle.Index] != Handle.Generation)
	{
		return nullptr;
	}
	return &Entities[Handle.Index];
}

bool FSeinEntityPool::IsValid(FSeinEntityHandle Handle) const
{
	return Handle.IsValid()
		&& Handle.Index < Entities.Num()
		&& Generations[Handle.Index] == Handle.Generation;
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
}

void FSeinEntityPool::Reset()
{
	Entities.Empty();
	Generations.Empty();
	OwnerPlayerIDs.Empty();
	FreeList.Empty();
	ActiveCount = 0;
	Capacity = 0;
}

void FSeinEntityPool::RebuildFromSnapshot(int32 MaxSlotIndex,
	const TArray<int32>& SlotIndices,
	const TArray<int32>& SlotGenerations,
	const TArray<FFixedTransform>& SlotTransforms,
	const TArray<FSeinPlayerID>& SlotOwners,
	const TArray<bool>& SlotAliveFlags)
{
	const int32 N = SlotIndices.Num();
	check(N == SlotGenerations.Num());
	check(N == SlotTransforms.Num());
	check(N == SlotOwners.Num());
	check(N == SlotAliveFlags.Num());

	const int32 TotalSlots = MaxSlotIndex + 1;

	Entities.Empty();
	Generations.Empty();
	OwnerPlayerIDs.Empty();
	FreeList.Empty();

	Entities.SetNum(TotalSlots);
	Generations.SetNum(TotalSlots);
	OwnerPlayerIDs.SetNum(TotalSlots);
	FMemory::Memzero(Generations.GetData(), TotalSlots * sizeof(int32));

	// Slot 0 is always reserved.
	Entities[0].SetAlive(false);
	Entities[0].Flags = 0;
	OwnerPlayerIDs[0] = FSeinPlayerID::Neutral();

	// Default-initialize every slot before applying records, so any slot
	// the snapshot didn't include lands in the free list.
	for (int32 i = 1; i < TotalSlots; ++i)
	{
		Entities[i].SetAlive(false);
		Entities[i].Flags = 0;
		OwnerPlayerIDs[i] = FSeinPlayerID::Neutral();
	}

	// Apply records.
	int32 NewActiveCount = 0;
	for (int32 i = 0; i < N; ++i)
	{
		const int32 Idx = SlotIndices[i];
		if (Idx <= 0 || Idx >= TotalSlots) continue;
		Generations[Idx] = SlotGenerations[i];
		Entities[Idx].Transform = SlotTransforms[i];
		Entities[Idx].SetAlive(SlotAliveFlags[i]);
		OwnerPlayerIDs[Idx] = SlotOwners[i];
		if (SlotAliveFlags[i]) ++NewActiveCount;
	}

	// Rebuild the free list: any slot in [1, TotalSlots) that's not alive
	// is free. Push in reverse so lower indices pop first (matches Initialize).
	for (int32 i = TotalSlots - 1; i >= 1; --i)
	{
		if (!Entities[i].IsAlive())
		{
			FreeList.Push(i);
		}
	}

	ActiveCount = NewActiveCount;
	Capacity = MaxSlotIndex;
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

	// Initialize new slots and push to free list (reverse order for LIFO ordering)
	for (int32 i = NewTotalSlots - 1; i >= OldTotalSlots; --i)
	{
		Entities[i].SetAlive(false);
		Entities[i].Flags = 0;
		Generations[i] = 0;
		OwnerPlayerIDs[i] = FSeinPlayerID::Neutral();
		FreeList.Push(i);
	}

	Capacity = MinCapacity;

	UE_LOG(LogTemp, Log, TEXT("EntityPool: Grew from %d to %d slots"), OldTotalSlots, NewTotalSlots);
}

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadComponent.cpp
 * @brief   Pure-read helpers for FSeinSquadComponent. Mutations route through
 *          USeinSimMutationBPFL so member back-refs stay consistent.
 */

#include "Components/SeinSquadComponent.h"

TArray<FSeinEntityHandle> FSeinSquadComponent::GetLiveMembers() const
{
	TArray<FSeinEntityHandle> Out;
	Out.Reserve(Slots.Num());
	for (const FSeinSquadSlot& Slot : Slots)
	{
		if (Slot.CurrentOccupant.IsValid())
		{
			Out.Add(Slot.CurrentOccupant);
		}
	}
	return Out;
}

int32 FSeinSquadComponent::GetLiveMemberCount() const
{
	int32 Count = 0;
	for (const FSeinSquadSlot& Slot : Slots)
	{
		if (Slot.CurrentOccupant.IsValid()) { ++Count; }
	}
	return Count;
}

int32 FSeinSquadComponent::IndexOfSlotByTag(FGameplayTag SlotTag) const
{
	if (!SlotTag.IsValid()) return INDEX_NONE;
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].SlotTags.HasTag(SlotTag)) { return i; }
	}
	return INDEX_NONE;
}

int32 FSeinSquadComponent::IndexOfSlotByMember(FSeinEntityHandle Member) const
{
	if (!Member.IsValid()) return INDEX_NONE;
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].CurrentOccupant == Member) { return i; }
	}
	return INDEX_NONE;
}

int32 FSeinSquadComponent::FindFirstEmptySlotIndex() const
{
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (!Slots[i].CurrentOccupant.IsValid()) { return i; }
	}
	return INDEX_NONE;
}

FFixedVector FSeinSquadComponent::ComputeCentroid(const FFixedVector& Fallback) const
{
	int32 Count = 0;
	FFixedVector Sum = FFixedVector::ZeroVector;
	for (const FSeinSquadSlot& Slot : Slots)
	{
		if (Slot.CurrentOccupant.IsValid())
		{
			// Position of each member is read from sim transforms by callers
			// who have a USeinWorldSubsystem. Helper stays storage-only by
			// summing offsets relative to the squad's recorded centroid;
			// systems with World access compute the true centroid directly.
			Sum = Sum + Slot.OffsetTransform.GetLocation();
			++Count;
		}
	}
	if (Count == 0) { return Fallback; }
	return Sum / FFixedPoint::FromInt(Count);
}

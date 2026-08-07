/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadMutationBPFL.cpp
 * @brief   Implementation of the restricted squad-mutation BPFL.
 */

#include "SeinSquadMutationBPFL.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/ComponentStorage.h"
#include "Core/SeinEntityPool.h"
#include "Types/Entity.h"
#include "Components/SeinCommandBrokerData.h"
#include "Events/SeinVisualEvent.h"
#include "Components/SeinSquadComponent.h"
#include "Components/SeinSquadMemberComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinSquadMutBPFL, Log, All);

USeinWorldSubsystem* USeinSquadMutationBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

// Templated helper: whole-struct write. Bails on invalid handle / missing storage.
namespace
{
	template<typename T>
	bool WriteWholeStruct(const UObject* WorldContextObject, FSeinEntityHandle Handle, const T& NewData, const TCHAR* FnName)
	{
		UWorld* World = WorldContextObject ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
		USeinWorldSubsystem* Subsystem = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
		if (!Subsystem)
		{
			UE_LOG(LogSeinSquadMutBPFL, Warning, TEXT("%s: no SeinWorldSubsystem"), FnName);
			return false;
		}
		if (!Subsystem->RequireStateMutationAuthorization(FnName)) return false;
		T* Dst = Subsystem->GetComponentMutable<T>(Handle);
		if (!Dst)
		{
			UE_LOG(LogSeinSquadMutBPFL, Warning, TEXT("%s: entity %s invalid or has no %s"), FnName, *Handle.ToString(), *T::StaticStruct()->GetName());
			return false;
		}
		*Dst = NewData;
		return true;
	}
}

bool USeinSquadMutationBPFL::SeinSetSquadData(const UObject* WCO, FSeinEntityHandle H, const FSeinSquadComponent& D)           { return WriteWholeStruct(WCO, H, D, TEXT("SetSquadData")); }
bool USeinSquadMutationBPFL::SeinSetSquadMemberData(const UObject* WCO, FSeinEntityHandle H, const FSeinSquadMemberComponent& D) { return WriteWholeStruct(WCO, H, D, TEXT("SetSquadMemberData")); }

// ─── Squad field-level ───
//
// Slot mutations route through SeinFillSquadSlot / SeinEmptySquadSlot — these
// keep the squad's CommandBroker member list, the member's back-refs, and the
// squad's Leader handle all consistent in one operation. The convenience
// SeinAddSquadMember / SeinRemoveSquadMember wrap them for callers that don't
// care about the specific slot.

namespace SeinSquadMutation
{
	// Pick the discriminator tag from a slot's SlotTags container. Convention:
	// the FIRST tag in the container is treated as the canonical discriminator
	// for back-ref purposes. Designers ensure each slot's first tag is unique
	// within the squad (e.g., `Squad.Slot.Sergeant`, `Squad.Slot.Rifle.0`).
	// Returns an empty tag if the container is empty (programmer error —
	// slots without any tag have no stable identity).
	static FGameplayTag GetDiscriminatorTag(const FSeinSquadSlot& Slot)
	{
		for (const FGameplayTag& Tag : Slot.SlotTags)
		{
			return Tag;            // first tag wins
		}
		return FGameplayTag();
	}

	// Strip the back-ref + remove from the squad's broker member list. Called
	// when emptying a slot whose CurrentOccupant is being evicted.
	static void ClearMemberBackref(USeinWorldSubsystem& World, FSeinEntityHandle SquadHandle, FSeinEntityHandle Member)
	{
		if (FSeinSquadMemberComponent* MemberData = World.GetComponentMutable<FSeinSquadMemberComponent>(Member))
		{
			MemberData->SquadEntity = FSeinEntityHandle::Invalid();
			MemberData->SlotTag = FGameplayTag();
		}
		if (FSeinCommandBrokerData* Broker = World.GetComponentMutable<FSeinCommandBrokerData>(SquadHandle))
		{
			const int32 NumBefore = Broker->Members.Num();
			Broker->Members.Remove(Member);
			if (Broker->Members.Num() != NumBefore) { Broker->bCapabilityMapDirty = true; }
		}
	}

	// Set the back-ref + add to the squad's broker member list. Called when
	// filling a slot with a new occupant.
	static void SetMemberBackref(USeinWorldSubsystem& World, FSeinEntityHandle SquadHandle, FGameplayTag SlotTag, FSeinEntityHandle Member)
	{
		if (FSeinSquadMemberComponent* MemberData = World.GetComponentMutable<FSeinSquadMemberComponent>(Member))
		{
			MemberData->SquadEntity = SquadHandle;
			MemberData->SlotTag = SlotTag;
		}
		if (FSeinCommandBrokerData* Broker = World.GetComponentMutable<FSeinCommandBrokerData>(SquadHandle))
		{
			Broker->Members.AddUnique(Member);
			Broker->bCapabilityMapDirty = true;
		}
	}

	// If the leader slot is empty, walk slots in declaration order and pick
	// the first live occupant as the new leader. Emits SquadLeaderChanged
	// only when the leader handle actually changes.
	static void PromoteLeaderIfNeeded(USeinWorldSubsystem& World, FSeinEntityHandle SquadHandle, FSeinSquadComponent& Squad)
	{
		const bool bLeaderInvalid = !Squad.Leader.IsValid()
			|| !World.GetEntityPool().IsValid(Squad.Leader)
			|| Squad.IndexOfSlotByMember(Squad.Leader) == INDEX_NONE;
		if (!bLeaderInvalid) { return; }

		FSeinEntityHandle NewLeader = FSeinEntityHandle::Invalid();
		for (const FSeinSquadSlot& Slot : Squad.Slots)
		{
			if (Slot.CurrentOccupant.IsValid() && World.GetEntityPool().IsValid(Slot.CurrentOccupant))
			{
				NewLeader = Slot.CurrentOccupant;
				break;
			}
		}
		if (NewLeader != Squad.Leader)
		{
			Squad.Leader = NewLeader;
			World.EnqueueVisualEvent(FSeinVisualEvent::MakeSquadLeaderChangedEvent(SquadHandle, NewLeader));
		}
	}
}

bool USeinSquadMutationBPFL::SeinSetSquadLeader(const UObject* WCO, FSeinEntityHandle SquadHandle, FSeinEntityHandle NewLeader)
{
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	if (!S->RequireStateMutationAuthorization(TEXT("SetSquadLeader"))) return false;
	FSeinSquadComponent* D = S->GetComponentMutable<FSeinSquadComponent>(SquadHandle);
	if (!D) { UE_LOG(LogSeinSquadMutBPFL, Warning, TEXT("SetSquadLeader: squad %s has no FSeinSquadComponent"), *SquadHandle.ToString()); return false; }
	if (NewLeader.IsValid() && D->IndexOfSlotByMember(NewLeader) == INDEX_NONE)
	{
		UE_LOG(LogSeinSquadMutBPFL, Warning, TEXT("SetSquadLeader: candidate %s is not in any slot of squad %s"), *NewLeader.ToString(), *SquadHandle.ToString());
		return false;
	}
	if (D->Leader != NewLeader)
	{
		D->Leader = NewLeader;
		S->EnqueueVisualEvent(FSeinVisualEvent::MakeSquadLeaderChangedEvent(SquadHandle, NewLeader));
	}
	return true;
}

bool USeinSquadMutationBPFL::SeinFillSquadSlot(const UObject* WCO, FSeinEntityHandle SquadHandle, FGameplayTag SlotTag, FSeinEntityHandle Member)
{
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	if (!S->RequireStateMutationAuthorization(TEXT("FillSquadSlot"))) return false;
	if (!Member.IsValid())
	{
		UE_LOG(LogSeinSquadMutBPFL, Warning, TEXT("FillSquadSlot: invalid member handle (squad=%s, slot=%s)"), *SquadHandle.ToString(), *SlotTag.ToString());
		return false;
	}
	FSeinSquadComponent* Squad = S->GetComponentMutable<FSeinSquadComponent>(SquadHandle);
	if (!Squad) { UE_LOG(LogSeinSquadMutBPFL, Warning, TEXT("FillSquadSlot: squad %s has no FSeinSquadComponent"), *SquadHandle.ToString()); return false; }

	const int32 SlotIdx = Squad->IndexOfSlotByTag(SlotTag);
	if (SlotIdx == INDEX_NONE)
	{
		UE_LOG(LogSeinSquadMutBPFL, Warning, TEXT("FillSquadSlot: squad %s has no slot tagged '%s'"), *SquadHandle.ToString(), *SlotTag.ToString());
		return false;
	}
	FSeinSquadSlot& Slot = Squad->Slots[SlotIdx];

	// Evict prior occupant if any.
	if (Slot.CurrentOccupant.IsValid() && Slot.CurrentOccupant != Member)
	{
		SeinSquadMutation::ClearMemberBackref(*S, SquadHandle, Slot.CurrentOccupant);
	}

	Slot.CurrentOccupant = Member;
	SeinSquadMutation::SetMemberBackref(*S, SquadHandle, SlotTag, Member);

	S->EnqueueVisualEvent(FSeinVisualEvent::MakeSquadMemberAddedEvent(SquadHandle, Member, SlotTag));

	// Promote a leader if there isn't one (first occupant fills the leader role).
	SeinSquadMutation::PromoteLeaderIfNeeded(*S, SquadHandle, *Squad);
	return true;
}

bool USeinSquadMutationBPFL::SeinEmptySquadSlot(const UObject* WCO, FSeinEntityHandle SquadHandle, FGameplayTag SlotTag)
{
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	if (!S->RequireStateMutationAuthorization(TEXT("EmptySquadSlot"))) return false;
	FSeinSquadComponent* Squad = S->GetComponentMutable<FSeinSquadComponent>(SquadHandle);
	if (!Squad) { UE_LOG(LogSeinSquadMutBPFL, Warning, TEXT("EmptySquadSlot: squad %s has no FSeinSquadComponent"), *SquadHandle.ToString()); return false; }

	const int32 SlotIdx = Squad->IndexOfSlotByTag(SlotTag);
	if (SlotIdx == INDEX_NONE)
	{
		UE_LOG(LogSeinSquadMutBPFL, Warning, TEXT("EmptySquadSlot: squad %s has no slot tagged '%s'"), *SquadHandle.ToString(), *SlotTag.ToString());
		return false;
	}
	FSeinSquadSlot& Slot = Squad->Slots[SlotIdx];
	if (!Slot.CurrentOccupant.IsValid()) { return false; }

	const FSeinEntityHandle Evicted = Slot.CurrentOccupant;
	SeinSquadMutation::ClearMemberBackref(*S, SquadHandle, Evicted);
	Slot.CurrentOccupant = FSeinEntityHandle::Invalid();

	// If the evicted member was the leader, promote the next live occupant.
	if (Squad->Leader == Evicted)
	{
		Squad->Leader = FSeinEntityHandle::Invalid();
		SeinSquadMutation::PromoteLeaderIfNeeded(*S, SquadHandle, *Squad);
	}
	return true;
}

bool USeinSquadMutationBPFL::SeinAddSquadMember(const UObject* WCO, FSeinEntityHandle SquadHandle, FSeinEntityHandle NewMember)
{
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	if (!S->RequireStateMutationAuthorization(TEXT("AddSquadMember"))) return false;
	FSeinSquadComponent* Squad = S->GetComponentMutable<FSeinSquadComponent>(SquadHandle);
	if (!Squad) { UE_LOG(LogSeinSquadMutBPFL, Warning, TEXT("AddSquadMember: squad %s has no FSeinSquadComponent"), *SquadHandle.ToString()); return false; }

	const int32 EmptyIdx = Squad->FindFirstEmptySlotIndex();
	if (EmptyIdx == INDEX_NONE)
	{
		UE_LOG(LogSeinSquadMutBPFL, Warning, TEXT("AddSquadMember: squad %s has no empty slots"), *SquadHandle.ToString());
		return false;
	}
	const FGameplayTag SlotTag = SeinSquadMutation::GetDiscriminatorTag(Squad->Slots[EmptyIdx]);
	if (!SlotTag.IsValid())
	{
		UE_LOG(LogSeinSquadMutBPFL, Warning, TEXT("AddSquadMember: squad %s slot index %d has no SlotTags (no discriminator)"), *SquadHandle.ToString(), EmptyIdx);
		return false;
	}
	return SeinFillSquadSlot(WCO, SquadHandle, SlotTag, NewMember);
}

bool USeinSquadMutationBPFL::SeinRemoveSquadMember(const UObject* WCO, FSeinEntityHandle SquadHandle, FSeinEntityHandle MemberToRemove)
{
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	if (!S->RequireStateMutationAuthorization(TEXT("RemoveSquadMember"))) return false;
	FSeinSquadComponent* Squad = S->GetComponentMutable<FSeinSquadComponent>(SquadHandle);
	if (!Squad) { UE_LOG(LogSeinSquadMutBPFL, Warning, TEXT("RemoveSquadMember: squad %s has no FSeinSquadComponent"), *SquadHandle.ToString()); return false; }

	const int32 SlotIdx = Squad->IndexOfSlotByMember(MemberToRemove);
	if (SlotIdx == INDEX_NONE)
	{
		UE_LOG(LogSeinSquadMutBPFL, Warning, TEXT("RemoveSquadMember: member %s not found in squad %s"), *MemberToRemove.ToString(), *SquadHandle.ToString());
		return false;
	}
	const FGameplayTag SlotTag = SeinSquadMutation::GetDiscriminatorTag(Squad->Slots[SlotIdx]);
	if (!SlotTag.IsValid())
	{
		UE_LOG(LogSeinSquadMutBPFL, Warning, TEXT("RemoveSquadMember: squad %s slot index %d has no SlotTags (no discriminator)"), *SquadHandle.ToString(), SlotIdx);
		return false;
	}
	return SeinEmptySquadSlot(WCO, SquadHandle, SlotTag);
}

bool USeinSquadMutationBPFL::SeinSetSlotOffsetTransform(const UObject* WCO, FSeinEntityHandle SquadHandle, FGameplayTag SlotTag, FFixedTransform NewOffset)
{
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	if (!S->RequireStateMutationAuthorization(TEXT("SetSlotOffsetTransform"))) return false;
	FSeinSquadComponent* Squad = S->GetComponentMutable<FSeinSquadComponent>(SquadHandle);
	if (!Squad) { UE_LOG(LogSeinSquadMutBPFL, Warning, TEXT("SetSlotOffsetTransform: squad %s has no FSeinSquadComponent"), *SquadHandle.ToString()); return false; }

	const int32 SlotIdx = Squad->IndexOfSlotByTag(SlotTag);
	if (SlotIdx == INDEX_NONE)
	{
		UE_LOG(LogSeinSquadMutBPFL, Warning, TEXT("SetSlotOffsetTransform: squad %s has no slot tagged '%s'"), *SquadHandle.ToString(), *SlotTag.ToString());
		return false;
	}
	Squad->Slots[SlotIdx].OffsetTransform = NewOffset;
	return true;
}

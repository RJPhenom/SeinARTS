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
#include "Components/SeinBrokerMembershipData.h"
#include "Events/SeinVisualEvent.h"
#include "Components/SeinSquadComponent.h"
#include "Components/SeinSquadMemberComponent.h"
#include "Reinforcement/SeinSquadReinforcementService.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinSquadMutBPFL, Log, All);

USeinWorldSubsystem* USeinSquadMutationBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

bool USeinSquadMutationBPFL::SeinSetSquadData(
	const UObject* WCO,
	FSeinEntityHandle H,
	const FSeinSquadComponent& D)
{
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S || !S->RequireStateMutationAuthorization(TEXT("SetSquadData")))
	{
		return false;
	}
	const FSeinSquadComponent* Existing =
		S->GetComponent<FSeinSquadComponent>(H);
	const bool bExistingRuntimeState = !Existing
		|| Existing->Leader.IsValid()
		|| !Existing->ReinforceQueue.IsEmpty()
		|| Existing->NextReinforceRequestID != 1
		|| Existing->Slots.ContainsByPredicate(
			[](const FSeinSquadSlot& Slot)
			{
				return Slot.CurrentOccupant.IsValid()
					|| Slot.CurrentCooldown != FFixedPoint::Zero;
			});
	const bool bNewRuntimeState = D.Leader.IsValid()
		|| !D.ReinforceQueue.IsEmpty()
		|| D.NextReinforceRequestID != 1
		|| D.Slots.ContainsByPredicate(
			[](const FSeinSquadSlot& Slot)
			{
				return Slot.CurrentOccupant.IsValid()
					|| Slot.CurrentCooldown != FFixedPoint::Zero;
			});
	if (bExistingRuntimeState || bNewRuntimeState
		|| S->GetComponent<FSeinCommandBrokerData>(H))
	{
		UE_LOG(LogSeinSquadMutBPFL, Warning,
			TEXT("SetSquadData: live squad topology cannot be replaced; use exact field-level APIs."));
		return false;
	}
	FSeinSquadComponent* Mutable =
		S->GetComponentMutable<FSeinSquadComponent>(H);
	*Mutable = D;
	return true;
}

bool USeinSquadMutationBPFL::SeinSetSquadMemberData(
	const UObject* WCO,
	FSeinEntityHandle H,
	const FSeinSquadMemberComponent& D)
{
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S || !S->RequireStateMutationAuthorization(TEXT("SetSquadMemberData")))
	{
		return false;
	}
	const FSeinSquadMemberComponent* Existing =
		S->GetComponent<FSeinSquadMemberComponent>(H);
	const FSeinBrokerMembershipData* BrokerMembership =
		S->GetComponent<FSeinBrokerMembershipData>(H);
	if (!Existing
		|| Existing->SquadEntity.IsValid()
		|| Existing->SlotIndex != INDEX_NONE
		|| Existing->SlotTag.IsValid()
		|| D.SquadEntity.IsValid()
		|| D.SlotIndex != INDEX_NONE
		|| D.SlotTag.IsValid()
		|| (BrokerMembership
			&& BrokerMembership->CurrentBrokerHandle.IsValid()))
	{
		UE_LOG(LogSeinSquadMutBPFL, Warning,
			TEXT("SetSquadMemberData: assigned membership cannot be replaced; use squad slot APIs."));
		return false;
	}
	FSeinSquadMemberComponent* Mutable =
		S->GetComponentMutable<FSeinSquadMemberComponent>(H);
	*Mutable = D;
	return true;
}

// ─── Squad field-level ───
//
// Slot mutations route through SeinFillSquadSlot / SeinEmptySquadSlot — these
// keep the squad's CommandBroker member list, the member's back-refs, and the
// squad's Leader handle all consistent in one operation. The convenience
// SeinAddSquadMember / SeinRemoveSquadMember wrap them for callers that don't
// care about the specific slot.

namespace SeinSquadMutation
{
	// Slot tags are metadata; choose one canonically for events/back-refs.
	static FGameplayTag GetDiscriminatorTag(const FSeinSquadSlot& Slot)
	{
		return FSeinSquadReinforcementService::ResolveCanonicalSlotTag(Slot);
	}

	static void InvalidateSettledMemberLayout(FSeinCommandBrokerData& Broker)
	{
		Broker.SettledSlotPositions.Reset();
		Broker.SettledSlotFacings.Reset();
		Broker.bSettledSlotsMemberAligned = false;
		Broker.NextReseekAllowedTick = 0;
		Broker.ReseekEpisodeStartTick = 0;
	}

	// Strip the back-ref + remove from the squad's broker member list. Called
	// when emptying a slot whose CurrentOccupant is being evicted.
	static void ClearMemberBackref(USeinWorldSubsystem& World, FSeinEntityHandle SquadHandle, FSeinEntityHandle Member)
	{
		if (FSeinSquadMemberComponent* MemberData = World.GetComponentMutable<FSeinSquadMemberComponent>(Member))
		{
			MemberData->SquadEntity = FSeinEntityHandle::Invalid();
			MemberData->SlotIndex = INDEX_NONE;
			MemberData->SlotTag = FGameplayTag();
		}
		if (FSeinBrokerMembershipData* Membership =
			World.GetComponentMutable<FSeinBrokerMembershipData>(Member))
		{
			if (Membership->CurrentBrokerHandle == SquadHandle)
			{
				Membership->CurrentBrokerHandle =
					FSeinEntityHandle::Invalid();
				Membership->CohesionGroupId = 0;
			}
		}
		if (FSeinCommandBrokerData* Broker = World.GetComponentMutable<FSeinCommandBrokerData>(SquadHandle))
		{
			const int32 NumBefore = Broker->Members.Num();
			Broker->Members.Remove(Member);
			if (Broker->Members.Num() != NumBefore)
			{
				Broker->bCapabilityMapDirty = true;
				InvalidateSettledMemberLayout(*Broker);
			}
		}
	}

	// Set the back-ref + add to the squad's broker member list. Called when
	// filling a slot with a new occupant.
	static void SetMemberBackref(
		USeinWorldSubsystem& World,
		FSeinEntityHandle SquadHandle,
		int32 SlotIndex,
		FGameplayTag SlotTag,
		FSeinEntityHandle Member)
	{
		if (FSeinSquadMemberComponent* MemberData = World.GetComponentMutable<FSeinSquadMemberComponent>(Member))
		{
			MemberData->SquadEntity = SquadHandle;
			MemberData->SlotIndex = SlotIndex;
			MemberData->SlotTag = SlotTag;
		}
		else
		{
			FSeinSquadMemberComponent NewMemberData;
			NewMemberData.SquadEntity = SquadHandle;
			NewMemberData.SlotIndex = SlotIndex;
			NewMemberData.SlotTag = SlotTag;
			World.AddComponent(Member, NewMemberData);
		}
		if (FSeinBrokerMembershipData* Membership =
			World.GetComponentMutable<FSeinBrokerMembershipData>(Member))
		{
			Membership->CurrentBrokerHandle = SquadHandle;
			Membership->CohesionGroupId = 0;
		}
		else
		{
			FSeinBrokerMembershipData NewMembership;
			NewMembership.CurrentBrokerHandle = SquadHandle;
			World.AddComponent(Member, NewMembership);
		}
		if (FSeinCommandBrokerData* Broker = World.GetComponentMutable<FSeinCommandBrokerData>(SquadHandle))
		{
			const int32 NumBefore = Broker->Members.Num();
			Broker->Members.AddUnique(Member);
			Broker->bCapabilityMapDirty = true;
			if (Broker->Members.Num() != NumBefore)
			{
				InvalidateSettledMemberLayout(*Broker);
			}
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

	static bool FillSlotByIndex(
		USeinWorldSubsystem& World,
		FSeinEntityHandle SquadHandle,
		int32 SlotIndex,
		FSeinEntityHandle Member)
	{
		if (!World.IsEntityAlive(Member))
		{
			UE_LOG(LogSeinSquadMutBPFL, Warning,
				TEXT("FillSquadSlot: member %s is not alive."),
				*Member.ToString());
			return false;
		}
		FSeinSquadComponent* Squad =
			World.GetComponentMutable<FSeinSquadComponent>(SquadHandle);
		if (!Squad || !Squad->Slots.IsValidIndex(SlotIndex))
		{
			return false;
		}

		if (const FSeinSquadMemberComponent* Existing =
			World.GetComponent<FSeinSquadMemberComponent>(Member))
		{
			if (Existing->SquadEntity.IsValid()
				&& Existing->SquadEntity != SquadHandle
				&& World.IsEntityAlive(Existing->SquadEntity))
			{
				UE_LOG(LogSeinSquadMutBPFL, Warning,
					TEXT("FillSquadSlot: member %s already belongs to squad %s."),
					*Member.ToString(), *Existing->SquadEntity.ToString());
				return false;
			}
		}
		if (const FSeinBrokerMembershipData* ExistingMembership =
			World.GetComponent<FSeinBrokerMembershipData>(Member))
		{
			if (ExistingMembership->CurrentBrokerHandle.IsValid()
				&& ExistingMembership->CurrentBrokerHandle != SquadHandle
				&& World.IsEntityAlive(
					ExistingMembership->CurrentBrokerHandle))
			{
				UE_LOG(LogSeinSquadMutBPFL, Warning,
					TEXT("FillSquadSlot: member %s already belongs to broker %s."),
					*Member.ToString(),
					*ExistingMembership->CurrentBrokerHandle.ToString());
				return false;
			}
		}

		const bool bHadQueuedRequest = Squad->ReinforceQueue.ContainsByPredicate(
			[SlotIndex](const FSeinSquadReinforceEntry& Entry)
			{
				return Entry.RequestedSlotIndex == SlotIndex;
			});
		const int32 Cancelled =
			FSeinSquadReinforcementService::CancelForSlot(
				World, SquadHandle, SlotIndex);
		if (bHadQueuedRequest && Cancelled == 0)
		{
			return false;
		}
		FSeinSquadSlot& TargetSlot = Squad->Slots[SlotIndex];
		const FGameplayTag SlotTag = GetDiscriminatorTag(TargetSlot);
		if (TargetSlot.CurrentOccupant == Member)
		{
			SetMemberBackref(
				World, SquadHandle, SlotIndex, SlotTag, Member);
			PromoteLeaderIfNeeded(World, SquadHandle, *Squad);
			return true;
		}

		const int32 PreviousSlotIndex = Squad->IndexOfSlotByMember(Member);
		if (PreviousSlotIndex != INDEX_NONE
			&& PreviousSlotIndex != SlotIndex)
		{
			Squad->Slots[PreviousSlotIndex].CurrentOccupant =
				FSeinEntityHandle::Invalid();
			ClearMemberBackref(World, SquadHandle, Member);
		}
		if (TargetSlot.CurrentOccupant.IsValid())
		{
			ClearMemberBackref(
				World, SquadHandle, TargetSlot.CurrentOccupant);
		}

		TargetSlot.CurrentOccupant = Member;
		SetMemberBackref(
			World, SquadHandle, SlotIndex, SlotTag, Member);
		World.EnqueueVisualEvent(
			FSeinVisualEvent::MakeSquadMemberAddedEvent(
				SquadHandle, Member, SlotTag));
		PromoteLeaderIfNeeded(World, SquadHandle, *Squad);
		return true;
	}

	static bool EmptySlotByIndex(
		USeinWorldSubsystem& World,
		FSeinEntityHandle SquadHandle,
		int32 SlotIndex)
	{
		FSeinSquadComponent* Squad =
			World.GetComponentMutable<FSeinSquadComponent>(SquadHandle);
		if (!Squad || !Squad->Slots.IsValidIndex(SlotIndex))
		{
			return false;
		}
		FSeinSquadSlot& Slot = Squad->Slots[SlotIndex];
		if (!Slot.CurrentOccupant.IsValid()) return false;

		const FSeinEntityHandle Evicted = Slot.CurrentOccupant;
		ClearMemberBackref(World, SquadHandle, Evicted);
		Slot.CurrentOccupant = FSeinEntityHandle::Invalid();
		if (Squad->Leader == Evicted)
		{
			Squad->Leader = FSeinEntityHandle::Invalid();
			PromoteLeaderIfNeeded(World, SquadHandle, *Squad);
		}
		return true;
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
	return SeinSquadMutation::FillSlotByIndex(
		*S, SquadHandle, SlotIdx, Member);
}

bool USeinSquadMutationBPFL::SeinFillSquadSlotByIndex(
	const UObject* WCO,
	FSeinEntityHandle SquadHandle,
	int32 SlotIndex,
	FSeinEntityHandle Member)
{
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S || !S->RequireStateMutationAuthorization(
		TEXT("FillSquadSlotByIndex")))
	{
		return false;
	}
	return SeinSquadMutation::FillSlotByIndex(
		*S, SquadHandle, SlotIndex, Member);
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
	return SeinSquadMutation::EmptySlotByIndex(
		*S, SquadHandle, SlotIdx);
}

bool USeinSquadMutationBPFL::SeinEmptySquadSlotByIndex(
	const UObject* WCO,
	FSeinEntityHandle SquadHandle,
	int32 SlotIndex)
{
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S || !S->RequireStateMutationAuthorization(
		TEXT("EmptySquadSlotByIndex")))
	{
		return false;
	}
	return SeinSquadMutation::EmptySlotByIndex(
		*S, SquadHandle, SlotIndex);
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
	return SeinSquadMutation::FillSlotByIndex(
		*S, SquadHandle, EmptyIdx, NewMember);
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
	return SeinSquadMutation::EmptySlotByIndex(
		*S, SquadHandle, SlotIdx);
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
	if (FSeinCommandBrokerData* Broker =
		S->GetComponentMutable<FSeinCommandBrokerData>(SquadHandle))
	{
		SeinSquadMutation::InvalidateSettledMemberLayout(*Broker);
	}
	return true;
}

bool USeinSquadMutationBPFL::SeinQueueSquadReinforcement(
	const UObject* WCO,
	FSeinEntityHandle SquadHandle,
	int32 SlotIndex,
	int64& OutRequestID)
{
	OutRequestID = 0;
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	return S && FSeinSquadReinforcementService::TryEnqueue(
		*S, SquadHandle, SlotIndex, OutRequestID);
}

bool USeinSquadMutationBPFL::SeinCancelSquadReinforcement(
	const UObject* WCO,
	FSeinEntityHandle SquadHandle,
	int64 RequestID)
{
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	return S && FSeinSquadReinforcementService::CancelByRequestID(
		*S, SquadHandle, RequestID);
}

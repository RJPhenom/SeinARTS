/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinContainmentStateValidation.cpp
 * @author       RJ Macklem
 * @created      13 Aug 2026
 * @latest       13 Aug 2026
 * @brief        Validates reciprocal, bounded, acyclic containment state.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Simulation/SeinContainmentStateValidation.h"

#include "Components/SeinAttachmentSpec.h"
#include "Components/SeinContainmentData.h"
#include "Components/SeinContainmentMemberData.h"

namespace UE::SeinARTSCoreEntity
{
	namespace
	{
		bool Fail(FString& OutError, const FString& Reason)
		{
			OutError = Reason;
			return false;
		}

		bool ValidateContainer(
			FSeinEntityHandle ContainerHandle,
			const FSeinContainmentData& Container,
			TFunctionRef<bool(FSeinEntityHandle)> IsEntityAvailable,
			TFunctionRef<const FSeinContainmentMemberData*(
				FSeinEntityHandle)> FindMember,
			const FSeinAttachmentSpec* Attachment,
			TSet<FSeinEntityHandle>& OutOccupants,
			FString& OutError)
		{
			if (Container.TotalCapacity < 0 || Container.CurrentLoad < 0
				|| Container.CurrentLoad > Container.TotalCapacity)
			{
				return Fail(OutError,
					FString::Printf(TEXT("container %s has invalid capacity/load"),
						*ContainerHandle.ToString()));
			}

			OutOccupants.Reset();
			OutOccupants.Reserve(Container.Occupants.Num());
			int64 CalculatedLoad = 0;
			for (const FSeinEntityHandle Occupant : Container.Occupants)
			{
				const FSeinContainmentMemberData* Member = FindMember(Occupant);
				if (Occupant == ContainerHandle
					|| !IsEntityAvailable(Occupant)
					|| OutOccupants.Contains(Occupant)
					|| !Member
					|| Member->CurrentContainer != ContainerHandle
					|| Member->Size <= 0)
				{
					return Fail(OutError,
						FString::Printf(TEXT("container %s has invalid occupant %s"),
							*ContainerHandle.ToString(), *Occupant.ToString()));
				}
				OutOccupants.Add(Occupant);
				CalculatedLoad += static_cast<int64>(Member->Size);
				if (CalculatedLoad > MAX_int32)
				{
					return Fail(OutError,
						FString::Printf(TEXT("container %s load exceeds int32"),
							*ContainerHandle.ToString()));
				}
			}
			if (CalculatedLoad != Container.CurrentLoad)
			{
				return Fail(OutError,
					FString::Printf(TEXT("container %s load does not match occupants"),
						*ContainerHandle.ToString()));
			}

			if (Container.bTracksVisualSlots)
			{
				if (Container.VisualSlotAssignments.Num() != 0
					&& Container.VisualSlotAssignments.Num()
						!= Container.TotalCapacity)
				{
					return Fail(OutError,
						FString::Printf(TEXT("container %s has invalid visual-slot extent"),
							*ContainerHandle.ToString()));
				}
				TSet<FSeinEntityHandle> VisualOccupants;
				for (int32 SlotIndex = 0;
					SlotIndex < Container.VisualSlotAssignments.Num();
					++SlotIndex)
				{
					const FSeinEntityHandle Occupant =
						Container.VisualSlotAssignments[SlotIndex];
					if (!Occupant.IsValid())
					{
						continue;
					}
					const FSeinContainmentMemberData* Member = FindMember(Occupant);
					if (!OutOccupants.Contains(Occupant)
						|| VisualOccupants.Contains(Occupant)
						|| !Member || Member->VisualSlotIndex != SlotIndex)
					{
						return Fail(OutError,
							FString::Printf(TEXT("container %s has invalid visual-slot assignment"),
								*ContainerHandle.ToString()));
					}
					VisualOccupants.Add(Occupant);
				}
				if (VisualOccupants.Num() != OutOccupants.Num())
				{
					return Fail(OutError,
						FString::Printf(TEXT("container %s does not assign every occupant a visual slot"),
							*ContainerHandle.ToString()));
				}
			}
			else
			{
				if (!Container.VisualSlotAssignments.IsEmpty())
				{
					return Fail(OutError,
						FString::Printf(TEXT("container %s retains disabled visual slots"),
							*ContainerHandle.ToString()));
				}
				for (const FSeinEntityHandle Occupant : OutOccupants)
				{
					const FSeinContainmentMemberData* Member = FindMember(Occupant);
					if (!Member || Member->VisualSlotIndex != INDEX_NONE)
					{
						return Fail(OutError,
							FString::Printf(TEXT("container %s has an occupant with a disabled visual slot"),
								*ContainerHandle.ToString()));
					}
				}
			}

			TSet<FGameplayTag> SlotTags;
			if (Attachment)
			{
				SlotTags.Reserve(Attachment->Slots.Num());
				for (const FSeinAttachmentSlotDef& Slot : Attachment->Slots)
				{
					if (!Slot.SlotTag.IsValid() || SlotTags.Contains(Slot.SlotTag))
					{
						return Fail(OutError,
							FString::Printf(TEXT("container %s has invalid or duplicate attachment slots"),
								*ContainerHandle.ToString()));
					}
					SlotTags.Add(Slot.SlotTag);
				}

				TSet<FSeinEntityHandle> AssignedOccupants;
				for (const auto& Pair : Attachment->Assignments)
				{
					if (!Pair.Key.IsValid() || !SlotTags.Contains(Pair.Key))
					{
						return Fail(OutError,
							FString::Printf(TEXT("container %s has an unknown attachment assignment"),
								*ContainerHandle.ToString()));
					}
					if (!Pair.Value.IsValid())
					{
						continue;
					}
					const FSeinContainmentMemberData* Member = FindMember(Pair.Value);
					if (!OutOccupants.Contains(Pair.Value)
						|| AssignedOccupants.Contains(Pair.Value)
						|| !Member || Member->CurrentSlot != Pair.Key)
					{
						return Fail(OutError,
							FString::Printf(TEXT("container %s has a non-reciprocal attachment assignment"),
								*ContainerHandle.ToString()));
					}
					AssignedOccupants.Add(Pair.Value);
				}
			}

			for (const FSeinEntityHandle Occupant : OutOccupants)
			{
				const FSeinContainmentMemberData* Member = FindMember(Occupant);
				if (!Member || !Member->CurrentSlot.IsValid())
				{
					continue;
				}
				const FSeinEntityHandle* Assignment = Attachment
					? Attachment->Assignments.Find(Member->CurrentSlot)
					: nullptr;
				if (!Assignment || *Assignment != Occupant)
				{
					return Fail(OutError,
						FString::Printf(TEXT("member %s has a non-reciprocal attachment slot"),
							*Occupant.ToString()));
				}
			}

			return true;
		}
	}

	bool ValidateContainmentContainer(
		FSeinEntityHandle ContainerHandle,
		const FSeinContainmentData& Container,
		TFunctionRef<bool(FSeinEntityHandle)> IsEntityAvailable,
		TFunctionRef<const FSeinContainmentMemberData*(FSeinEntityHandle)>
			FindMember,
		const FSeinAttachmentSpec* Attachment,
		FString& OutError)
	{
		OutError.Reset();
		TSet<FSeinEntityHandle> Occupants;
		return ValidateContainer(
			ContainerHandle,
			Container,
			IsEntityAvailable,
			FindMember,
			Attachment,
			Occupants,
			OutError);
	}

	bool ValidateContainmentState(
		TConstArrayView<FSeinEntityHandle> Entities,
		TFunctionRef<bool(FSeinEntityHandle)> IsEntityValid,
		TFunctionRef<const FSeinContainmentData*(FSeinEntityHandle)>
			FindContainer,
		TFunctionRef<const FSeinContainmentMemberData*(FSeinEntityHandle)>
			FindMember,
		TFunctionRef<const FSeinAttachmentSpec*(FSeinEntityHandle)>
			FindAttachment,
		FString& OutError)
	{
		OutError.Reset();
		TSet<FSeinEntityHandle> AliveEntities;
		AliveEntities.Reserve(Entities.Num());
		for (const FSeinEntityHandle Entity : Entities)
		{
			if (!Entity.IsValid() || !IsEntityValid(Entity)
				|| AliveEntities.Contains(Entity))
			{
				return Fail(OutError,
					FString::Printf(TEXT("invalid or duplicate live entity %s"),
						*Entity.ToString()));
			}
			AliveEntities.Add(Entity);
		}

		TMap<FSeinEntityHandle, FSeinEntityHandle> ParentByMember;
		ParentByMember.Reserve(Entities.Num());
		for (const FSeinEntityHandle Entity : Entities)
		{
			const FSeinContainmentMemberData* Member = FindMember(Entity);
			if (!Member)
			{
				continue;
			}
			if (Member->Size <= 0)
			{
				return Fail(OutError,
					FString::Printf(TEXT("member %s has non-positive size"),
						*Entity.ToString()));
			}
			if (!Member->CurrentContainer.IsValid())
			{
				if (Member->CurrentSlot.IsValid()
					|| Member->VisualSlotIndex != INDEX_NONE)
				{
					return Fail(OutError,
						FString::Printf(TEXT("uncontained member %s retains slot state"),
							*Entity.ToString()));
				}
				continue;
			}
			const FSeinContainmentData* Parent =
				FindContainer(Member->CurrentContainer);
			if (Member->CurrentContainer == Entity
				|| !AliveEntities.Contains(Member->CurrentContainer)
				|| !IsEntityValid(Member->CurrentContainer)
				|| !Parent)
			{
				return Fail(OutError,
					FString::Printf(TEXT("member %s references invalid container %s"),
						*Entity.ToString(),
						*Member->CurrentContainer.ToString()));
			}
			ParentByMember.Add(Entity, Member->CurrentContainer);
		}

		TMap<FSeinEntityHandle, TSet<FSeinEntityHandle>>
			OccupantsByContainer;
		OccupantsByContainer.Reserve(Entities.Num());
		for (const FSeinEntityHandle ContainerHandle : Entities)
		{
			const FSeinContainmentData* Container =
				FindContainer(ContainerHandle);
			if (!Container)
			{
				continue;
			}
			TSet<FSeinEntityHandle> Occupants;
			if (!ValidateContainer(
					ContainerHandle,
					*Container,
					[&AliveEntities](FSeinEntityHandle Handle)
					{
						return AliveEntities.Contains(Handle);
					},
					FindMember,
					FindAttachment(ContainerHandle),
					Occupants,
					OutError))
			{
				return false;
			}
			OccupantsByContainer.Add(
				ContainerHandle, MoveTemp(Occupants));
		}

		for (const auto& Pair : ParentByMember)
		{
			const TSet<FSeinEntityHandle>* Occupants =
				OccupantsByContainer.Find(Pair.Value);
			if (!Occupants || !Occupants->Contains(Pair.Key))
			{
				return Fail(OutError,
					FString::Printf(TEXT("member %s references invalid container %s"),
						*Pair.Key.ToString(), *Pair.Value.ToString()));
			}
		}

		TSet<FSeinEntityHandle> Resolved;
		for (const FSeinEntityHandle Start : Entities)
		{
			if (Resolved.Contains(Start))
			{
				continue;
			}
			TArray<FSeinEntityHandle> Path;
			TSet<FSeinEntityHandle> PathSet;
			FSeinEntityHandle Cursor = Start;
			while (!Resolved.Contains(Cursor))
			{
				const FSeinEntityHandle* Parent = ParentByMember.Find(Cursor);
				if (!Parent)
				{
					break;
				}
				if (PathSet.Contains(Cursor))
				{
					return Fail(OutError,
						FString::Printf(TEXT("containment cycle reaches %s"),
							*Cursor.ToString()));
				}
				PathSet.Add(Cursor);
				Path.Add(Cursor);
				Cursor = *Parent;
			}
			for (const FSeinEntityHandle Entity : Path)
			{
				Resolved.Add(Entity);
			}
			Resolved.Add(Cursor);
		}

		return true;
	}
}

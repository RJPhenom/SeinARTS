/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSelectionBPFL.cpp
 */

#include "Lib/SeinSelectionBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinContainmentMemberData.h"
#include "Input/SeinCommand.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Types/Entity.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinSelection, Log, All);

USeinWorldSubsystem* USeinSelectionBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

void USeinSelectionBPFL::EnqueueObserverCommand(
	USeinWorldSubsystem* World,
	FGameplayTag CommandType,
	FSeinPlayerID PlayerID,
	const TArray<FSeinEntityHandle>& Members,
	int32 GroupIndex,
	int32 ActiveFocusIndex)
{
	if (!World) return;
	FSeinCommand Cmd;
	Cmd.PlayerID = PlayerID;
	Cmd.CommandType = CommandType;
	Cmd.EntityList = Members;
	Cmd.ActiveFocusIndex = ActiveFocusIndex;
	Cmd.QueueIndex = GroupIndex; // repurposed — observer commands never hit production-queue paths
	World->SubmitLocalCommandDraft(Cmd);
}

void USeinSelectionBPFL::SeinReplaceSelection(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const TArray<FSeinEntityHandle>& Members, int32 ActiveFocusIndex)
{
	EnqueueObserverCommand(GetWorldSubsystem(WorldContextObject),
		SeinARTSTags::Command_Type_Observer_Selection_Replaced,
		PlayerID, Members, INDEX_NONE, ActiveFocusIndex);
}

void USeinSelectionBPFL::SeinAddToSelection(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const TArray<FSeinEntityHandle>& Members)
{
	EnqueueObserverCommand(GetWorldSubsystem(WorldContextObject),
		SeinARTSTags::Command_Type_Observer_Selection_Added,
		PlayerID, Members);
}

void USeinSelectionBPFL::SeinRemoveFromSelection(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const TArray<FSeinEntityHandle>& Members)
{
	EnqueueObserverCommand(GetWorldSubsystem(WorldContextObject),
		SeinARTSTags::Command_Type_Observer_Selection_Removed,
		PlayerID, Members);
}

void USeinSelectionBPFL::SeinBindControlGroup(const UObject* WorldContextObject, FSeinPlayerID PlayerID, int32 GroupIndex, const TArray<FSeinEntityHandle>& Members)
{
	EnqueueObserverCommand(GetWorldSubsystem(WorldContextObject),
		SeinARTSTags::Command_Type_Observer_ControlGroup_Assigned,
		PlayerID, Members, GroupIndex);
}

void USeinSelectionBPFL::SeinAddToControlGroup(const UObject* WorldContextObject, FSeinPlayerID PlayerID, int32 GroupIndex, const TArray<FSeinEntityHandle>& Members)
{
	EnqueueObserverCommand(GetWorldSubsystem(WorldContextObject),
		SeinARTSTags::Command_Type_Observer_ControlGroup_AddedTo,
		PlayerID, Members, GroupIndex);
}

void USeinSelectionBPFL::SeinRecallControlGroup(const UObject* WorldContextObject, FSeinPlayerID PlayerID, int32 GroupIndex)
{
	EnqueueObserverCommand(GetWorldSubsystem(WorldContextObject),
		SeinARTSTags::Command_Type_Observer_ControlGroup_Selected,
		PlayerID, {}, GroupIndex);
}

bool USeinSelectionBPFL::SeinIsSelectable(const UObject* WorldContextObject, FSeinEntityHandle Entity)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return false;
	const FSeinEntity* E = Sub->GetEntity(Entity);
	return E && E->IsSelectable();
}

void USeinSelectionBPFL::SeinSetSelectable(const UObject* WorldContextObject, FSeinEntityHandle Entity, bool bSelectable)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub
		|| !Sub->RequireStateMutationAuthorization(TEXT("SetSelectable")))
	{
		return;
	}
	if (FSeinEntity* E = Sub->GetEntity(Entity))
	{
		E->SetSelectable(bSelectable);
	}
}

TArray<FSeinEntityHandle> USeinSelectionBPFL::SeinFilterByOwnership(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const TArray<FSeinEntityHandle>& Candidates)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return {};
	TArray<FSeinEntityHandle> Out;
	Out.Reserve(Candidates.Num());
	for (const FSeinEntityHandle& H : Candidates)
	{
		if (!Sub->IsEntityAlive(H)) continue;
		if (Sub->GetEntityOwner(H) != PlayerID) continue;
		Out.Add(H);
	}
	return Out;
}

TArray<FSeinEntityHandle> USeinSelectionBPFL::SeinSelectAllByType(const UObject* WorldContextObject, FGameplayTag IdentityTag)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub || !IdentityTag.IsValid()) return {};
	return Sub->GetEntitiesWithTag(IdentityTag);
}

FSeinMoveableSelectionResult USeinSelectionBPFL::SeinResolveMoveableSelection(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& Selection)
{
	FSeinMoveableSelectionResult Out;
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return Out;

	// Build the container-in-selection set — O(1) lookup in the loop below.
	TSet<FSeinEntityHandle> SelSet(Selection);

	Out.MoveableNow.Reserve(Selection.Num());
	for (const FSeinEntityHandle& Member : Selection)
	{
		const FSeinContainmentMemberData* Mem = Sub->GetComponent<FSeinContainmentMemberData>(Member);
		if (!Mem || !Mem->CurrentContainer.IsValid())
		{
			// Uncontained — moves directly.
			Out.MoveableNow.Add(Member);
			continue;
		}
		if (SelSet.Contains(Mem->CurrentContainer))
		{
			// Container already in selection; skip — the container's move carries this member.
			continue;
		}
		// Contained but the container wasn't selected → disembark pre-command.
		Out.NeedsDisembark.Add(Member);
	}
	return Out;
}

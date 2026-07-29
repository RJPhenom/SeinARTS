/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		SeinLatentActionManager.cpp
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		Latent action manager implementation.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#include "Abilities/SeinLatentActionManager.h"
#include "Abilities/SeinAbility.h"
#include "Abilities/SeinLatentAction.h"
#include "Serialization/SeinStateProviderTransaction.h"
#include "SeinARTSCoreEntityLog.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace
{
	using FActionSnapshot = TArray<TObjectPtr<USeinLatentAction>, TInlineAllocator<16>>;

	FActionSnapshot SnapshotActions(const TArray<TObjectPtr<USeinLatentAction>>& Actions)
	{
		FActionSnapshot Snapshot;
		Snapshot.Append(Actions);
		return Snapshot;
	}
}

bool USeinLatentActionManager::RegisterAction(USeinLatentAction* Action)
{
	if (!Action || ActiveActions.Contains(Action))
	{
		return false;
	}
	USeinAbility* Ability = Action->OwningAbility.Get();
	const USeinWorldSubsystem* ManagerWorld =
		Cast<USeinWorldSubsystem>(GetOuter());
	if (Action->ActionID != 0
		|| Action->AbilityActivationID != 0
		|| NextActionID <= 0
		|| NextActionID == MAX_int64
		|| bHardResetInProgress
		|| FSeinStateProviderTransactionScope::IsActive()
		|| !Ability
		|| !Ability->bIsActive
		|| Ability->GetActivationID() <= 0
		|| Ability->OwnerEntity != Action->OwnerEntity
		|| !ManagerWorld
		|| Ability->WorldSubsystem != ManagerWorld
		|| ManagerWorld->FindAbilityInstanceID(Ability)
			== INDEX_NONE)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("RegisterAction rejected invalid identity, ownership, or exhausted latent-action ID space."));
		return false;
	}
	Action->ActionID = NextActionID++;
	Action->AbilityActivationID = Ability->GetActivationID();
	ActiveActions.Add(Action);
	return true;
}

void USeinLatentActionManager::TickAll(FFixedPoint DeltaTime, USeinWorldSubsystem& World)
{
	// Tick the entry snapshot. Blueprint delegates may synchronously register,
	// cancel, or hard-reset actions; new registrations start next sim tick.
	const FActionSnapshot Snapshot = SnapshotActions(ActiveActions);
	for (USeinLatentAction* Action : Snapshot)
	{
		if (!Action || Action->bCompleted || Action->bCancelled)
		{
			continue;
		}

		const bool bFinished = Action->TickAction(DeltaTime, World);
		// TickAction may synchronously cancel or complete itself (including via a
		// recursive manager cancellation). Preserve that terminal outcome instead
		// of overwriting cancellation with natural completion.
		if (bFinished && !Action->bCompleted && !Action->bCancelled)
		{
			Action->Complete();
		}
	}

	CleanupCompleted();
}

void USeinLatentActionManager::CancelActionsForEntity(FSeinEntityHandle Handle)
{
	const FActionSnapshot Snapshot = SnapshotActions(ActiveActions);
	for (USeinLatentAction* Action : Snapshot)
	{
		if (Action && !Action->bCompleted && !Action->bCancelled && Action->OwnerEntity == Handle)
		{
			Action->Cancel();
		}
	}
}

void USeinLatentActionManager::CancelActionsForEntityOfClass(FSeinEntityHandle Handle, TSubclassOf<USeinLatentAction> ActionClass)
{
	if (!ActionClass) return;
	const FActionSnapshot Snapshot = SnapshotActions(ActiveActions);
	for (USeinLatentAction* Action : Snapshot)
	{
		if (Action && !Action->bCompleted && !Action->bCancelled
			&& Action->OwnerEntity == Handle && Action->IsA(ActionClass))
		{
			Action->Cancel();
		}
	}
}

void USeinLatentActionManager::CancelActionsForAbility(USeinAbility* Ability)
{
	const FActionSnapshot Snapshot = SnapshotActions(ActiveActions);
	for (USeinLatentAction* Action : Snapshot)
	{
		if (Action && !Action->bCompleted && !Action->bCancelled && Action->OwningAbility == Ability)
		{
			Action->Cancel();
		}
	}
}

void USeinLatentActionManager::CancelAllActions()
{
	TGuardValue<bool> HardResetGuard(bHardResetInProgress, true);
	const FActionSnapshot Snapshot = SnapshotActions(ActiveActions);
	// Detach first so callbacks cannot invalidate this pass. Registration is
	// rejected until every cancellation callback has returned.
	ActiveActions.Reset();
	for (USeinLatentAction* Action : Snapshot)
	{
		if (Action && !Action->bCompleted && !Action->bCancelled)
		{
			Action->Cancel();
		}
	}
	// Defensive cleanup for direct mutation by trusted internal code.
	ActiveActions.Reset();
}

void USeinLatentActionManager::AbandonAllForSnapshotRestore()
{
	const FActionSnapshot Snapshot = SnapshotActions(ActiveActions);
	ActiveActions.Reset();
	// OnTimelineAbandoned is a module-owned virtual. Treat cleanup and any
	// destructor it triggers as part of the cross-registry provider transaction.
	FSeinStateProviderTransactionScope ProviderTransaction;
	for (USeinLatentAction* Action : Snapshot)
	{
		if (Action)
		{
			Action->OnTimelineAbandoned();
		}
	}
	// Hooks are silent lifecycle cleanup, not a route to extend the abandoned
	// timeline. Discard any accidental registrations they made.
	ActiveActions.Reset();
}

void USeinLatentActionManager::AdoptRestoredActions(
	TArray<TObjectPtr<USeinLatentAction>>&& Actions,
	int64 InNextActionID)
{
	check(IsInGameThread());
	check(InNextActionID > 0);
	int64 PreviousID = 0;
	for (const USeinLatentAction* Action : Actions)
	{
		check(Action && Action->ActionID > PreviousID
			&& Action->ActionID < InNextActionID);
		PreviousID = Action->ActionID;
	}
	ActiveActions = MoveTemp(Actions);
	NextActionID = InNextActionID;
}

void USeinLatentActionManager::CleanupCompleted()
{
	ActiveActions.RemoveAll([](const TObjectPtr<USeinLatentAction>& Action)
	{
		return !Action || Action->bCompleted || Action->bCancelled;
	});
}

int32 USeinLatentActionManager::GetActiveActionCount() const
{
	return ActiveActions.Num();
}

bool USeinLatentActionManager::HasActiveActionForEntity(FSeinEntityHandle Handle) const
{
	for (const TObjectPtr<USeinLatentAction>& Action : ActiveActions)
	{
		if (Action && !Action->bCompleted && !Action->bCancelled && Action->OwnerEntity == Handle)
		{
			return true;
		}
	}
	return false;
}

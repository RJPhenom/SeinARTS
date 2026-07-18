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
#include "Abilities/SeinLatentAction.h"

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

void USeinLatentActionManager::RegisterAction(USeinLatentAction* Action)
{
	if (Action && !ActiveActions.Contains(Action))
	{
		ActiveActions.Add(Action);
	}
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
	const FActionSnapshot Snapshot = SnapshotActions(ActiveActions);
	// Detach first so callbacks cannot invalidate this pass. A second reset
	// below intentionally discards actions spawned by hard-reset callbacks.
	ActiveActions.Reset();
	for (USeinLatentAction* Action : Snapshot)
	{
		if (Action && !Action->bCompleted && !Action->bCancelled)
		{
			Action->Cancel();
		}
	}
	// Snapshot restore requires a clean slate before the ability pool rebuild.
	ActiveActions.Reset();
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

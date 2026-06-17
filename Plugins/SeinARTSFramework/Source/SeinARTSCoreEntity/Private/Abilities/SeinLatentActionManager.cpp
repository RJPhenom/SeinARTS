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

void USeinLatentActionManager::RegisterAction(USeinLatentAction* Action)
{
	if (Action && !ActiveActions.Contains(Action))
	{
		ActiveActions.Add(Action);
	}
}

void USeinLatentActionManager::TickAll(FFixedPoint DeltaTime, USeinWorldSubsystem& World)
{
	for (USeinLatentAction* Action : ActiveActions)
	{
		if (!Action || Action->bCompleted || Action->bCancelled)
		{
			continue;
		}

		const bool bFinished = Action->TickAction(DeltaTime, World);
		if (bFinished)
		{
			Action->Complete();
		}
	}

	CleanupCompleted();
}

void USeinLatentActionManager::CancelActionsForEntity(FSeinEntityHandle Handle)
{
	for (USeinLatentAction* Action : ActiveActions)
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
	for (USeinLatentAction* Action : ActiveActions)
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
	for (USeinLatentAction* Action : ActiveActions)
	{
		if (Action && !Action->bCompleted && !Action->bCancelled && Action->OwningAbility == Ability)
		{
			Action->Cancel();
		}
	}
}

void USeinLatentActionManager::CancelAllActions()
{
	for (USeinLatentAction* Action : ActiveActions)
	{
		if (Action && !Action->bCompleted && !Action->bCancelled)
		{
			Action->Cancel();
		}
	}
	// Drop the array — snapshot restore wants a clean slate before the
	// ability pool is rebuilt.
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

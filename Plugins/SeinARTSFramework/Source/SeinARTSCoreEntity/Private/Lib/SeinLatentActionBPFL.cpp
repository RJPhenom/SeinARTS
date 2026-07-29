/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLatentActionBPFL.cpp
 */

#include "Lib/SeinLatentActionBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Engine/World.h"

USeinWorldSubsystem* USeinLatentActionBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

void USeinLatentActionBPFL::SeinCancelAllActions(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	if (USeinWorldSubsystem* World = GetWorldSubsystem(WorldContextObject))
	{
		if (World->LatentActionManager
			&& World->RequireStateMutationAuthorization(
				TEXT("CancelAllActions")))
		{
			World->LatentActionManager->CancelActionsForEntity(EntityHandle);
		}
	}
}

void USeinLatentActionBPFL::SeinCancelActionsOfClass(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, TSubclassOf<USeinLatentAction> ActionClass)
{
	if (USeinWorldSubsystem* World = GetWorldSubsystem(WorldContextObject))
	{
		if (World->LatentActionManager
			&& World->RequireStateMutationAuthorization(
				TEXT("CancelActionsOfClass")))
		{
			World->LatentActionManager->CancelActionsForEntityOfClass(EntityHandle, ActionClass);
		}
	}
}

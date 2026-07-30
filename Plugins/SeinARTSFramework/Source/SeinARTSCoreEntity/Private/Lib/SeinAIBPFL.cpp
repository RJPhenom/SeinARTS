/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAIBPFL.cpp
 */

#include "Lib/SeinAIBPFL.h"
#include "AI/SeinAIController.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Input/SeinCommand.h"
#include "Engine/World.h"

USeinWorldSubsystem* USeinAIBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

TArray<FSeinEntityHandle> USeinAIBPFL::SeinQueryAllEntities(const UObject* WorldContextObject, FGameplayTagQuery Query)
{
	TArray<FSeinEntityHandle> Out;
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return Out;

	// Perfect-information walk over every live entity.
	Sub->GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, const FSeinEntity& /*Entity*/)
	{
		if (Query.IsEmpty())
		{
			Out.Add(Handle);
			return;
		}
		if (Query.Matches(Sub->GetEntityTags(Handle)))
		{
			Out.Add(Handle);
		}
	});
	return Out;
}

TArray<FSeinEntityHandle> USeinAIBPFL::SeinQueryVisibleEntitiesForPlayer(const UObject* WorldContextObject, FSeinPlayerID ObserverPlayer, FGameplayTagQuery Query)
{
	// V1: forwards through the all-entities query. Fog-respecting filter is a
	// cross-module call into USeinFogOfWarSubsystem; hooking it up is a polish
	// pass that binds into USeinFogOfWarBPFL when the reader BPFL grows a
	// "is visible for player" predicate entry. Designer-author AI that want
	// fog-respecting behavior can consult `USeinFogOfWarBPFL::SeinIsEntityVisible`
	// on each entity in the meantime.
	(void)ObserverPlayer;
	return SeinQueryAllEntities(WorldContextObject, Query);
}

void USeinAIBPFL::SeinEmitCommand(const UObject* WorldContextObject, const FSeinCommand& Command)
{
	if (USeinAIController* Controller = Cast<USeinAIController>(
		const_cast<UObject*>(WorldContextObject)))
	{
		Controller->EmitCommand(Command);
	}
}

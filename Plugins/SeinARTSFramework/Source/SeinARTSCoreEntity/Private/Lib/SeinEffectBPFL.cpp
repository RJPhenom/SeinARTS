/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEffectBPFL.cpp
 * @brief   Thin Blueprint wrappers over the world subsystem's effect lifecycle
 *          and scope-explicit query APIs.
 */

#include "Lib/SeinEffectBPFL.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Simulation/SeinWorldSubsystem.h"

USeinWorldSubsystem* USeinEffectBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

int64 USeinEffectBPFL::SeinApplyEffect(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle,
	TSubclassOf<USeinEffect> EffectClass, FSeinEntityHandle SourceHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem
		|| !Subsystem->RequireStateMutationAuthorization(TEXT("ApplyEffect")))
	{
		return 0;
	}
	return Subsystem->ApplyEffect(TargetHandle, EffectClass, SourceHandle);
}

void USeinEffectBPFL::SeinRemoveEffect(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, int64 EffectInstanceID)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem
		|| !Subsystem->RequireStateMutationAuthorization(TEXT("RemoveEffect")))
	{
		return;
	}
	Subsystem->RemoveEffect(TargetHandle, EffectInstanceID, /*bByExpiration=*/false);
}

bool USeinEffectBPFL::SeinRemoveEffectByID(const UObject* WorldContextObject, int64 EffectInstanceID)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	return Subsystem
		&& Subsystem->RequireStateMutationAuthorization(TEXT("RemoveEffectByID"))
		&& Subsystem->RemoveEffectByID(EffectInstanceID, /*bByExpiration=*/false);
}

void USeinEffectBPFL::SeinRemoveEffectsWithTag(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FGameplayTag Tag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem
		|| !Subsystem->RequireStateMutationAuthorization(TEXT("RemoveEffectsWithTag")))
	{
		return;
	}
	Subsystem->RemoveInstanceEffectsWithTag(TargetHandle, Tag);
}

bool USeinEffectBPFL::SeinHasEffectWithTag(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FGameplayTag Tag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	return Subsystem->HasInstanceEffectWithTag(TargetHandle, Tag);
}

int32 USeinEffectBPFL::SeinGetEffectStacks(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FGameplayTag EffectTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return 0;

	return Subsystem->GetInstanceEffectStacks(TargetHandle, EffectTag);
}

bool USeinEffectBPFL::SeinHasEffectWithTagForPlayer(const UObject* WorldContextObject, FSeinPlayerID PlayerID,
	FGameplayTag EffectTag, ESeinModifierScope Scope)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	return Subsystem && Subsystem->HasEffectWithTagForPlayer(PlayerID, Scope, EffectTag);
}

int32 USeinEffectBPFL::SeinGetEffectStacksForPlayer(const UObject* WorldContextObject, FSeinPlayerID PlayerID,
	FGameplayTag EffectTag, ESeinModifierScope Scope)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	return Subsystem ? Subsystem->GetEffectStacksForPlayer(PlayerID, Scope, EffectTag) : 0;
}

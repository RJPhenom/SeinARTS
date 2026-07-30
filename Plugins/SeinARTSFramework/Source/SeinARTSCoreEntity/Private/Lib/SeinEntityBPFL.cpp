/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEntityBPFL.cpp
 * @brief   Implementation of entity lifecycle and query Blueprint nodes.
 */

#include "Lib/SeinEntityBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Actor/SeinActor.h"

#include "SeinARTSCoreEntityLog.h"  // LogSeinBPFL (module-shared)

USeinWorldSubsystem* USeinEntityBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

FSeinEntityHandle USeinEntityBPFL::SeinSpawnEntity(const UObject* WorldContextObject, TSubclassOf<ASeinActor> ActorClass, const FFixedTransform& SpawnTransform, FSeinPlayerID OwnerPlayerID)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !ActorClass) return FSeinEntityHandle::Invalid();
	return Subsystem->SpawnEntity(ActorClass, SpawnTransform, OwnerPlayerID);
}

void USeinEntityBPFL::SeinDestroyEntity(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return;
	Subsystem->DestroyEntity(EntityHandle);
}

FFixedTransform USeinEntityBPFL::SeinGetEntityTransform(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return FFixedTransform();
	const FSeinEntity* Entity = Subsystem->GetEntity(EntityHandle);
	return Entity ? Entity->Transform : FFixedTransform();
}

void USeinEntityBPFL::SeinSetEntityTransform(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, const FFixedTransform& Transform)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem
		|| !Subsystem->RequireStateMutationAuthorization(TEXT("SetEntityTransform")))
	{
		return;
	}
	FSeinEntity* Entity =
		Subsystem->GetEntityMutable(EntityHandle);
	if (Entity)
	{
		Entity->Transform = Transform;
	}
}

FSeinPlayerID USeinEntityBPFL::SeinGetEntityOwner(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return FSeinPlayerID::Neutral();
	return Subsystem->GetEntityOwner(EntityHandle);
}

bool USeinEntityBPFL::SeinIsEntityAlive(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;
	return Subsystem->IsEntityAlive(EntityHandle);
}

bool USeinEntityBPFL::SeinIsHandleValid(FSeinEntityHandle EntityHandle)
{
	return EntityHandle.IsValid();
}

FSeinEntityHandle USeinEntityBPFL::SeinInvalidHandle()
{
	return FSeinEntityHandle::Invalid();
}

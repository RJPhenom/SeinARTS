/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEntityLookupBPFL.cpp
 * @brief   Implementation of tag-indexed lookups and named-entity registry
 *          Blueprint nodes.
 */

#include "Lib/SeinEntityLookupBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"

USeinWorldSubsystem* USeinEntityLookupBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

TArray<FSeinEntityHandle> USeinEntityLookupBPFL::SeinLookupEntitiesByTag(const UObject* WorldContextObject, FGameplayTag Tag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !Tag.IsValid()) return {};
	return Subsystem->GetEntitiesWithTag(Tag);
}

FSeinEntityHandle USeinEntityLookupBPFL::SeinLookupFirstEntityByTag(const UObject* WorldContextObject, FGameplayTag Tag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !Tag.IsValid()) return FSeinEntityHandle::Invalid();

	if (const TArray<FSeinEntityHandle>* Bucket = Subsystem->FindEntitiesWithTag(Tag))
	{
		if (Bucket->Num() > 0)
		{
			return (*Bucket)[0];
		}
	}
	return FSeinEntityHandle::Invalid();
}

int32 USeinEntityLookupBPFL::SeinCountEntitiesByTag(const UObject* WorldContextObject, FGameplayTag Tag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !Tag.IsValid()) return 0;

	const TArray<FSeinEntityHandle>* Bucket = Subsystem->FindEntitiesWithTag(Tag);
	return Bucket ? Bucket->Num() : 0;
}

bool USeinEntityLookupBPFL::SeinHasAnyEntityWithTag(const UObject* WorldContextObject, FGameplayTag Tag)
{
	return SeinCountEntitiesByTag(WorldContextObject, Tag) > 0;
}

void USeinEntityLookupBPFL::SeinRegisterNamedEntity(const UObject* WorldContextObject, FName Name, FSeinEntityHandle Handle)
{
	if (USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject))
	{
		Subsystem->RegisterNamedEntity(Name, Handle);
	}
}

FSeinEntityHandle USeinEntityLookupBPFL::SeinLookupNamedEntity(const UObject* WorldContextObject, FName Name)
{
	if (USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject))
	{
		return Subsystem->LookupNamedEntity(Name);
	}
	return FSeinEntityHandle::Invalid();
}

void USeinEntityLookupBPFL::SeinUnregisterNamedEntity(const UObject* WorldContextObject, FName Name)
{
	if (USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject))
	{
		Subsystem->UnregisterNamedEntity(Name);
	}
}

TArray<FSeinEntityHandle> USeinEntityLookupBPFL::SeinFindEntities(const UObject* WorldContextObject, FSeinEntityFilterPredicate Predicate)
{
	TArray<FSeinEntityHandle> Result;
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !Predicate.IsBound()) return Result;

	Subsystem->GetEntityPool().ForEachEntity([&Result, &Predicate](FSeinEntityHandle Handle, FSeinEntity& /*Entity*/)
	{
		if (Predicate.Execute(Handle))
		{
			Result.Add(Handle);
		}
	});
	return Result;
}

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinContainmentBPFL.cpp
 */

#include "Lib/SeinContainmentBPFL.h"
#include "Engine/Engine.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinAttachmentSpec.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinContainment, Log, All);

USeinWorldSubsystem* USeinContainmentBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

bool USeinContainmentBPFL::SeinEnterContainer(const UObject* WorldContextObject, FSeinEntityHandle Entity, FSeinEntityHandle Container)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	return Sub ? Sub->EnterContainer(Entity, Container) : false;
}

bool USeinContainmentBPFL::SeinExitContainer(const UObject* WorldContextObject, FSeinEntityHandle Entity, FFixedVector ExitLocation)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	return Sub ? Sub->ExitContainer(Entity, ExitLocation) : false;
}

bool USeinContainmentBPFL::SeinAttachToSlot(const UObject* WorldContextObject, FSeinEntityHandle Entity, FSeinEntityHandle Container, FGameplayTag SlotTag)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	return Sub ? Sub->AttachToSlot(Entity, Container, SlotTag) : false;
}

bool USeinContainmentBPFL::SeinDetachFromSlot(const UObject* WorldContextObject, FSeinEntityHandle Entity)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	return Sub ? Sub->DetachFromSlot(Entity) : false;
}

bool USeinContainmentBPFL::SeinGetContainmentData(const UObject* WorldContextObject, FSeinEntityHandle Container, FSeinContainmentData& OutData)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return false;
	const FSeinContainmentData* Data = Sub->GetComponent<FSeinContainmentData>(Container);
	if (!Data) return false;
	OutData = *Data;
	return true;
}

bool USeinContainmentBPFL::SeinGetContainmentMemberData(const UObject* WorldContextObject, FSeinEntityHandle Entity, FSeinContainmentMemberData& OutData)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return false;
	const FSeinContainmentMemberData* Data = Sub->GetComponent<FSeinContainmentMemberData>(Entity);
	if (!Data) return false;
	OutData = *Data;
	return true;
}

FSeinEntityHandle USeinContainmentBPFL::SeinGetImmediateContainer(const UObject* WorldContextObject, FSeinEntityHandle Entity)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	return Sub ? Sub->GetImmediateContainer(Entity) : FSeinEntityHandle();
}

FSeinEntityHandle USeinContainmentBPFL::SeinGetRootContainer(const UObject* WorldContextObject, FSeinEntityHandle Entity)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	return Sub ? Sub->GetRootContainer(Entity) : FSeinEntityHandle();
}

bool USeinContainmentBPFL::SeinIsContained(const UObject* WorldContextObject, FSeinEntityHandle Entity)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	return Sub ? Sub->IsContained(Entity) : false;
}

TArray<FSeinEntityHandle> USeinContainmentBPFL::SeinGetOccupants(const UObject* WorldContextObject, FSeinEntityHandle Container)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return {};
	const FSeinContainmentData* Cont = Sub->GetComponent<FSeinContainmentData>(Container);
	return Cont ? Cont->Occupants : TArray<FSeinEntityHandle>{};
}

TArray<FSeinEntityHandle> USeinContainmentBPFL::SeinGetAllNestedOccupants(const UObject* WorldContextObject, FSeinEntityHandle Container)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	return Sub ? Sub->GetAllNestedOccupants(Container) : TArray<FSeinEntityHandle>{};
}

FSeinContainmentTree USeinContainmentBPFL::SeinGetContainmentTree(const UObject* WorldContextObject, FSeinEntityHandle Container)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	return Sub ? Sub->BuildContainmentTree(Container) : FSeinContainmentTree{};
}

FSeinEntityHandle USeinContainmentBPFL::SeinGetSlotOccupant(const UObject* WorldContextObject, FSeinEntityHandle Container, FGameplayTag SlotTag)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return FSeinEntityHandle();
	const FSeinAttachmentSpec* Spec = Sub->GetComponent<FSeinAttachmentSpec>(Container);
	if (!Spec) return FSeinEntityHandle();
	if (const FSeinEntityHandle* Found = Spec->Assignments.Find(SlotTag))
	{
		return *Found;
	}
	return FSeinEntityHandle();
}

bool USeinContainmentBPFL::SeinCanAcceptEntity(const UObject* WorldContextObject, FSeinEntityHandle Container, FSeinEntityHandle Entity)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return false;
	const FSeinContainmentData* Cont = Sub->GetComponent<FSeinContainmentData>(Container);
	const FSeinContainmentMemberData* Mem = Sub->GetComponent<FSeinContainmentMemberData>(Entity);
	if (!Cont || !Mem) return false;
	if (Mem->CurrentContainer.IsValid()) return false; // already contained
	if (Cont->CurrentLoad + Mem->Size > Cont->TotalCapacity) return false;
	if (!Cont->AcceptedEntityQuery.IsEmpty())
	{
		if (!Cont->AcceptedEntityQuery.Matches(Sub->GetEntityTags(Entity))) return false;
	}
	return true;
}

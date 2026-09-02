/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadBPFL.cpp
 * @brief   Implementation of squad query Blueprint nodes.
 */

#include "SeinSquadBPFL.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinSquadPayload.h"
#include "Components/SeinSquadMemberPayload.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinBPFL, Log, All);

USeinWorldSubsystem* USeinSquadBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

bool USeinSquadBPFL::SeinGetSquadData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinSquadPayload& OutData)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) { UE_LOG(LogSeinBPFL, Warning, TEXT("GetSquadData: no SeinWorldSubsystem")); return false; }
	const FSeinSquadPayload* Data = Subsystem->GetComponent<FSeinSquadPayload>(EntityHandle);
	if (!Data) { UE_LOG(LogSeinBPFL, Warning, TEXT("GetSquadData: entity %s invalid or has no FSeinSquadPayload"), *EntityHandle.ToString()); return false; }
	OutData = *Data;
	return true;
}

TArray<FSeinSquadPayload> USeinSquadBPFL::SeinGetSquadDataMany(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& EntityHandles)
{
	TArray<FSeinSquadPayload> Result;
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return Result;
	Result.Reserve(EntityHandles.Num());
	for (const FSeinEntityHandle& Handle : EntityHandles)
	{
		if (const FSeinSquadPayload* Data = Subsystem->GetComponent<FSeinSquadPayload>(Handle)) { Result.Add(*Data); }
		else { UE_LOG(LogSeinBPFL, Warning, TEXT("GetSquadData (batch): skipping %s"), *Handle.ToString()); }
	}
	return Result;
}

bool USeinSquadBPFL::SeinGetSquadMemberData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinSquadMemberPayload& OutData)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) { UE_LOG(LogSeinBPFL, Warning, TEXT("GetSquadMemberData: no SeinWorldSubsystem")); return false; }
	const FSeinSquadMemberPayload* Data = Subsystem->GetComponent<FSeinSquadMemberPayload>(EntityHandle);
	if (!Data) { UE_LOG(LogSeinBPFL, Warning, TEXT("GetSquadMemberData: entity %s invalid or has no FSeinSquadMemberPayload"), *EntityHandle.ToString()); return false; }
	OutData = *Data;
	return true;
}

TArray<FSeinSquadMemberPayload> USeinSquadBPFL::SeinGetSquadMemberDataMany(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& EntityHandles)
{
	TArray<FSeinSquadMemberPayload> Result;
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return Result;
	Result.Reserve(EntityHandles.Num());
	for (const FSeinEntityHandle& Handle : EntityHandles)
	{
		if (const FSeinSquadMemberPayload* Data = Subsystem->GetComponent<FSeinSquadMemberPayload>(Handle)) { Result.Add(*Data); }
		else { UE_LOG(LogSeinBPFL, Warning, TEXT("GetSquadMemberData (batch): skipping %s"), *Handle.ToString()); }
	}
	return Result;
}

TArray<FSeinEntityHandle> USeinSquadBPFL::SeinGetSquadMembers(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return TArray<FSeinEntityHandle>();

	const FSeinSquadPayload* SquadComp = Subsystem->GetComponent<FSeinSquadPayload>(SquadHandle);
	if (!SquadComp) return TArray<FSeinEntityHandle>();

	return SquadComp->GetLiveMembers();
}

FSeinEntityHandle USeinSquadBPFL::SeinGetSquadLeader(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return FSeinEntityHandle::Invalid();

	const FSeinSquadPayload* SquadComp = Subsystem->GetComponent<FSeinSquadPayload>(SquadHandle);
	if (!SquadComp) return FSeinEntityHandle::Invalid();

	return SquadComp->Leader;
}

FSeinEntityHandle USeinSquadBPFL::SeinGetEntitySquad(const UObject* WorldContextObject, FSeinEntityHandle MemberHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return FSeinEntityHandle::Invalid();

	const FSeinSquadMemberPayload* MemberComp = Subsystem->GetComponent<FSeinSquadMemberPayload>(MemberHandle);
	if (!MemberComp) return FSeinEntityHandle::Invalid();

	return MemberComp->SquadEntity;
}

bool USeinSquadBPFL::SeinIsSquadMember(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	return Subsystem->HasComponent<FSeinSquadMemberPayload>(EntityHandle);
}

int32 USeinSquadBPFL::SeinGetSquadSize(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return 0;

	const FSeinSquadPayload* SquadComp = Subsystem->GetComponent<FSeinSquadPayload>(SquadHandle);
	if (!SquadComp) return 0;

	return SquadComp->GetLiveMemberCount();
}

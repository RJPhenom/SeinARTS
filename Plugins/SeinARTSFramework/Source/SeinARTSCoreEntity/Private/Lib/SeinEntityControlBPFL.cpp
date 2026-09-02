/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEntityControlBPFL.cpp
 */

#include "Lib/SeinEntityControlBPFL.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace
{
	bool IsGrantActiveAtTick(const FSeinEntityControlGrant& Grant, int32 AtTick)
	{
		return Grant.GrantID.IsValid()
			&& Grant.Grantee.IsValid()
			&& Grant.StartTick >= 0
			&& (Grant.EndTick == INDEX_NONE || Grant.EndTick > Grant.StartTick)
			&& AtTick >= Grant.StartTick
			&& (Grant.EndTick == INDEX_NONE || AtTick < Grant.EndTick);
	}

	bool GrantAllowsCommand(
		const FSeinEntityControlGrant& Grant, const FGameplayTag& CommandType)
	{
		if (Grant.AllowedCommandTypes.IsEmpty()) return true;
		if (!CommandType.IsValid()) return false;
		for (const FGameplayTag& Allowed : Grant.AllowedCommandTypes)
		{
			if (Allowed.MatchesTagExact(CommandType)) return true;
		}
		return false;
	}

	void NormalizeAllowedCommandTypes(TArray<FGameplayTag>& Tags)
	{
		Tags.RemoveAll([](const FGameplayTag& Tag) { return !Tag.IsValid(); });
		Tags.Sort([](const FGameplayTag& A, const FGameplayTag& B)
		{
			return A.ToString() < B.ToString();
		});
		for (int32 Index = Tags.Num() - 1; Index > 0; --Index)
		{
			if (Tags[Index].MatchesTagExact(Tags[Index - 1]))
			{
				Tags.RemoveAt(Index, 1, EAllowShrinking::No);
			}
		}
	}

	void SortGrantsCanonical(TArray<FSeinEntityControlGrant>& Grants)
	{
		Grants.Sort([](const FSeinEntityControlGrant& A, const FSeinEntityControlGrant& B)
		{
			return A.GrantID < B.GrantID;
		});
	}
}

USeinWorldSubsystem* USeinEntityControlBPFL::GetWorldSubsystem(
	const UObject* WorldContextObject)
{
	if (!WorldContextObject || !GEngine) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(
		WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

FSeinEntityControlGrantID USeinEntityControlBPFL::SeinGrantEntityControl(
	const UObject* WorldContextObject,
	FSeinEntityHandle TargetEntity,
	FSeinPlayerID Grantee,
	const TArray<FGameplayTag>& AllowedCommandTypes,
	int32 StartTick,
	int32 EndTick)
{
	USeinWorldSubsystem* World = GetWorldSubsystem(WorldContextObject);
	if (!World || !World->GetEntityPool().IsValid(TargetEntity) || !Grantee.IsValid())
	{
		return {};
	}
	if (!World->RequireStateMutationAuthorization(TEXT("Grant Entity Control")))
	{
		return {};
	}

	const int32 ResolvedStart = StartTick == INDEX_NONE
		? World->GetCurrentTick()
		: StartTick;
	if (ResolvedStart < 0 || EndTick < INDEX_NONE
		|| (EndTick != INDEX_NONE && EndTick <= ResolvedStart))
	{
		return {};
	}

	FSeinEntityControlPayload* State =
		World->GetComponentMutable<FSeinEntityControlPayload>(
			TargetEntity);
	if (!State)
	{
		World->AddComponent(TargetEntity, FSeinEntityControlPayload());
		State = World->GetComponentMutable<FSeinEntityControlPayload>(
			TargetEntity);
	}
	if (!State || State->NextGrantSerial <= 0)
	{
		return {};
	}

	// Defensive monotonicity: dynamically supplied component data must not let
	// the allocator collide with an already-live serial.
	int64 MaxExistingSerial = 0;
	for (const FSeinEntityControlGrant& Existing : State->Grants)
	{
		if (Existing.GrantID.TargetEntity == TargetEntity)
		{
			if (Existing.GrantID.Serial > MaxExistingSerial)
			{
				MaxExistingSerial = Existing.GrantID.Serial;
			}
		}
	}
	if (State->NextGrantSerial <= MaxExistingSerial)
	{
		if (MaxExistingSerial == MAX_int64)
		{
			State->NextGrantSerial = 0;
			return {};
		}
		State->NextGrantSerial = MaxExistingSerial + 1;
	}

	FSeinEntityControlGrant Grant;
	Grant.GrantID.TargetEntity = TargetEntity;
	Grant.GrantID.Serial = State->NextGrantSerial;
	Grant.Grantee = Grantee;
	Grant.AllowedCommandTypes = AllowedCommandTypes;
	NormalizeAllowedCommandTypes(Grant.AllowedCommandTypes);
	Grant.StartTick = ResolvedStart;
	Grant.EndTick = EndTick;

	if (State->NextGrantSerial == MAX_int64)
	{
		State->NextGrantSerial = 0;
	}
	else
	{
		++State->NextGrantSerial;
	}

	State->Grants.Add(Grant);
	SortGrantsCanonical(State->Grants);
	return Grant.GrantID;
}

bool USeinEntityControlBPFL::SeinRevokeEntityControl(
	const UObject* WorldContextObject,
	FSeinEntityControlGrantID GrantID)
{
	USeinWorldSubsystem* World = GetWorldSubsystem(WorldContextObject);
	if (!World || !GrantID.IsValid()
		|| !World->GetEntityPool().IsValid(GrantID.TargetEntity))
	{
		return false;
	}
	if (!World->RequireStateMutationAuthorization(TEXT("Revoke Entity Control")))
	{
		return false;
	}

	FSeinEntityControlPayload* State =
		World->GetComponentMutable<FSeinEntityControlPayload>(
			GrantID.TargetEntity);
	if (!State) return false;
	const int32 Removed = State->Grants.RemoveAll(
		[&GrantID](const FSeinEntityControlGrant& Grant)
		{
			return Grant.GrantID == GrantID;
		});
	return Removed > 0;
}

int32 USeinEntityControlBPFL::SeinPruneExpiredEntityControlGrants(
	const UObject* WorldContextObject,
	FSeinEntityHandle TargetEntity)
{
	USeinWorldSubsystem* World = GetWorldSubsystem(WorldContextObject);
	if (!World || !World->GetEntityPool().IsValid(TargetEntity)) return 0;
	if (!World->RequireStateMutationAuthorization(
		TEXT("Prune Entity Control Grants")))
	{
		return 0;
	}

	FSeinEntityControlPayload* State =
		World->GetComponentMutable<FSeinEntityControlPayload>(
			TargetEntity);
	if (!State) return 0;
	const int32 CurrentTick = World->GetCurrentTick();
	return State->Grants.RemoveAll([CurrentTick](const FSeinEntityControlGrant& Grant)
	{
		return Grant.EndTick != INDEX_NONE && Grant.EndTick <= CurrentTick;
	});
}

bool USeinEntityControlBPFL::SeinCanPlayerControlEntity(
	const UObject* WorldContextObject,
	FSeinPlayerID Player,
	FSeinEntityHandle TargetEntity,
	FGameplayTag CommandType)
{
	const USeinWorldSubsystem* World = GetWorldSubsystem(WorldContextObject);
	return World && CanPlayerControlEntityAtTick(
		*World, Player, TargetEntity, CommandType, World->GetCurrentTick());
}

bool USeinEntityControlBPFL::SeinIsEntityControlGrantActive(
	const UObject* WorldContextObject,
	FSeinEntityControlGrantID GrantID)
{
	const USeinWorldSubsystem* World = GetWorldSubsystem(WorldContextObject);
	return World && IsEntityControlGrantActiveAtTick(
		*World, GrantID, World->GetCurrentTick());
}

TArray<FSeinEntityControlGrant> USeinEntityControlBPFL::SeinGetEntityControlGrants(
	const UObject* WorldContextObject,
	FSeinEntityHandle TargetEntity)
{
	const USeinWorldSubsystem* World = GetWorldSubsystem(WorldContextObject);
	if (!World || !World->GetEntityPool().IsValid(TargetEntity)) return {};
	const FSeinEntityControlPayload* State =
		World->GetComponent<FSeinEntityControlPayload>(TargetEntity);
	return State ? State->Grants : TArray<FSeinEntityControlGrant>();
}

bool USeinEntityControlBPFL::CanPlayerControlEntityAtTick(
	const USeinWorldSubsystem& World,
	FSeinPlayerID Player,
	FSeinEntityHandle TargetEntity,
	FGameplayTag CommandType,
	int32 AtTick)
{
	if (!Player.IsValid() || AtTick < 0
		|| !World.GetEntityPool().IsValid(TargetEntity))
	{
		return false;
	}

	if (World.GetEntityOwner(TargetEntity) == Player)
	{
		return true;
	}

	const FSeinEntityControlPayload* State =
		World.GetComponent<FSeinEntityControlPayload>(TargetEntity);
	if (!State) return false;
	for (const FSeinEntityControlGrant& Grant : State->Grants)
	{
		if (Grant.GrantID.TargetEntity == TargetEntity
			&& Grant.Grantee == Player
			&& IsGrantActiveAtTick(Grant, AtTick)
			&& GrantAllowsCommand(Grant, CommandType))
		{
			return true;
		}
	}
	return false;
}

bool USeinEntityControlBPFL::IsEntityControlGrantActiveAtTick(
	const USeinWorldSubsystem& World,
	FSeinEntityControlGrantID GrantID,
	int32 AtTick)
{
	if (!GrantID.IsValid() || AtTick < 0
		|| !World.GetEntityPool().IsValid(GrantID.TargetEntity))
	{
		return false;
	}
	const FSeinEntityControlPayload* State =
		World.GetComponent<FSeinEntityControlPayload>(GrantID.TargetEntity);
	if (!State) return false;
	for (const FSeinEntityControlGrant& Grant : State->Grants)
	{
		if (Grant.GrantID == GrantID)
		{
			return IsGrantActiveAtTick(Grant, AtTick);
		}
	}
	return false;
}

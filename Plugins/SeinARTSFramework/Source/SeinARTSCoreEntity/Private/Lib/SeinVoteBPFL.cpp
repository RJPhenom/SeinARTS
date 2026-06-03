/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVoteBPFL.cpp
 */

#include "Lib/SeinVoteBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Input/SeinCommand.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Engine/World.h"
#include "StructUtils/InstancedStruct.h"

USeinWorldSubsystem* USeinVoteBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

void USeinVoteBPFL::SeinStartVote(const UObject* WorldContextObject, FGameplayTag VoteType, ESeinVoteResolution Resolution, int32 RequiredThreshold, int32 ExpiresInTicks, FSeinPlayerID Initiator)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return;
	FSeinStartVoteCommandPayload Pay;
	Pay.Resolution = Resolution;
	Pay.RequiredThreshold = RequiredThreshold;
	Pay.ExpiresInTicks = ExpiresInTicks;

	FSeinCommand Cmd;
	Cmd.CommandType = SeinARTSTags::Command_Type_StartVote;
	Cmd.AbilityTag = VoteType;
	Cmd.PlayerID = Initiator;
	Cmd.Payload.InitializeAs<FSeinStartVoteCommandPayload>(Pay);
	Sub->EnqueueCommand(Cmd);
}

void USeinVoteBPFL::SeinCastVote(const UObject* WorldContextObject, FGameplayTag VoteType, FSeinPlayerID Voter, int32 VoteValue)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return;
	FSeinCommand Cmd;
	Cmd.CommandType = SeinARTSTags::Command_Type_CastVote;
	Cmd.AbilityTag = VoteType;
	Cmd.PlayerID = Voter;
	Cmd.QueueIndex = VoteValue;
	Sub->EnqueueCommand(Cmd);
}

TArray<FSeinVoteState> USeinVoteBPFL::SeinGetActiveVotes(const UObject* WorldContextObject)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	return Sub ? Sub->GetActiveVotes() : TArray<FSeinVoteState>{};
}

ESeinVoteStatus USeinVoteBPFL::SeinCheckVoteStatus(const UObject* WorldContextObject, FGameplayTag VoteType)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	return Sub ? Sub->GetVoteStatus(VoteType) : ESeinVoteStatus::NotStarted;
}

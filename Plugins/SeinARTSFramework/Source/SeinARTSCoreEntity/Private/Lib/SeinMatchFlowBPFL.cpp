/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMatchFlowBPFL.cpp
 */

#include "Lib/SeinMatchFlowBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Input/SeinCommand.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Engine/World.h"
#include "StructUtils/InstancedStruct.h"

USeinWorldSubsystem* USeinMatchFlowBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

FSeinMatchSettings USeinMatchFlowBPFL::SeinGetMatchSettings(const UObject* WorldContextObject)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	return Sub ? Sub->GetMatchSettings() : FSeinMatchSettings{};
}

ESeinMatchState USeinMatchFlowBPFL::SeinGetMatchState(const UObject* WorldContextObject)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	return Sub ? Sub->GetMatchState() : ESeinMatchState::Lobby;
}

void USeinMatchFlowBPFL::SeinStartMatch(const UObject* WorldContextObject, const FSeinMatchSettings& Settings)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return;
	FSeinCommand Cmd;
	Cmd.CommandType = SeinARTSTags::Command_Type_StartMatch;
	Cmd.Payload.InitializeAs<FSeinMatchSettings>(Settings);
	Sub->EnqueueCommand(Cmd);
}

void USeinMatchFlowBPFL::SeinEndMatch(const UObject* WorldContextObject, FSeinPlayerID Winner, FGameplayTag Reason)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return;
	FSeinCommand Cmd;
	Cmd.CommandType = SeinARTSTags::Command_Type_EndMatch;
	Cmd.PlayerID = Winner;
	Cmd.AbilityTag = Reason;
	Sub->EnqueueCommand(Cmd);
}

void USeinMatchFlowBPFL::SeinRequestPause(const UObject* WorldContextObject, FSeinPlayerID Requester)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return;
	FSeinCommand Cmd;
	Cmd.CommandType = SeinARTSTags::Command_Type_PauseMatchRequest;
	Cmd.PlayerID = Requester;
	Sub->EnqueueCommand(Cmd);
}

void USeinMatchFlowBPFL::SeinRequestResume(const UObject* WorldContextObject, FSeinPlayerID Requester)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return;
	FSeinCommand Cmd;
	Cmd.CommandType = SeinARTSTags::Command_Type_ResumeMatchRequest;
	Cmd.PlayerID = Requester;
	Sub->EnqueueCommand(Cmd);
}

void USeinMatchFlowBPFL::SeinConcedeMatch(const UObject* WorldContextObject, FSeinPlayerID Conceding)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return;
	FSeinCommand Cmd;
	Cmd.CommandType = SeinARTSTags::Command_Type_ConcedeMatch;
	Cmd.PlayerID = Conceding;
	Sub->EnqueueCommand(Cmd);
}

void USeinMatchFlowBPFL::SeinRestartMatch(const UObject* WorldContextObject)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return;
	FSeinCommand Cmd;
	Cmd.CommandType = SeinARTSTags::Command_Type_RestartMatch;
	Sub->EnqueueCommand(Cmd);
}

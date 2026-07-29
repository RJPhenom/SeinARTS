/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMatchFlowBPFL.cpp
 */

#include "Lib/SeinMatchFlowBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Input/SeinCommand.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Engine/World.h"

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

bool USeinMatchFlowBPFL::SeinRegisterBootstrapEvidenceValue(
	const UObject* WorldContextObject,
	FName StableContributorID,
	int32 SchemaVersion,
	const FInstancedStruct& Value,
	FString& OutError)
{
	OutError.Reset();
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub)
	{
		OutError = TEXT("Register Bootstrap Evidence requires a Sein world.");
		return false;
	}
	if (SchemaVersion <= 0)
	{
		OutError = TEXT("Initial-state schema version must be positive.");
		return false;
	}
	return Sub->RegisterCanonicalBootstrapEvidenceValue(
		StableContributorID,
		static_cast<uint32>(SchemaVersion),
		Value,
		OutError);
}

bool USeinMatchFlowBPFL::SeinSetCanonicalStateValue(
	const UObject* WorldContextObject,
	const FSeinCanonicalStateKey& Key,
	const FInstancedStruct& Value,
	FString& OutError)
{
	OutError.Reset();
	USeinWorldSubsystem* Sub =
		GetWorldSubsystem(WorldContextObject);
	if (!Sub)
	{
		OutError = TEXT("Set State Value requires a Sein world.");
		return false;
	}
	return Sub->SetCanonicalStateValue(Key, Value, OutError);
}

bool USeinMatchFlowBPFL::SeinGetCanonicalStateValue(
	const UObject* WorldContextObject,
	const FSeinCanonicalStateKey& Key,
	FInstancedStruct& OutValue)
{
	OutValue.Reset();
	const USeinWorldSubsystem* Sub =
		GetWorldSubsystem(WorldContextObject);
	return Sub && Sub->GetCanonicalStateValue(Key, OutValue);
}

bool USeinMatchFlowBPFL::SeinStartStandaloneSimulation(
	const UObject* WorldContextObject)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	return Sub && Sub->GetWorld()
		&& Sub->GetWorld()->GetNetMode() == NM_Standalone
		&& Sub->StandaloneBootstrapLauncher.IsBound()
		&& Sub->StandaloneBootstrapLauncher.Execute();
}

void USeinMatchFlowBPFL::SeinEndMatch(const UObject* WorldContextObject, FSeinPlayerID Winner, FGameplayTag Reason)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return;
	FSeinCommand Cmd;
	Cmd.CommandType = SeinARTSTags::Command_Type_EndMatch;
	FSeinEndMatchCommandPayload Payload;
	Payload.Winner = Winner;
	Payload.Reason = Reason;
	Cmd.Payload.InitializeAs<FSeinEndMatchCommandPayload>(Payload);
	Sub->SubmitLocalCommandDraft(Cmd, /*bRequestMatchAdministration=*/true);
}

void USeinMatchFlowBPFL::SeinRequestPause(const UObject* WorldContextObject, FSeinPlayerID Requester)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return;
	FSeinCommand Cmd;
	Cmd.CommandType = SeinARTSTags::Command_Type_PauseMatchRequest;
	Cmd.PlayerID = Requester;
	Sub->SubmitLocalCommandDraft(Cmd);
}

void USeinMatchFlowBPFL::SeinRequestResume(const UObject* WorldContextObject, FSeinPlayerID Requester)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return;
	FSeinCommand Cmd;
	Cmd.CommandType = SeinARTSTags::Command_Type_ResumeMatchRequest;
	Cmd.PlayerID = Requester;
	Sub->SubmitLocalCommandDraft(Cmd);
}

void USeinMatchFlowBPFL::SeinConcedeMatch(const UObject* WorldContextObject, FSeinPlayerID Conceding)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return;
	FSeinCommand Cmd;
	Cmd.CommandType = SeinARTSTags::Command_Type_ConcedeMatch;
	Cmd.PlayerID = Conceding;
	Sub->SubmitLocalCommandDraft(Cmd);
}

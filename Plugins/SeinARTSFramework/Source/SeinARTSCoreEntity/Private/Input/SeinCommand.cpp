/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommand.cpp
 * @brief   FSeinCommand factory methods and FSeinCommandBuffer implementation.
 */

#include "Input/SeinCommand.h"
#include "Tags/SeinARTSGameplayTags.h"

// --- FSeinCommand factory methods ---

FSeinCommand FSeinCommand::MakeAbilityCommand(
	FSeinPlayerID Player,
	FSeinEntityHandle Entity,
	FGameplayTag Tag,
	FSeinEntityHandle Target,
	FFixedVector Location)
{
	FSeinCommand Cmd;
	Cmd.PlayerID = Player;
	Cmd.EntityHandle = Entity;
	Cmd.CommandType = SeinARTSTags::Command_Type_ActivateAbility;
	Cmd.AbilityTag = Tag;
	Cmd.TargetEntity = Target;
	Cmd.TargetLocation = Location;
	return Cmd;
}

FSeinCommand FSeinCommand::MakeCancelCommand(
	FSeinPlayerID Player,
	FSeinEntityHandle Entity)
{
	FSeinCommand Cmd;
	Cmd.PlayerID = Player;
	Cmd.EntityHandle = Entity;
	Cmd.CommandType = SeinARTSTags::Command_Type_CancelAbility;
	return Cmd;
}

FSeinCommand FSeinCommand::MakeCancelProductionCommand(
	FSeinPlayerID Player,
	FSeinEntityHandle Producer,
	int32 QueueIndex)
{
	FSeinCommand Cmd;
	Cmd.PlayerID = Player;
	Cmd.EntityHandle = Producer;
	Cmd.CommandType = SeinARTSTags::Command_Type_CancelProduction;
	Cmd.QueueIndex = QueueIndex;
	return Cmd;
}

// MakeProductionCommand + MakeRallyPointCommand removed (refactored 2026-05-05):
// production now flows through MakeAbilityCommand on production-marked
// abilities; rally authoring goes through SA_SetRallyPoint abilities.

FSeinCommand FSeinCommand::MakeAbilityCommandEx(
	FSeinPlayerID Player,
	FSeinEntityHandle Entity,
	FGameplayTag Tag,
	FSeinEntityHandle Target,
	FFixedVector Location,
	bool bQueue,
	FFixedVector FormationEnd)
{
	FSeinCommand Cmd;
	Cmd.PlayerID = Player;
	Cmd.EntityHandle = Entity;
	Cmd.CommandType = SeinARTSTags::Command_Type_ActivateAbility;
	Cmd.AbilityTag = Tag;
	Cmd.TargetEntity = Target;
	Cmd.TargetLocation = Location;
	Cmd.bQueueCommand = bQueue;
	Cmd.AuxLocation = FormationEnd;
	return Cmd;
}

FSeinCommand FSeinCommand::MakePingCommand(
	FSeinPlayerID Player,
	FFixedVector Location,
	FSeinEntityHandle OptionalTarget)
{
	FSeinCommand Cmd;
	Cmd.PlayerID = Player;
	Cmd.CommandType = SeinARTSTags::Command_Type_Ping;
	Cmd.TargetLocation = Location;
	Cmd.TargetEntity = OptionalTarget;
	return Cmd;
}

// --- Observer commands ---

bool FSeinCommand::IsObserverCommand() const
{
	return CommandType.MatchesTag(SeinARTSTags::Command_Type_Observer);
}

FSeinCommand FSeinCommand::MakeCameraUpdateCommand(
	FSeinPlayerID Player,
	FFixedVector PivotLocation,
	FFixedPoint Yaw,
	FFixedPoint ZoomDistance)
{
	FSeinCommand Cmd;
	Cmd.PlayerID = Player;
	Cmd.CommandType = SeinARTSTags::Command_Type_Observer_CameraUpdate;
	Cmd.TargetLocation = PivotLocation;
	Cmd.AuxA = Yaw;
	Cmd.AuxB = ZoomDistance;
	return Cmd;
}

FSeinCommand FSeinCommand::MakeSelectionChangedCommand(
	FSeinPlayerID Player,
	const TArray<FSeinEntityHandle>& SelectedEntities,
	int32 InActiveFocusIndex)
{
	FSeinCommand Cmd;
	Cmd.PlayerID = Player;
	Cmd.CommandType = SeinARTSTags::Command_Type_Observer_SelectionChanged;
	Cmd.EntityList = SelectedEntities;
	Cmd.ActiveFocusIndex = InActiveFocusIndex;
	return Cmd;
}

// --- FSeinCommandBuffer ---

void FSeinCommandBuffer::AddCommand(const FSeinCommand& Cmd)
{
	Commands.Add(Cmd);
}

void FSeinCommandBuffer::Clear()
{
	Commands.Reset();
}

int32 FSeinCommandBuffer::Num() const
{
	return Commands.Num();
}

const TArray<FSeinCommand>& FSeinCommandBuffer::GetCommands() const
{
	return Commands;
}

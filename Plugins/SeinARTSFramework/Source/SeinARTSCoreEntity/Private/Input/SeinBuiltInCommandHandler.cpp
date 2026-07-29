/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file SeinBuiltInCommandHandler.cpp
 */

#include "Input/SeinBuiltInCommandHandler.h"

#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"

bool USeinBuiltInCommandHandler::ExecuteCommand_Implementation(
	USeinWorldSubsystem* World,
	const FSeinCommand& Command,
	FGameplayTag& OutRejectionReason) const
{
	if (!World)
	{
		OutRejectionReason = SeinARTSTags::Command_Reject_Malformed;
		return false;
	}
	return World->ExecuteBuiltInCommand(Command, OutRejectionReason);
}

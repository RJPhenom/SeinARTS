/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinPlayerAbilityBPFL.cpp
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Submits captured inputs using the invoking local player's identity.
 *
 * @disclaimer   Generated with assistance from an AI language model.
 */
#include "Lib/SeinPlayerAbilityBPFL.h"
#include "Player/SeinPlayerController.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"

bool USeinPlayerAbilityBPFL::SubmitAbility(ASeinPlayerController* PC,
	FSeinEntityHandle Entity, FGameplayTag AbilityTag, FSeinEntityHandle TargetEntity,
	FFixedVector TargetLocation, const FSeinAbilityActivationInputs& Inputs)
{
	if (!PC || !PC->IsLocalController() || !PC->SeinPlayerID.IsValid() || !Inputs.IsBounded()) return false;
	USeinWorldSubsystem* Sub = PC->GetWorld()->GetSubsystem<USeinWorldSubsystem>();
	if (!Sub) return false;
	FSeinCommand Command = FSeinCommand::MakeAbilityCommand(PC->SeinPlayerID, Entity, AbilityTag, TargetEntity, TargetLocation);
	Command.ActivationInputs = Inputs;
	return Sub->SubmitLocalCommandDraft(Command);
}

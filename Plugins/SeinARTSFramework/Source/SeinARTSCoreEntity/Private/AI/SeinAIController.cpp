/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAIController.cpp
 */

#include "AI/SeinAIController.h"
#include "Simulation/SeinWorldSubsystem.h"

DEFINE_LOG_CATEGORY(LogSeinAI);

UWorld* USeinAIController::GetWorld() const
{
	if (HasAnyFlags(RF_ClassDefaultObject)) { return nullptr; }
	return WorldSubsystem ? WorldSubsystem->GetWorld() : nullptr;
}

void USeinAIController::EmitCommand(const FSeinCommand& Command)
{
	if (!WorldSubsystem)
	{
		UE_LOG(LogSeinAI, Warning, TEXT("EmitCommand: AI controller %s has no WorldSubsystem (not registered?)"), *GetName());
		return;
	}
	WorldSubsystem->RouteAICommandFromController(this, Command);
}

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAIController.cpp
 */

#include "AI/SeinAIController.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Input/SeinCommand.h"

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
	// The author is expected to stamp PlayerID on the command; we don't
	// silently overwrite because it might legitimately target the Neutral
	// player for e.g. scenario-owner-like emissions.

	// Route through the lockstep wire if a networked-session interceptor is
	// bound (USeinNetSubsystem binds it on the server in NotifyLocalSlotAssigned).
	// Otherwise — Standalone, networking disabled in settings, or any case
	// where the interceptor declines to handle — fall through to direct
	// local enqueue. The AI runs on the host only (DESIGN §16); the
	// interceptor packs the command into the per-turn buffer so every peer
	// receives + applies it deterministically, instead of the host's sim
	// alone seeing it (which would desync immediately).
	if (WorldSubsystem->AIEmitInterceptor.IsBound())
	{
		const bool bRouted = WorldSubsystem->AIEmitInterceptor.Execute(OwnedPlayerID, Command);
		if (bRouted) return;
	}

	WorldSubsystem->EnqueueCommand(Command);
}

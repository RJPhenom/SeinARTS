/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTickPhase.h
 * @brief   Simulation tick phases and system interface.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"

class USeinWorldSubsystem;

/**
 * Phases of the simulation tick, executed in order.
 * Systems register into a phase and are ticked in priority order within that phase.
 */
enum class ESeinTickPhase : uint8
{
	PreTick,            // Cooldowns, effect expiration, modifier cleanup, resource income
	CommandProcessing,  // Dequeue player/AI commands, activate/cancel abilities
	AbilityExecution,   // All active abilities tick via latent action manager, production
	PostTick            // Deferred destroy, pool recycle, state hash computation
};

/**
 * Abstract interface for simulation systems.
 * Systems are registered with the world subsystem and ticked each sim frame.
 */
class SEINARTSCOREENTITY_API ISeinSystem
{
public:
	virtual ~ISeinSystem() = default;

	/** Tick this system for one simulation frame. */
	virtual void Tick(FFixedPoint DeltaTime, USeinWorldSubsystem& World) = 0;

	/** Which phase this system runs in. */
	virtual ESeinTickPhase GetPhase() const = 0;

	/** Priority within phase. Lower = earlier. */
	virtual int32 GetPriority() const = 0;

	/** Debug name for this system. */
	virtual FName GetSystemName() const = 0;
};

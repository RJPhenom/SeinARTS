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
 * Explicit recapture coverage for retained state used by a tick system.
 *
 * Stateless means the system and every implementation object it delegates to
 * retain no future-affecting state outside Core's ordinary snapshot. A system
 * that delegates through persistent module-owned objects names the canonical
 * state contributors that restore those objects. Unspecified is deliberately
 * rejected before tick zero.
 */
enum class ESeinSystemStateCoverage : uint8
{
	Unspecified,
	Stateless,
	CanonicalStateContributors
};

/**
 * Immutable participation contract for one deterministic simulation system.
 *
 * StableSystemID is a globally namespaced, bounded ASCII identifier. Core
 * canonicalizes it case-insensitively when the system registers.
 * ImplementationRevision is positive and must change whenever the system's
 * deterministic behavior changes without another compatibility identity doing
 * so. Phase, priority, and the canonical ID form the total execution order.
 *
 * RequiredCanonicalStateContributorKeys use the frozen
 * "stable-domain/stable-contributor" spelling. They cover persistent state
 * retained by the system or an implementation object it invokes; component,
 * entity-pool, and other state already owned by Core's snapshot is not repeated.
 */
struct SEINARTSCOREENTITY_API FSeinSystemDescriptor
{
	FName StableSystemID = NAME_None;
	uint32 ImplementationRevision = 0;
	ESeinTickPhase Phase = ESeinTickPhase::PreTick;
	int32 Priority = 0;
	ESeinSystemStateCoverage StateCoverage =
		ESeinSystemStateCoverage::Unspecified;
	TArray<FName> RequiredCanonicalStateContributorKeys;

	static FSeinSystemDescriptor Stateless(
		FName StableSystemID,
		uint32 ImplementationRevision,
		ESeinTickPhase Phase,
		int32 Priority)
	{
		FSeinSystemDescriptor Result;
		Result.StableSystemID = StableSystemID;
		Result.ImplementationRevision = ImplementationRevision;
		Result.Phase = Phase;
		Result.Priority = Priority;
		Result.StateCoverage = ESeinSystemStateCoverage::Stateless;
		return Result;
	}

	static FSeinSystemDescriptor WithCanonicalState(
		FName StableSystemID,
		uint32 ImplementationRevision,
		ESeinTickPhase Phase,
		int32 Priority,
		TArray<FName> RequiredContributorKeys)
	{
		FSeinSystemDescriptor Result;
		Result.StableSystemID = StableSystemID;
		Result.ImplementationRevision = ImplementationRevision;
		Result.Phase = Phase;
		Result.Priority = Priority;
		Result.StateCoverage =
			ESeinSystemStateCoverage::CanonicalStateContributors;
		Result.RequiredCanonicalStateContributorKeys =
			MoveTemp(RequiredContributorKeys);
		return Result;
	}

	bool operator==(const FSeinSystemDescriptor& Other) const
	{
		return StableSystemID == Other.StableSystemID
			&& ImplementationRevision == Other.ImplementationRevision
			&& Phase == Other.Phase
			&& Priority == Other.Priority
			&& StateCoverage == Other.StateCoverage
			&& RequiredCanonicalStateContributorKeys
				== Other.RequiredCanonicalStateContributorKeys;
	}

	bool operator!=(const FSeinSystemDescriptor& Other) const
	{
		return !(*this == Other);
	}
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

	/** Frozen identity, revision, and canonical execution position. */
	virtual FSeinSystemDescriptor DescribeSystem() const = 0;
};

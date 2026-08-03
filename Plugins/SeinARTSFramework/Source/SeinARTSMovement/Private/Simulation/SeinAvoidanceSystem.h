/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAvoidanceSystem.h
 * @brief   PreTick delegator for the pluggable local-avoidance layer.
 *
 *          This system is a THIN delegator (mirrors FSeinCollisionResolutionSystem):
 *          it owns no avoidance logic, it just calls the active USeinAvoidance impl's
 *          ComputeAvoidance() once per tick. The impl is chosen via
 *          `USeinARTSCoreSettings::AvoidanceClass` and instantiated + GC-rooted by
 *          USeinMovementSubsystem, which constructs this system with a pointer to it.
 *
 *          Phase: PreTick | Priority: Avoidance (after the CollisionBroadphase rebuild
 *          at pri 5, so neighbour reads are a consistent start-of-tick snapshot; before
 *          movement runs in AbilityExecution, which consumes the output this same tick).
 *
 *          The avoidance MODEL (the lateral-steer boids math) lives in
 *          USeinAvoidanceDefault — see SeinAvoidanceDefault.{h,cpp} and the contract
 *          docstring on USeinAvoidance.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Core/SeinSystemPriority.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Movement/SeinAvoidance.h"
#include "SeinMovementSubsystem.h"
#include "Types/FixedPoint.h"

/**
 * System: Avoidance (local unit-unit steering) — delegator to the active USeinAvoidance.
 */
class FSeinAvoidanceSystem final : public ISeinSystem
{
public:
	/** The avoidance impl is owned + GC-rooted by USeinMovementSubsystem (a UPROPERTY),
	 *  created once at world begin-play and never re-created, so this raw pointer is
	 *  valid for the system's whole lifetime (the subsystem deletes this system before
	 *  releasing the impl). */
	explicit FSeinAvoidanceSystem(
		USeinMovementSubsystem* InOwner,
		USeinAvoidance* InAvoidance)
		: OwnerSubsystem(InOwner)
		, Avoidance(InAvoidance)
	{}

	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		if (Avoidance)
		{
			// Blueprint implementations may update reflected policy variables from
			// ComputeAvoidance, so conservatively invalidate their canonical policy
			// state. Shipped native implementations keep all evolving state in sim
			// components; invalidating their immutable policy object every tick forced
			// a redundant UObject serialization at each root boundary.
			if (!Avoidance->HasImmutableRuntimePolicyState())
			{
				if (USeinMovementSubsystem* Owner = OwnerSubsystem.Get())
				{
					Owner->MarkAvoidanceStateDirty();
				}
			}
			Avoidance->ComputeAvoidance(World);
		}
	}

	virtual FSeinSystemDescriptor DescribeSystem() const override
	{
		return FSeinSystemDescriptor::WithCanonicalState(
			FName(TEXT("seinarts.movement.avoidance")),
			1u,
			ESeinTickPhase::PreTick,
			SeinSystemPriority::Avoidance,
			{FName(TEXT(
				"seinarts.movement/persistent-policy-instances"))});
	}

private:
	TWeakObjectPtr<USeinMovementSubsystem> OwnerSubsystem;
	USeinAvoidance* Avoidance = nullptr;
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAvoidanceDefault.h
 * @brief   The framework's shipped local-avoidance plug — CURRENTLY A CLEAN-SLATE SKELETON.
 *
 *          ComputeAvoidance is a deliberate no-op: it writes no steer, so selecting this class is
 *          behaviourally IDENTICAL to AvoidanceClass=None. It exists as the registered scaffold to
 *          rebuild a local-avoidance model into from the ground up (the former lateral-steer boids
 *          model was removed 2026-07-02 — recoverable from git history). See the .cpp for the
 *          rebuild contract (snapshot reads, per-unit AvoidanceOutput writes, determinism rules).
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinAvoidance.h"
#include "SeinAvoidanceDefault.generated.h"

class USeinWorldSubsystem;

/**
 * The out-of-the-box local-avoidance plug. Currently a CLEAN-SLATE SKELETON: it does nothing, so
 * moving units behave exactly as they would with avoidance turned off (AvoidanceClass=None). This
 * is the scaffold for building the shipped avoidance model from the ground up — build the model in
 * ComputeAvoidance here, or subclass Sein Avoidance (the abstract base) to ship an alternative and
 * select it in settings.
 */
UCLASS(meta = (DisplayName = "Sein Avoidance Default"))
class SEINARTSMOVEMENT_API USeinAvoidanceDefault : public USeinAvoidance
{
	GENERATED_BODY()

public:

	/** One PreTick local-avoidance pass. CLEAN-SLATE SKELETON — currently a no-op (writes no steer;
	 *  behaviourally identical to AvoidanceClass=None). Rebuild the model here; see the .cpp and
	 *  USeinAvoidance::ComputeAvoidance for the contract. */
	virtual void ComputeAvoidance(USeinWorldSubsystem& World) override;
};

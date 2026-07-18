#pragma once

#include "CoreMinimal.h"

/** One end-of-tick sample from the collision determinism workload. */
struct SEINARTSTESTSUPPORT_API FSeinCollisionDeterminismFrame
{
	int32 Tick = 0;
	uint32 StateHash = 0;
	uint64 PoseDigest = 0;
	TArray<uint64> PoseWords;

	/** Mode-neutral payload used by the fresh-process log comparator. */
	FString ToLogPayload() const;
};

/** Result and non-vacuity diagnostics for one fresh transient-world run. */
struct SEINARTSTESTSUPPORT_API FSeinCollisionDeterminismTrace
{
	bool bRequestedParallel = false;
	bool bParallelCvarsAvailable = false;
	bool bParallelModeObserved = false;
	bool bParallelResolverSelected = false;
	bool bAuthoritativeDestinationResolverBound = false;
	bool bAnyEntityMoved = false;
	int32 ExpectedEntityCount = 0;
	int32 SpawnedEntityCount = 0;
	int32 FinalEntityCount = 0;
	int32 ComponentStorageCount = 0;
	int32 MaxDynamicEntries = 0;
	TArray<FSeinCollisionDeterminismFrame> Frames;
	FString FailureReason;

	/** Empty on success; otherwise explains why this run was invalid or vacuous. */
	FString Validate(bool bExpectedParallel, int32 ExpectedTicks) const;
};

/**
 * Runs the exact production fixed-tick path in a CQTest transient world.
 * The packed collider workload exercises collision broadphase, parallel Jacobi
 * resolution, and the component-storage StateHash dispatch.
 */
SEINARTSTESTSUPPORT_API FSeinCollisionDeterminismTrace
	SeinRunCollisionDeterminismScenario(bool bParallel, int32 TickCount = 120);

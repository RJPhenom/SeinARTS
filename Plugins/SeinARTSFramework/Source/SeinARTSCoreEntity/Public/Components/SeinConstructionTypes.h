/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinConstructionTypes.h
 * @author       RJ Macklem
 * @created      11 Sep 2026
 * @latest       11 Sep 2026
 * @brief        Defines construction jobs, lifecycle results, and read-only status.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "SeinConstructionTypes.generated.h"

/** The lifecycle of a construction job. Designer milestones are separate stage tags. */
UENUM(BlueprintType)
enum class ESeinConstructionState : uint8
{
	Complete,
	Queued,
	Building,
	Paused,
	ReadyToComplete UMETA(Hidden)
};

/** Whether an operation changed a job, or why it could not proceed. */
UENUM(BlueprintType)
enum class ESeinConstructionResult : uint8
{
	Succeeded,
	Unchanged,
	ReadyToComplete UMETA(DisplayName = "Work Complete"),
	AlreadyComplete,
	InvalidEntity,
	MissingComponent,
	StaleJob,
	InvalidState,
	InvalidAmount,
	NotAuthorized,
	JobLimitReached
};

/** Initial state for Spawn Construction Site. Overrides Start Queued for Construction. */
UENUM(BlueprintType)
enum class ESeinConstructionInitialState : uint8
{
	Queued,
	Building
};

/** Identifies one job. Old worker actions cannot mutate a later job on the same entity. */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinConstructionHandle
{
	GENERATED_BODY()

	/** Entity that carries this job. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS")
	FSeinEntityHandle Entity;

	/** Monotonic job number. Zero means no job has been queued. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS")
	int32 JobID = 0;
};

/** A safe read for widgets and abilities. Invalid queries return a cleared status. */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinConstructionStatus
{
	GENERATED_BODY()

	/** True if a living entity carries construction settings. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS")
	bool bValid = false;

	/** Current job. Its ID is zero when the entity has always been complete. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS")
	FSeinConstructionHandle Construction;

	/** Lifecycle phase. Check Valid before interpreting a failed query. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS")
	ESeinConstructionState State = ESeinConstructionState::Complete;

	/** Designer milestone. Empty means no milestone is assigned. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS")
	FGameplayTag Stage;

};

/** Work measurement on the current construction job. Invalid means no job has been queued. */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinConstructionWorkStatus
{
	GENERATED_BODY()
	/** True when the entity has a current or completed construction job. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS")
	bool bValid = false;
	/** Work meets its requirement. This does not complete the construction job. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS")
	bool bWorkComplete = false;
	/** Contributed work, in units chosen by the build ability. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS")
	FFixedPoint Progress;

	/** Required work captured at queue time, in the same units as contributed work. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS")
	FFixedPoint RequiredWork;

	/** Normalized work progress. Reaching one does not complete the job. Invalid is zero. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS")
	FFixedPoint Percent;
};

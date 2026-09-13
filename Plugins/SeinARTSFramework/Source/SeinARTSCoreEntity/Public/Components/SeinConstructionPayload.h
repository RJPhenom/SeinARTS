/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinConstructionPayload.h
 * @author       RJ Macklem
 * @created      11 Sep 2026
 * @latest       11 Sep 2026
 * @brief        Stores construction settings and persistent deterministic job state.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#pragma once

#include "CoreMinimal.h"
#include "Components/SeinPayload.h"
#include "Components/SeinConstructionTypes.h"
#include "Templates/SubclassOf.h"
#include "SeinConstructionPayload.generated.h"

class USeinEffect;

/** Settings survive completion. Use construction operations to change the job's lifecycle. */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinConstructionPayload : public FSeinPayload
{
	GENERATED_BODY()

	/** Start as an unfinished site waiting for work. Off starts completed without applying a completion effect.
	 *  Applies to spawned entities and level instances. Start Construction begins work separately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS", meta = (DisplayName = "Start Queued for Construction"))
	bool bQueueConstructionOnSpawn = false;

	/** Legacy duration used only when migrating older construction assets to Required Work. */
	UPROPERTY()
	FFixedPoint TimeToCompletion = FFixedPoint::FromInt(10);

	/** Applied once by Complete Construction. Captured when the job is queued. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS")
	TSubclassOf<USeinEffect> CompletionEffect;

	/** Work contributed to the current job. Ignored by games using another completion rule. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS")
	FFixedPoint Progress = FFixedPoint::Zero;

	/** Lifecycle phase. Merely carrying this payload does not start construction. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS")
	ESeinConstructionState State = ESeinConstructionState::Complete;

	/** Designer milestone, changed through Set Construction Stage. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS")
	FGameplayTag Stage;

	/** Job identity, retained after completion to reject stale worker actions. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS")
	int32 JobID = 0;

	/** Work needed for the next job. Units belong to the build ability; HP-based construction may ignore this field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS", meta = (ClampMin = "0"))
	FFixedPoint RequiredWork = FFixedPoint::FromInt(10);

	/** Required work captured when the current job was queued. Changing Required Work affects the next job. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS")
	FFixedPoint JobRequiredWork = FFixedPoint::Zero;

	/** Completion effect captured at queue time. */
	UPROPERTY()
	TSubclassOf<USeinEffect> JobCompletionEffect;

	/** Whether this job owns one UnderConstruction grant independently of other sources. */
	UPROPERTY()
	bool bOwnsConstructionTag = false;
};

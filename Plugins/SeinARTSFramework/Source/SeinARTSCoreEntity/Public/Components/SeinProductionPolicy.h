/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinProductionPolicy.h
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Production allowances and deterministic runtime policy state.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */
#pragma once

#include "CoreMinimal.h"
#include "Components/SeinPayload.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "SeinProductionPolicy.generated.h"

/** Limits queued items plus successful completions of the same exact producible class. */
UENUM(BlueprintType)
enum class ESeinProductionQueuePolicy : uint8
{
	MultiQueueable UMETA(DisplayName = "Multi-Queueable"),
	OncePerProductionUnit UMETA(DisplayName = "Queue Once per Production Unit"),
	OncePerPlayer UMETA(DisplayName = "Queue Once per Player"),
	FixedAmountPerProductionUnit UMETA(DisplayName = "Queue Fixed Amount per Production Unit"),
	FixedAmountPerPlayer UMETA(DisplayName = "Queue Fixed Amount per Player")
};

/** Runtime replacement for the producible's authored policy and amount. */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinProductionQueueSettings
{
	GENERATED_BODY()

	/** Limits this class across a producer's lifetime or a player's match. Cancelled items do not consume allowance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS")
	ESeinProductionQueuePolicy QueuePolicy = ESeinProductionQueuePolicy::MultiQueueable;

	/** Maximum queued plus successfully completed items for a fixed-amount policy. Must be at least 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS", meta = (ClampMin = "1", EditCondition = "QueuePolicy == ESeinProductionQueuePolicy::FixedAmountPerProductionUnit || QueuePolicy == ESeinProductionQueuePolicy::FixedAmountPerPlayer", EditConditionHides))
	int32 QueueAmount = 1;

	bool IsValid() const;
	bool IsPlayerScoped() const;
	int32 GetLimit() const;
};

/** Read-only result of checking whether one more item can enter a production queue. */
UENUM(BlueprintType)
enum class ESeinProductionQueueResult : uint8
{
	Available,
	InvalidProducer,
	InvalidProducible,
	MissingProduction,
	QueueFull,
	InvalidPolicy,
	LimitReached
};

/** Persistent counts and overrides, keyed by canonical exact class path. */
USTRUCT(meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinProductionPolicyState
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<FString, int64> Completed;

	UPROPERTY()
	TMap<FString, FSeinProductionQueueSettings> Overrides;

	bool IsValid() const;
	uint32 ComputeHash() const;
};

/** Backend history survives removing/replacing the producer's production component. */
USTRUCT(meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinProductionHistoryPayload : public FSeinPayload
{
	GENERATED_BODY()

	UPROPERTY()
	FSeinProductionPolicyState State;
};

FORCEINLINE uint32 GetTypeHash(const FSeinProductionHistoryPayload& Payload)
{
	return Payload.State.ComputeHash();
}

/** Stack-local receipt protecting an item while completion callbacks run. */
struct FSeinProductionCompletionClaim
{
	FSeinEntityHandle Producer;
	FSeinPlayerID Player;
	FString ClassPath;
	bool bProducerIncremented = false;
	bool bPlayerIncremented = false;
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMatchBootstrapBarrier.h
 * @brief   Topology-neutral one-shot barrier between tick-zero materialization and simulation.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinMatchBootstrapBarrier.generated.h"

class USeinWorldSubsystem;

/**
 * Opaque, copyable native capability for one world's bootstrap control plane.
 * The token is deliberately inaccessible to callers and is never serialized,
 * hashed, replicated, or exposed to Blueprint.
 */
class SEINARTSCOREENTITY_API FSeinMatchBootstrapAuthorityHandle
{
public:
	FSeinMatchBootstrapAuthorityHandle() = default;
	bool IsValid() const { return Token.IsValid(); }

private:
	FName StableAuthorityID;
	FGuid Token;

	friend class USeinWorldSubsystem;
};

/**
 * Persistent phase of this world's one-shot tick-zero bootstrap barrier.
 * The materializer's plans and scratch state live outside CoreEntity and may
 * be released after authorization; this enum and the receipt remain as the
 * start/resume guard for the lifetime of the world.
 */
UENUM(BlueprintType)
enum class ESeinMatchBootstrapState : uint8
{
	/** No materialization transaction has claimed this world. */
	Awaiting,

	/** A materializer owns tick-zero mutation and has not sealed it yet. */
	Applying,

	/** Local materialization is sealed and its canonical receipt is available. */
	LocallyReady,

	/** The active topology accepted this exact local receipt. */
	Authorized,

	/** Bootstrap failed closed. World replacement is the only recovery path. */
	Failed,

	/** First launch consumed authorization; later Stop/Start calls are resumes. */
	Consumed,
};

/**
 * Canonical proof that one peer materialized the agreed tick-zero contract.
 * Authorization context is deliberately separate: a receipt describes the
 * deterministic result, while the topology adapter binds its acceptance to a
 * particular match/coordinator epoch.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinMatchBootstrapReceipt
{
	GENERATED_BODY()

	static constexpr int32 CurrentFormatVersion = 3;

	/** Version of this receipt's field/framing contract. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Match|Bootstrap")
	int32 FormatVersion = CurrentFormatVersion;

	/** Canonical identity of the immutable bootstrap inputs. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Match|Bootstrap")
	FGuid ContractDigest;

	/** Generated Blueprint/native simulation-content identity. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Match|Bootstrap")
	FGuid SimulationContentDigest;

	/** Frozen native + Blueprint canonical-state and execution-topology contract. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Match|Bootstrap")
	FGuid StateContractDigest;

	/** Canonical identity of the materializer's sealed execution plan. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Match|Bootstrap")
	FGuid PlanDigest;

	/** Canonical digest of the locally materialized deterministic tick-zero state. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Match|Bootstrap")
	FGuid InitialStateDigest;

	bool IsValid() const
	{
		return FormatVersion == CurrentFormatVersion
			&& ContractDigest.IsValid()
			&& SimulationContentDigest.IsValid()
			&& StateContractDigest.IsValid()
			&& PlanDigest.IsValid()
			&& InitialStateDigest.IsValid();
	}

	bool operator==(const FSeinMatchBootstrapReceipt& Other) const
	{
		return FormatVersion == Other.FormatVersion
			&& ContractDigest == Other.ContractDigest
			&& SimulationContentDigest
				== Other.SimulationContentDigest
			&& StateContractDigest == Other.StateContractDigest
			&& PlanDigest == Other.PlanDigest
			&& InitialStateDigest == Other.InitialStateDigest;
	}

	bool operator!=(const FSeinMatchBootstrapReceipt& Other) const
	{
		return !(*this == Other);
	}
};

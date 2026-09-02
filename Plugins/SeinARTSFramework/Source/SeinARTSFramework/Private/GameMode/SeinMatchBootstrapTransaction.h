/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinMatchBootstrapTransaction.h
 * @author       RJ Macklem
 * @created      17 Aug 2026
 * @latest       22 Aug 2026
 * @brief        Declares the transient deterministic tick-zero materialization plan.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Data/SeinMatchSettings.h"
#include "Simulation/SeinMatchBootstrapBarrier.h"
#include "Types/Transform.h"
#include "SeinMatchBootstrapTransaction.generated.h"

class ASeinActor;
class ASeinPlayerStart;
class USeinActorBridgeSubsystem;
class USeinEntityBridgeComponent;
class USeinWorldSubsystem;
class UWorld;

USTRUCT(meta = (SeinDeterministic))
struct FSeinBootstrapPlayerPlanEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FSeinPlayerID PlayerID;

	UPROPERTY()
	FSeinFactionID FactionID;

	UPROPERTY()
	uint8 TeamID = 0;

	UPROPERTY()
	ESeinSlotState SlotState = ESeinSlotState::Closed;

	UPROPERTY()
	bool bHasSpawnEntity = false;

	UPROPERTY()
	FString SpawnClassPath;

	UPROPERTY()
	FFixedTransform SpawnTransform;
};

USTRUCT(meta = (SeinDeterministic))
struct FSeinBootstrapPlacedActorPlanEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FString StableKey;

	UPROPERTY()
	FString ActorClassPath;

	UPROPERTY()
	FFixedTransform BakedTransform;

	UPROPERTY()
	FSeinPlayerID OwnerPlayerID;
};

USTRUCT(meta = (SeinDeterministic))
struct FSeinBootstrapInactivePlacedActorPlanEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FString StableKey;

	UPROPERTY()
	FString ActorClassPath;

	UPROPERTY()
	FSeinPlayerID OwnerPlayerID;
};

USTRUCT(meta = (SeinDeterministic))
struct FSeinBootstrapPlanDigestData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSeinBootstrapPlayerPlanEntry> Players;

	UPROPERTY()
	TArray<FSeinBootstrapPlacedActorPlanEntry> PlacedActors;

	UPROPERTY()
	TArray<FSeinBootstrapInactivePlacedActorPlanEntry> InactivePlacedActors;
};

USTRUCT(meta = (SeinDeterministic))
struct FSeinStandaloneBootstrapContext
{
	GENERATED_BODY()

	UPROPERTY()
	FString Domain;

	UPROPERTY()
	FString MapPackageName;

	UPROPERTY()
	FString MatchSettingsDigest;

	UPROPERTY()
	FString CommandProtocolDigest;

	UPROPERTY()
	FString SimulationContentDigest;

	UPROPERTY()
	int32 ConfigFingerprint = 0;

	UPROPERTY()
	int64 SessionSeed = 0;
};

/**
 * One-shot scratch owner for a world's default Framework materializer.
 * The immutable reflected plan is retained only while Core waits for the
 * topology to authorize or fail its receipt.
 */
class FSeinMatchBootstrapTransaction final
{
public:
	FSeinMatchBootstrapTransaction(
		UWorld& InWorld,
		USeinWorldSubsystem& InWorldSubsystem,
		USeinActorBridgeSubsystem& InActorBridge);

	bool Materialize(
		const FSeinMatchSettings& Settings,
		const FGuid& AuthorizationContextDigest,
		FSeinMatchBootstrapReceipt& OutReceipt,
		FString& OutError);

	static bool ComputeStandaloneAuthorizationContextDigest(
		UWorld& World,
		USeinWorldSubsystem& WorldSubsystem,
		const FSeinMatchSettings& Settings,
		FGuid& OutDigest,
		FString& OutError);

private:
	bool Preflight(FString& OutError);
	bool VerifyFrozenPlan(FString& OutError) const;
	bool Apply(FString& OutError);
	bool ComputePlanDigest(FString& OutError);
	bool Fail(const FString& Reason, FString& OutError);

	static FString BuildPlacedActorStableKey(const ASeinActor& Actor);
	static bool ValidateEntityComponentData(
		TConstArrayView<const USeinEntityBridgeComponent*> Components,
		const FString& OwnerLabel,
		FString& OutError);

	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<USeinWorldSubsystem> WorldSubsystem;
	TWeakObjectPtr<USeinActorBridgeSubsystem> ActorBridge;

	FSeinMatchSettings CanonicalSettings;
	FGuid ContractDigest;
	FGuid AuthorizationContextDigest;
	FGuid PlanDigest;
	FSeinBootstrapPlanDigestData Plan;
	TArray<TWeakObjectPtr<ASeinPlayerStart>> PlayerStarts;
	TArray<TWeakObjectPtr<ASeinActor>> PlacedActors;
	TArray<TWeakObjectPtr<ASeinActor>> InactivePlacedActors;
};

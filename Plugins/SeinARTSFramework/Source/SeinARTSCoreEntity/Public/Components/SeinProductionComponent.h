/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinProductionComponent.h
 * @brief   Single-queue production component.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/Transform.h"
#include "GameplayTagContainer.h"
#include "Actor/SeinActor.h"
#include "Core/SeinEntityHandle.h"
#include "Components/SeinComponent.h"
#include "Data/SeinResourceTypes.h"
#include "SeinProductionComponent.generated.h"

class USeinEffect;

/**
 * Refund policy for a cancelled production queue entry. DESIGN §9 Q2.
 * Default: refund = (1 - progress_fraction) * cost. Opt-in: flat custom percentage.
 * Authored on the producible's `FSeinProducibleComponent`; snapshotted on each queue entry at enqueue.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinProductionRefundPolicy
{
	GENERATED_BODY()

	/** If true, use `CustomRefundPercentage` instead of progress-proportional refund. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Production")
	bool bUseCustomRefund = false;

	/** Flat fraction of cost refunded when bUseCustomRefund == true.
	 *  1.0 = full refund, 0.0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Production",
		meta = (EditCondition = "bUseCustomRefund", ClampMin = "0.0", ClampMax = "1.0"))
	FFixedPoint CustomRefundPercentage = FFixedPoint::One;
};

// Rally-target sub-struct removed — fields are now flat on FSeinProductionComponent
// (bRallyToEntity / RallyTransform / RallyEntity). Designer doesn't see a
// nested struct in the production component's details panel; rally is one
// authoring section alongside the queue / spawn fields. DESIGN §9 Q9.

/**
 * One entry in a production queue. Carries a snapshot of cost at queue time so
 * refunds are deterministic, plus — for research entries — the USeinEffect
 * class to apply on completion (per DESIGN §10 tech unification).
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinProductionQueueEntry
{
	GENERATED_BODY()

	/** Blueprint class being produced. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	TSubclassOf<ASeinActor> ActorClass;

	/** Total time required to complete. Snapshot — not affected by mid-build
	 *  modifiers per DESIGN §9 Q4b. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	FFixedPoint TotalBuildTime;

	/** Cost portion that was deducted at enqueue time (resources whose catalog
	 *  `ProductionDeductionTiming == AtEnqueue`). Drives the refund on cancel. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	FSeinResourceCost AtEnqueueCost;

	/** Cost portion deferred to completion time (resources whose catalog
	 *  `ProductionDeductionTiming == AtCompletion`). Attempted on spawn; may
	 *  stall if it would exceed cap. Never refunded on cancel (never deducted). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	FSeinResourceCost AtCompletionCost;

	/** Research entries: USeinEffect class applied to the owner on completion. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	bool bIsResearch = false;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	TSubclassOf<USeinEffect> ResearchEffectClass;

	/** Copy of the owning producible's refund policy, frozen at enqueue. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	FSeinProductionRefundPolicy RefundPolicy;
};

FORCEINLINE uint32 GetTypeHash(const FSeinProductionQueueEntry& Entry)
{
	uint32 Hash = GetTypeHash(Entry.TotalBuildTime);
	Hash = HashCombine(Hash, GetTypeHash(Entry.ActorClass.Get()));
	Hash = HashCombine(Hash, GetTypeHash(Entry.AtEnqueueCost));
	Hash = HashCombine(Hash, GetTypeHash(Entry.AtCompletionCost));
	return Hash;
}

/**
 * Production component for entities that can produce units or research. Any
 * entity carrying this can produce — not limited to buildings per DESIGN §9.
 *
 * What an entity can produce is discovered via its abilities: any USeinAbility
 * with `ProducibleClass` or `ResearchEffectClass` set is a production button on
 * this entity's HUD. Activating such an ability calls
 * USeinProductionBPFL::SeinEnqueueProduction, which appends to `Queue` here.
 * The component itself owns no producibles list — the AbilitiesComponent is
 * the single source of truth for "what can this entity do" (DESIGN §2 + §9).
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinProductionComponent : public FSeinComponent
{
	GENERATED_BODY()

	/** Maximum queue depth. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Production")
	int32 MaxQueueSize = 5;

	/** Current production queue. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	TArray<FSeinProductionQueueEntry> Queue;

	/** Build progress of the current (front) queue entry. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	FFixedPoint CurrentBuildProgress;

	/** True iff the front item reached 100% but the AtCompletion cost wouldn't
	 *  fit (e.g., pop cap hit). Cleared once AttemptSpawn succeeds. DESIGN §9 stall-at-completion. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	bool bStalledAtCompletion = false;

	// ─── Rally (flat fields — was wrapped in FSeinRallyTarget) ───

	/** When true, produced units chase `RallyEntity` (destination resolves
	 *  at dispatch time to the entity's current transform). When false, units
	 *  rally to `RallyTransform.GetLocation()` facing the transform's rotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Production")
	bool bRallyToEntity = false;

	/** Used when bRallyToEntity is false. Produced units rally to this
	 *  transform's location facing the transform's rotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Production",
		meta = (EditCondition = "!bRallyToEntity"))
	FFixedTransform RallyTransform;

	/** Used when bRallyToEntity is true. Produced units chase this entity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Production",
		meta = (EditCondition = "bRallyToEntity"))
	FSeinEntityHandle RallyEntity;

	/** Producer-LOCAL-space offset where finished units appear. Composed with
	 *  the producer's world transform at spawn time:
	 *      WorldSpawn = Producer.WorldTransform * SpawnPointOffset
	 *  so the offset rotates with the producer (a barracks placed at any yaw
	 *  spawns its riflemen out of the same door regardless of facing). The
	 *  offset's rotation also carries to the produced unit's initial facing —
	 *  point it toward the rally walkway and units come out aimed the right
	 *  way before the rally Move kicks in.
	 *
	 *  Default = identity (units spawn at the producer's pivot, typically
	 *  inside the mesh — designers must override). Authored on the producer
	 *  BP's Production Component. The editor's component visualizer draws a
	 *  green marker + forward arrow at the resolved world position when the
	 *  producer is selected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Production",
		meta = (DisplayName = "Spawn Point Offset"))
	FFixedTransform SpawnPointOffset;

	bool IsProducing() const;
	FFixedPoint GetProgressPercent() const;
	bool CanQueueMore() const;
};

FORCEINLINE uint32 GetTypeHash(const FSeinProductionComponent& Component)
{
	uint32 Hash = GetTypeHash(Component.MaxQueueSize);
	Hash = HashCombine(Hash, GetTypeHash(Component.CurrentBuildProgress));
	Hash = HashCombine(Hash, GetTypeHash(Component.bRallyToEntity));
	Hash = HashCombine(Hash, GetTypeHash(Component.RallyTransform));
	Hash = HashCombine(Hash, GetTypeHash(Component.RallyEntity));
	Hash = HashCombine(Hash, GetTypeHash(Component.SpawnPointOffset));
	Hash = HashCombine(Hash, GetTypeHash(Component.bStalledAtCompletion));
	for (const auto& Entry : Component.Queue)
	{
		Hash = HashCombine(Hash, GetTypeHash(Entry));
	}
	return Hash;
}

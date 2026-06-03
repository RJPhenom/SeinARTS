/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLifespanData.h
 * @brief   Component for auto-expiring entities. FSeinLifespanSystem (PostTick)
 *          reaps entities whose ExpiresAtTick <= CurrentTick.
 *          For short-lived entities (projectiles, damage zones, decoys,
 *          sim-side VFX anchors).
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SeinComponent.h"
#include "SeinLifespanData.generated.h"

USTRUCT(BlueprintType, meta = (SeinDeterministic, SeinSubData))
struct SEINARTSCOREENTITY_API FSeinLifespanData : public FSeinComponent
{
	GENERATED_BODY()

	/** Sim tick at which this entity should be destroyed. When CurrentTick >= this,
	 *  FSeinLifespanSystem enqueues a DestroyEntity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Entity")
	int32 ExpiresAtTick = 0;
};

FORCEINLINE uint32 GetTypeHash(const FSeinLifespanData& Comp)
{
	return GetTypeHash(Comp.ExpiresAtTick);
}

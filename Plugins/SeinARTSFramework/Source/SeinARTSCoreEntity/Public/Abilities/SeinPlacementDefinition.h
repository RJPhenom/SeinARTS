/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinPlacementDefinition.h
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Deterministic footprint authoring independent of capture gestures and rendering.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#pragma once
#include "CoreMinimal.h"
#include "SeinPlacementDefinition.generated.h"

/** Footprint source used by preview validation and authoritative placement admission. */
USTRUCT(BlueprintType, meta=(SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinPlacementDefinition
{
	GENERATED_BODY()
	/** Actor class whose authored extents must fit at the captured pose. Does not spawn an actor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement", meta=(DisplayName="Footprint Actor Class"))
	TSoftClassPtr<AActor> ActorClass;
};

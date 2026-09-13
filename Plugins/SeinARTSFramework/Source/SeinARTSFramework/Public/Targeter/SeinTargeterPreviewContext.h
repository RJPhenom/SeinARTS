/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinTargeterPreviewContext.h
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Read-only presentation state for any target capture gesture.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Abilities/SeinTargeterTypes.h"
#include "Abilities/SeinTargeterSpec.h"
#include "SeinTargeterPreviewContext.generated.h"

/** Why the local targeting session ended. Submission is not gameplay success. */
UENUM(BlueprintType)
enum class ESeinPreviewEndReason : uint8 { Submitted, Cancelled, Replaced, Unavailable };

/** Input phase of the current preview. */
UENUM(BlueprintType)
enum class ESeinPreviewPhase : uint8 { Waiting, Dragging, Chaining };

/** Presentation snapshot. ResolvedTarget is the pose encoded by confirmation. */
USTRUCT(BlueprintType)
struct SEINARTSFRAMEWORK_API FSeinTargeterPreviewContext
{
	GENERATED_BODY()
	/** Entity whose ability is being targeted. */
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS") FSeinEntityHandle SourceEntity;
	/** Ability being targeted. */
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS") FGameplayTag AbilityTag;
	/** Current input phase. */
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS") ESeinPreviewPhase Phase = ESeinPreviewPhase::Waiting;
	/** Live cursor position in world units. */
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS") FVector CursorWorld = FVector::ZeroVector;
	/** Whether AnchorWorld is present, including at world origin. */
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS") bool bHasAnchor = false;
	/** Locked start of the current gesture or segment. */
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS") FVector AnchorWorld = FVector::ZeroVector;
	/** Captured point pose; for a segment, its start and captured yaw. */
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS") FTransform ResolvedTarget;
	/** Current targeting result, independent of visual style. */
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS") ESeinTargeterValidity Validity = ESeinTargeterValidity::Valid;
	/** Broad feedback reason; custom rules can supply their own wording in Blueprint. */
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS") FText ValidityReason;
	/** Previously captured points or segments in this session. */
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS") TArray<FSeinTargeterPoint> CapturedPoints;
	/** Requested number of captures. */
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS") int32 TargetCount = 1;
	/** Area radius from the ability, in world units. */
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS") float AreaRadius = 0;
	/** Corridor width in world units; zero for a point or widthless line. */
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS") float CorridorWidth = 0;
	/** Optional default visual source from placement authoring. Visual overrides do not affect validation. */
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS") TSoftClassPtr<AActor> ActorClass;
};

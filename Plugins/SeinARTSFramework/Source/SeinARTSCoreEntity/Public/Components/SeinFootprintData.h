/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFootprintData.h
 * @brief   Multi-cell entity footprint. Buildings, walls,
 *          large vehicles occupy more than one nav cell. On spawn the nav
 *          subsystem reads this component + registers the entity with the
 *          affected tiles + marks cells DynamicBlocked / BlocksVision.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinFootprintData.generated.h"

USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinFootprintData
{
	GENERATED_BODY()

	/** Cell offsets from the entity's root cell (authoring space). Positive X is east, positive Y is north. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Footprint")
	TArray<FIntPoint> OccupiedCellOffsets;

	/** Rotation in 90° steps (0/1/2/3 = 0°/90°/180°/270°). Applied to offsets at registration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Footprint", meta = (ClampMin = "0", ClampMax = "3"))
	uint8 Rotation = 0;

	/** Mark covered cells as DynamicBlocked for pathfinding. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Footprint")
	bool bBlocksPathing = true;

	/** Mark covered cells as vision blockers (§12 Vision). MVP ignores but struct reserves the flag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Footprint")
	bool bBlocksVision = false;

	/** Agent-class index (into USeinARTSCoreSettings::AgentRadiusClasses). 0 = default class. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Footprint")
	uint8 AgentClass = 0;
};

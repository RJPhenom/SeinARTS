/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavAgentProfile.h
 * @brief   Module-neutral, complete per-entity navigation policy.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"

/**
 * Immutable-by-convention navigation policy resolved from one entity's core
 * component state. Core simulation callers build it once at a module boundary;
 * navigation implementations consume it without reaching back into the world or
 * repeating component/footprint discovery in collision and formation hot paths.
 *
 * This is intentionally a plain C++ value, not reflected simulation state. Its
 * authoritative inputs remain the entity's canonical components and tags.
 */
struct SEINARTSCOREENTITY_API FSeinNavAgentProfile
{
	FSeinEntityHandle Requester;
	FGameplayTagContainer AgentTags;
	FGameplayTagContainer BlockedTerrainTags;
	uint8 AgentNavLayerMask = 0x01;
	FFixedPoint AgentFootprintRadius = FFixedPoint::Zero;
	int32 AgentWallPaddingCells = 0;
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinUITypes.h
 * @brief   Common types and enums for the SeinARTS UI Toolkit.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinUITypes.generated.h"

/** Relationship between an entity and a player. */
UENUM(BlueprintType)
enum class ESeinRelation : uint8
{
	Friendly,
	Enemy,
	Neutral
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinComponent.h
 * @brief   Base USTRUCT for all SeinARTS simulation components.
 *
 *          All Sein sim components (movement, combat, production, squad, tag,
 *          etc.) should inherit from FSeinComponent. It inherits FTableRowBase
 *          so these structs can also serve as DataTable rows (used by the
 *          BP authoring + DataTable mode).
 *
 *          Purpose: let editor struct pickers filter to "sim components only"
 *          instead of showing every USTRUCT in the engine. The Components
 *          array on the entity bridge's `FSeinAbilityComponent::ComponentData` uses this via BaseStruct metadata.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "SeinComponent.generated.h"

USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinComponent : public FTableRowBase
{
	GENERATED_BODY()
};

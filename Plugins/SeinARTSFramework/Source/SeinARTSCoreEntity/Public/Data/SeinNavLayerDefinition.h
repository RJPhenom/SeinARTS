/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavLayerDefinition.h
 * @brief   Plugin-settings row defining one designer-configurable nav layer.
 *          Up to 7 layers map to bits 1..7 of the agent / blocker layer mask
 *          on `FSeinNavigationComponent::NavLayerMask` and `FSeinExtentsComponent::
 *          BlockedNavLayerMask`.
 *
 *          NOTE: the "Default" layer is NOT in this array — it's reserved as
 *          bit 0 and always present. These 7 slots are exclusively for
 *          additional agent classes a game needs (Amphibious, HeavyVehicle,
 *          FriendlyFaction, etc.).
 *
 *          Bit-index stability: renaming a slot is safe; reordering / inserting
 *          IS NOT (shifts every higher-index slot's bit, breaks replays + saves).
 *          Only append or rename.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinNavLayerDefinition.generated.h"

/**
 * One designer-configurable N-layer definition for navigation. Slot index N
 * (0..6) maps to bit (1 + N) of the agent/blocker layer mask.
 *
 *  - bEnabled toggles the slot without shifting bit indices.
 *  - LayerName is displayed in the bitmask combos on FSeinNavigationComponent and
 *    FSeinExtentsComponent (BlockedNavLayerMask) for designer clarity.
 *  - DebugColor used by the nav debug overlay to tint blocker stamps that
 *    occupy this layer (mixed-layer blockers blend toward the dominant bit).
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinNavLayerDefinition
{
	GENERATED_BODY()

	/** If false, this slot is treated as inert at runtime — blockers with this
	 *  bit don't gate any agent, agents authored on it intersect nothing. Lets
	 *  designers pre-reserve a layer's name + slot index without making it
	 *  live, or toggle it off without shifting higher-index slots. */
	UPROPERTY(Config, EditAnywhere, Category = "SeinARTS|Navigation|Layer")
	bool bEnabled = false;

	/** Designer-friendly label for this slot. Displayed alongside the raw bit
	 *  in BlockedNavLayerMask / NavLayerMask bitmask UIs. Use project-specific
	 *  names: "Amphibious", "HeavyVehicle", "FriendlyFaction", etc. */
	UPROPERTY(Config, EditAnywhere, Category = "SeinARTS|Navigation|Layer")
	FName LayerName = NAME_None;

	/** Color used by the nav debug overlay to tint blocker cells whose
	 *  BlockedNavLayerMask includes this layer's bit. Mixed-layer blockers
	 *  blend toward the dominant bit's color; the framework default
	 *  (orange) renders for blockers on Default-only. */
	UPROPERTY(Config, EditAnywhere, Category = "SeinARTS|Navigation|Layer")
	FLinearColor DebugColor = FLinearColor::White;
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisionLayerDefinition.h
 * @brief   Plugin-settings row defining one designer-configurable vision layer
 *          Up to 6 layers map to the 6 N-bits in the per-cell
 *          EVNNNNNN bitfield (bits 2..7).
 *
 *          NOTE: the "Normal" / default-visibility layer is NOT in this array.
 *          It's reserved by the framework and always maps to the V bit (bit 1)
 *          of the EVNNNNNN bitfield. Designers reference the default layer by
 *          the name "Normal" on entity PerceptionLayers / EmissionLayers,
 *          without declaring it here.
 *
 *          Plugin settings ship 6 slots (N0..N5). Slot index N → bitfield bit
 *          (2 + N). Slot index is canonical; renaming a slot is safe,
 *          reordering / inserting IS NOT (breaks replays).
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinVisionLayerDefinition.generated.h"

/**
 * One designer-configurable N-layer definition.
 *
 * Configure in Project Settings > Plugins > SeinARTS > Vision Layers. The
 * framework-default "Normal" layer is NOT part of this array — it's always the
 * V bit, hardcoded. These 6 slots are exclusively for additional layers a game
 * needs beyond generic visibility: Stealth, Thermal, Radar, DetectorPerception,
 * Acoustic, Infrared, etc.
 *
 * Bit-index stability:
 *  - `bEnabled` toggles the slot without shifting bit indices — disabling a
 *    layer mid-development is safe.
 *  - Renaming a slot is safe.
 *  - Reordering or inserting entries into the middle of the array IS NOT safe
 *    (shifts every higher-index slot's bit, breaking replays + save games).
 *    Only append or rename.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinVisionLayerDefinition
{
	GENERATED_BODY()

	/** If false, this slot is skipped at runtime — its refcount array isn't
	 *  consulted, its N-bit in the bitfield stays clear. Lets designers
	 *  pre-reserve a layer's name + slot index without making it live, or
	 *  toggle it off without shifting higher-index slots. */
	UPROPERTY(Config, EditAnywhere, Category = "SeinARTS|Vision|Layer")
	bool bEnabled = false;

	/** Name used to reference this layer by name in BP queries (e.g.
	 *  `SeinIsCellVisible(..., LayerName)`) and the debug viewer. Runtime
	 *  matching is by BIT, not name: a stamp emits into this layer via its
	 *  `FSeinVisionStamp.LayerMask` bit; a target is seen on it when its
	 *  `FSeinFogVisibilityPayload.FogVisibilityLayerMask` shares that bit.
	 *  Case-sensitive exact match. "Normal" is reserved
	 *  (it's the V-bit / default layer, handled by the framework) — a designer
	 *  naming a slot "Normal" here will be rejected with a log warning at
	 *  subsystem init. Use project-specific names: "Stealth", "Thermal",
	 *  "Radar", "DetectorPerception", etc. */
	UPROPERTY(Config, EditAnywhere, Category = "SeinARTS|Vision|Layer")
	FName LayerName = NAME_None;

	/** Color used by the fog-of-war debug viewer to paint cells where this
	 *  layer's bit is set. Defaults are seeded per-slot in the settings ctor
	 *  (blue→violet→purple→light-purple→magenta→light-pink for slots 0..5).
	 *  Swap via `Sein.FogOfWar.Show.Layer <bit>` to audit
	 *  an individual layer's stamp output. */
	UPROPERTY(Config, EditAnywhere, Category = "SeinARTS|Vision|Layer")
	FLinearColor DebugColor = FLinearColor::White;
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarBPFL.h
 * @brief   Blueprint-exposed reader functions for the fog-of-war subsystem.
 *          Designer-facing surface: "is this cell visible?", "is this entity
 *          visible to player X?", "what's the local PC's observer ID?".
 *          Queries route through the active `USeinFogOfWar` impl via the
 *          subsystem — no impl-specific knowledge leaks into BP.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "SeinFogOfWarBPFL.generated.h"

UCLASS(meta = (DisplayName = "SeinARTS Fog Of War Library"))
class SEINARTSFOGOFWAR_API USeinFogOfWarBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Is the cell at `WorldPos` currently visible to `Observer` on the
	 *  given vision layer? `LayerName` resolves to a bit: "Normal" → V bit,
	 *  "Explored" → E bit (sticky history), any other name matches
	 *  `USeinARTSCoreSettings::VisionLayers[i].LayerName` (enabled slots
	 *  only). Unknown names return false while fog is active. Returns true
	 *  when fog is disabled or its implementation has no runtime data,
	 *  matching the Line of Sight Resolver's permissive fallback. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Fog Of War",
		meta = (WorldContext = "WorldContextObject",
				DisplayName = "Is Cell Visible",
				AdvancedDisplay = "LayerName"))
	static bool SeinIsCellVisible(const UObject* WorldContextObject,
		FSeinPlayerID Observer, const FVector& WorldPos, FName LayerName = "Normal");

	/** Has `Observer` ever explored the cell at `WorldPos`? Sticky per
	 *  match — once set, stays set until the next match start. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Fog Of War",
		meta = (WorldContext = "WorldContextObject",
				DisplayName = "Is Cell Explored"))
	static bool SeinIsCellExplored(const UObject* WorldContextObject,
		FSeinPlayerID Observer, const FVector& WorldPos);

	/** Raw EVNNNNNN byte at the cell containing `WorldPos` from
	 *  `Observer`'s VisionGroup. Bit 0 = Explored (sticky), bit 1 = Normal
	 *  visibility, bits 2..7 = custom layers N0..N5. Advanced: most BP
	 *  callers want `SeinIsCellVisible` / `SeinIsCellExplored` instead. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Fog Of War",
		meta = (WorldContext = "WorldContextObject",
				DisplayName = "Get Cell Bitfield",
				AdvancedDisplay))
	static uint8 SeinGetCellBitfield(const UObject* WorldContextObject,
		FSeinPlayerID Observer, const FVector& WorldPos);

	/** Is `Target` currently visible to `Observer` under the active fog's
	 *  entity-visibility policy? Honors owner visibility, authored emission
	 *  layers, volumetric footprints, and the Always Visible, Visible Once
	 *  Seen, and Visible Once Explored policies. A neutral observer disables
	 *  filtering. A disabled fog implementation or missing runtime fog data
	 *  permits visibility. Use Is Cell Visible for a raw layer check at a
	 *  specific position. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Fog Of War",
		meta = (WorldContext = "WorldContextObject",
				DisplayName = "Is Entity Visible"))
	static bool SeinIsEntityVisible(const UObject* WorldContextObject,
		FSeinPlayerID Observer, FSeinEntityHandle Target);

	/** The local player controller's FSeinPlayerID. Reflectively reads the
	 *  PC's `SeinPlayerID` property — same lookup the debug proxy uses.
	 *  Returns neutral (id 0) if no local PC exists or the PC's class
	 *  doesn't expose a `SeinPlayerID` FStructProperty. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Fog Of War",
		meta = (WorldContext = "WorldContextObject",
				DisplayName = "Get Local Observer"))
	static FSeinPlayerID SeinGetLocalObserver(const UObject* WorldContextObject);

	// ====================================================================
	// Runtime mutation — fog visibility mask (FSeinFogVisibilityComponent::FogVisibilityLayerMask)
	// ====================================================================
	//
	// "What bits need to be stamped in an observer's view for this entity
	// to be visible." Lives on `FSeinFogVisibilityComponent` (a top-level
	// component since the Phase-5+ split from extents) so entities WITHOUT
	// a physical body (sim-side VFX anchors, audio emitters, scenario
	// triggers) can still author emission. Default behaviour for entities
	// without the component authored: VisionLayersOnly policy with
	// Normal-bit emission.

	/** Read `Entity`'s current FogVisibilityLayerMask. Returns 0 if the
	 *  entity has no FSeinFogVisibilityComponent authored. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Fog Of War",
		meta = (WorldContext = "WorldContextObject",
				DisplayName = "Get Entity Emission Mask"))
	static int32 SeinGetEntityEmissionMask(const UObject* WorldContextObject,
		FSeinEntityHandle Entity);

	/** Overwrite `Entity`'s FogVisibilityLayerMask. Use for clean state
	 *  swaps (e.g. ability activates cloak → set to `N0`; ability ends →
	 *  set back to `Normal`). No-op if the entity has no
	 *  FSeinFogVisibilityComponent authored. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Fog Of War",
		meta = (WorldContext = "WorldContextObject",
				DisplayName = "Set Entity Emission Mask"))
	static void SeinSetEntityEmissionMask(const UObject* WorldContextObject,
		FSeinEntityHandle Entity,
		UPARAM(meta = (Bitmask, BitmaskEnum = "/Script/SeinARTSFogOfWar.ESeinFogOfWarLayerBit")) int32 NewMask);

	/** OR `LayersToAdd` into `Entity`'s FogVisibilityLayerMask. Use for
	 *  additive ability effects. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Fog Of War",
		meta = (WorldContext = "WorldContextObject",
				DisplayName = "Add Entity Emission Layers"))
	static void SeinAddEntityEmissionLayers(const UObject* WorldContextObject,
		FSeinEntityHandle Entity,
		UPARAM(meta = (Bitmask, BitmaskEnum = "/Script/SeinARTSFogOfWar.ESeinFogOfWarLayerBit")) int32 LayersToAdd);

	/** Clear `LayersToRemove` from `Entity`'s FogVisibilityLayerMask. Use
	 *  for ability-end / dispel cleanup. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Fog Of War",
		meta = (WorldContext = "WorldContextObject",
				DisplayName = "Remove Entity Emission Layers"))
	static void SeinRemoveEntityEmissionLayers(const UObject* WorldContextObject,
		FSeinEntityHandle Entity,
		UPARAM(meta = (Bitmask, BitmaskEnum = "/Script/SeinARTSFogOfWar.ESeinFogOfWarLayerBit")) int32 LayersToRemove);
};

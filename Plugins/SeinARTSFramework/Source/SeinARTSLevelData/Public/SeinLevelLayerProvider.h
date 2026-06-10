/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLevelLayerProvider.h
 * @brief   A level-data layer provider (planning/Decisions.md D12).
 *
 *          Navigation, Fog of War, and (later) terrain-cost each implement + register
 *          one of these with USeinLevelData. At bake time the substrate runs the shared
 *          trace pass, then asks each registered provider to compute its channel data
 *          from the shared grid + height + in-bounds mask (subsampling to its own
 *          resolution; D13). The provider serializes its channels into an OPAQUE byte
 *          blob the substrate stores in the level-data asset tagged by GetLayerId(); the
 *          layer's runtime CONSUMER (e.g. USeinNavigation) deserializes it. The substrate
 *          never interprets the blob — that decoupling is what keeps both replace-axes
 *          working (swap a provider, or swap the whole substrate).
 *
 *          Registration is programmatic (module startup), mirroring the framework's
 *          cross-module resolver / sim-system registration pattern (no-op until the
 *          owning module registers).
 */

#pragma once

#include "CoreMinimal.h"

class USeinLevelData;
class UWorld;

class SEINARTSLEVELDATA_API ISeinLevelLayerProvider
{
public:
	virtual ~ISeinLevelLayerProvider() = default;

	/** Stable id for this layer's channel block in the baked asset (e.g. "Nav",
	 *  "FogOfWar"). Must match what the runtime consumer looks up. */
	virtual FName GetLayerId() const = 0;

	/** Compute this layer's baked channels from the shared substrate. Called once
	 *  per BeginBake, after the shared trace pass. Reads `Substrate`'s grid +
	 *  GetSharedHeightAt + IsInBounds (subsampling to its own resolution), and may
	 *  run its OWN layer-specific traces against `World` (e.g. FoW's occluder sweep,
	 *  nav's connectivity midpoints — see the trace-reconciliation note in the
	 *  microplan). Serializes the result into `OutData` — opaque to the substrate. */
	virtual void BakeLayer(const USeinLevelData& Substrate, UWorld* World, TArray<uint8>& OutData) = 0;

	/** This layer's cell size as a multiple of the substrate's finest cell size
	 *  (D13 — coarser layers subsample; e.g. FoW at 400uu over a 100uu grid = 4).
	 *  Read by the bake orchestration AFTER BakeLayer returns (a provider that
	 *  derives its resolution from per-bake config records it during BakeLayer)
	 *  and stored on the channel block as metadata. Default: 1 (finest). */
	virtual int32 GetCellSizeMultiple() const { return 1; }
};

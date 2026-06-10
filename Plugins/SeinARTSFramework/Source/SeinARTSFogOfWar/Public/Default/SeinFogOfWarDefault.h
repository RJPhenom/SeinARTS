/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarDefault.h
 * @brief   Reference implementation of USeinFogOfWar. Grid loads from the
 *          unified level-data substrate's baked "FogOfWar" channel when
 *          available; auto-sizes from ASeinLevelVolumes when not. MVP stamp is
 *          flat-circle; shadowcast + lampshade + per-player + custom layers
 *          land in subsequent passes.
 *
 *          Determinism contract:
 *          - Stamp output (CellBitfield): fully FFixedPoint. StampFlatCircle
 *            uses FFixedPoint radius math + cell² integer comparisons; no
 *            float on the stamp path.
 *          - Stamp cadence: driven by USeinWorldSubsystem::OnSimTickCompleted
 *            gated by plugin-settings VisionTickInterval. All clients stamp
 *            against the same tick-N source snapshot — lockstep-safe.
 *          - LOS query: FSeinLineOfSightResolver takes FFixedVector; no
 *            float round-trip between sim caller and impl cell lookup.
 *          - Grid layout (Width/Height/Origin/CellSize): derived from AVolume
 *            bounds via FromFloat. Deterministic on IEEE-754 targets with
 *            stock UE floating-point modes (the platforms UE5 ships for).
 *          - Bake runs as a layer provider on the unified level-data bake
 *            (editor-only). Produces quantized per-cell Ground + Blocker
 *            heights in the substrate's "FogOfWar" channel; runtime
 *            dequantizes into FFixedPoint arrays on load.
 *          - Debug viz (CollectDebugCellQuads): float conversions at the
 *            FFixedPoint → FVector boundary, render-only, not sim state.
 *
 *          Upgrades in later passes:
 *          - Shadowcast + lampshade eye-height blocker test (CoH TrueSight)
 *          - Per-player VisionGroup grids + ownership filter
 *          - 6 custom layer bits (Stealth, Thermal, etc.)
 *          - Stamp-delta refcount (only recompute on cell-cross / prop change)
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinFogOfWar.h"
#include "SeinLevelLayerProvider.h"
#include "Core/SeinPlayerID.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "SeinFogOfWarDefault.generated.h"

class UWorld;
class USeinLevelData;
struct FSeinVisionComponent;
struct FSeinStampShape;

/**
 * Per-observer visibility state. One per FSeinPlayerID; lazily created on
 * the owner's first stamp.
 *
 *  - CellBitfield: the EVNNNNNN byte the player sees per cell. Bit 0
 *    (Explored) is sticky for the match. Bits 1-7 are derived from the
 *    refcount arrays — set when a bit's refcount goes 0→1, cleared when
 *    it goes 1→0. Direct readers (LOS resolver, BPFL, debug) never see
 *    refcounts; they just read this byte.
 *  - RefCounts[bit]: how many sources currently stamp `bit` at each cell.
 *    Indexed [1..7] (bit 0 unused — Explored is sticky, no refcount).
 *    Lazily allocated per-bit: only bits actually stamped allocate their
 *    W*H array. uint16 caps simultaneous overlap at 65535 sources/cell —
 *    well above any realistic RTS scenario.
 */
struct FSeinFogVisionGroup
{
	TArray<uint8> CellBitfield;
	TArray<uint16> RefCounts[8];
};

/**
 * Per-source memo of the last stamped state for delta-refcount updates.
 * One per live FSeinEntityHandle that carries FSeinVisionComponent.
 *
 * Each tick: a source whose pose AND stamp set hash to the last-tick value
 * skips entirely (the big perf win — most units don't move per tick). On
 * change: we walk the stored Footprints to decrement old refcounts, then
 * re-stamp every shape and accumulate footprints into the new arrays.
 *
 * Cache key is pose (WorldPos + Rotation, since shaped stamps care about
 * orientation) + EyeHeight + a hash of the Stamps array (shape geometry
 * + per-stamp layer mask + bEnabled). When any of those changes the source
 * does the full rebuild.
 */
struct FSeinFogSourceState
{
	bool bValid = false;
	FSeinPlayerID Owner;
	FFixedVector WorldPos;
	FFixedQuaternion Rotation;
	FFixedPoint EyeHeight;
	uint32 StampsHash = 0;

	/** Cells this source last stamped per bit (1-7), aggregated across all
	 *  the source's stamps that emitted on that bit — a building with two
	 *  separate cone stamps both on V folds both footprints into
	 *  Footprints[1]. Empty for bits the source doesn't emit. Bit 0
	 *  (Explored) not tracked — sticky.
	 *
	 *  Invariant: ascending-sorted. Multiset (duplicates kept) so per-stamp
	 *  apex / overlap with covered-cell scans pair off cleanly with the
	 *  next-tick generated set in ApplyFootprintDiff. UpdateSourceStamp
	 *  Sort()s the new generated set before commit; DecrementFootprintsForState's
	 *  Reset() leaves the array empty (sorted by definition). */
	TArray<int32> Footprints[8];
};

UCLASS(BlueprintType, meta = (DisplayName = "Sein Fog Of War (Default)"))
class SEINARTSFOGOFWAR_API USeinFogOfWarDefault : public USeinFogOfWar, public ISeinLevelLayerProvider
{
	GENERATED_BODY()

public:

	// ----------------------------------------------------------------------
	// Designer config
	// ----------------------------------------------------------------------

	/** Vertical extent (world units) above the tallest fog volume that bake
	 *  traces start from. Bump if any sight-blocker geometry sits near the
	 *  top of your volumes. */
	UPROPERTY(EditAnywhere, Category = "Bake", meta = (ClampMin = "0.0"))
	float BakeTraceHeadroom = 400.0f;

	/** Minimum gap (world units) between the top trace hit and the ground
	 *  trace hit below it for the difference to register as a static
	 *  blocker. Filters mesh-thickness noise (e.g. terrain with a few-cm-
	 *  thick top surface shouldn't read as a 3cm blocker). */
	UPROPERTY(EditAnywhere, Category = "Bake", meta = (ClampMin = "0.0"))
	float StaticBlockerMinHeight = 15.0f;

	/** Collision channel bake traces sample for ground + blocker detection.
	 *  ECC_Visibility is the correct default — walls / buildings / terrain
	 *  respond to it by default. Teams with a custom "SightBlocker" channel
	 *  can override here. */
	UPROPERTY(EditAnywhere, Category = "Bake")
	TEnumAsByte<ECollisionChannel> BakeTraceChannel = ECC_Visibility;

	/** Cap cell count for the per-cell terrain trace at init. Beyond this,
	 *  cells snap to volume mid-Z (avoids multi-second editor hitch on huge
	 *  maps when no bake is loaded). Bake itself has no cap — it's an
	 *  explicit designer action with progress UI. */
	UPROPERTY(EditAnywhere, Category = "Bake", meta = (ClampMin = "0"))
	int32 InitTraceCellCap = 4096;

	// ----------------------------------------------------------------------
	// USeinFogOfWar overrides
	// ----------------------------------------------------------------------

	virtual bool HasRuntimeData() const override { return Width > 0 && Height > 0; }

	// ----------------------------------------------------------------------
	// Unified level-data participation (CP1.1): this fog is the "FogOfWar"
	// layer provider on the shared substrate. BakeLayer adopts the substrate's
	// shared ground height (D17) and runs the fog-specific occluder box-sweep
	// at the fog's own (coarser) resolution; LoadFromSubstrate reconstructs the
	// runtime grid from the baked channel + shared coordinate space.
	// ----------------------------------------------------------------------

	// ISeinLevelLayerProvider
	virtual FName GetLayerId() const override;
	virtual void BakeLayer(const USeinLevelData& Substrate, UWorld* World, TArray<uint8>& OutData) override;
	virtual int32 GetCellSizeMultiple() const override { return LastBakedCellSizeMultiple; }

	// USeinFogOfWar participation hooks
	virtual ISeinLevelLayerProvider* GetLevelDataProvider() override { return this; }
	virtual bool LoadFromSubstrate(const USeinLevelData& Substrate) override;

	virtual void InitGridFromVolumes(UWorld* World) override;
	virtual void TickStamps(UWorld* World) override;

	virtual uint8 GetCellBitfield(FSeinPlayerID Observer, const FFixedVector& WorldPos) const override;
	virtual uint8 GetEntityVisibleBits(FSeinPlayerID Observer,
		USeinWorldSubsystem& Sim, FSeinEntityHandle Target) const override;

	virtual void CollectDebugCellQuads(FSeinPlayerID Observer,
		int32 VisibleBitIndex,
		TArray<FVector>& OutCenters,
		TArray<FColor>& OutColors,
		float& OutHalfExtent) const override;

private:

	// ----------------------------------------------------------------------
	// Runtime grid
	// ----------------------------------------------------------------------
	int32 Width = 0;
	int32 Height = 0;
	FFixedPoint CellSize = FFixedPoint::FromInt(400);
	FFixedVector Origin = FFixedVector::ZeroVector;

	/** Per-cell terrain Z (from bake or downward traces at init). Row-major. */
	TArray<FFixedPoint> GroundHeight;

	/** Per-cell static blocker height (absolute world Z of the top of the
	 *  blocker; zero = no static blocker at this cell). Populated by bake;
	 *  shadowcast step consumes it + a parallel dynamic overlay grid. */
	TArray<FFixedPoint> BlockerHeight;

	/** Per-cell static blocker layer mask — bits 1..7 indicate which of
	 *  the EVNNNNNN vis layers this blocker occludes. MVP bake sets all
	 *  vis bits (0xFE) for every detected blocker; per-layer masks
	 *  (e.g. thermal-see-through-smoke) land with the layer pass. */
	TArray<uint8> BlockerLayerMask;

	/** Dynamic blocker overlay — absolute world Z of any runtime-authored
	 *  blocker (smoke grenades, destructibles in progress) at this cell.
	 *  Rebuilt each `TickStamps` from entities carrying `FSeinExtentsComponent` with bBlocksFogOfWar set
	 *  in sim component storage. Zero = no dynamic blocker this tick.
	 *  LOS tests static + dynamic independently (per-blocker layer mask
	 *  is honored separately — see IsCellOpaqueToEye). */
	TArray<FFixedPoint> DynamicBlockerHeight;

	/** Parallel to `DynamicBlockerHeight` — which EVNNNNNN bits the
	 *  dynamic blocker at this cell occludes. OR'd across overlapping
	 *  blockers (e.g. two smoke grenades at the same cell combine their
	 *  masks). */
	TArray<uint8> DynamicBlockerLayerMask;

	/** Fingerprint of last tick's dynamic-blocker entity set (XOR-folded
	 *  per-entity hash of pos + height + radius + mask). Used by
	 *  RebuildDynamicBlockers to detect smoke-changed-since-last-tick
	 *  and force a full source-state invalidation when it does. */
	uint32 LastDynamicBlockerHash = 0;

	/** Cells written into the dynamic-blocker overlay last tick. Drives the
	 *  dirty-rect clear at the top of RebuildDynamicBlockers — only those
	 *  indices get zeroed, instead of memzeroing the full W*H arrays. Most
	 *  ticks dynamic blockers cover ≪ W*H cells, so this is a massive win
	 *  on large maps (saves ~50MB/tick of array clear at 1km² @ 400cm cells).
	 *  Reset (without shrinking allocation) every tick; populated by
	 *  StampDynamicBlockerShape on first-write per cell per tick. May
	 *  contain duplicates only across grid-resize transitions (handled by
	 *  the size-mismatch fallback in RebuildDynamicBlockers). */
	TArray<int32> LastDynamicBlockerCells;

	/** Per-observer visibility state. Keyed by FSeinPlayerID; lazily
	 *  created on first stamp by that owner. Each group's CellBitfield is
	 *  sized Width*Height; bit 0 is sticky Explored, bits 1..7 are
	 *  refcount-managed (set on 0→1, cleared on 1→0). Neutral (player ID
	 *  0) is a legal key — useful for neutral-owned structures with cheap
	 *  passive vision. */
	TMap<FSeinPlayerID, FSeinFogVisionGroup> VisionGroups;

	/** Per-source last-stamped memo. Drives the delta-refcount path:
	 *  TickStamps walks live entities, calls UpdateSourceStamp which
	 *  short-circuits on identical inputs, otherwise removes the old
	 *  footprint + re-stamps. Sources that vanish (entity destroyed,
	 *  vision component removed) get their footprint torn down on the
	 *  next tick. */
	TMap<FSeinEntityHandle, FSeinFogSourceState> SourceStates;

	// ----------------------------------------------------------------------
	// Bake state
	// ----------------------------------------------------------------------

	/** The fog cell size used by the last BakeLayer, as a multiple of the
	 *  substrate's finest cell size (snapped to an integer; D13/D15). Read by
	 *  the bake orchestration via GetCellSizeMultiple after BakeLayer. */
	int32 LastBakedCellSizeMultiple = 1;

	// ----------------------------------------------------------------------
	// Helpers
	// ----------------------------------------------------------------------
	FORCEINLINE int32 CellIndex(int32 X, int32 Y) const { return Y * Width + X; }
	FORCEINLINE bool IsValidCoord(int32 X, int32 Y) const { return X >= 0 && X < Width && Y >= 0 && Y < Height; }

	bool WorldToGrid(const FFixedVector& WorldPos, int32& OutX, int32& OutY) const;

	/** Get-or-create the VisionGroup for a player. Lazy-inits the bitfield
	 *  to Width*Height when first observed; reuses across ticks (V bits
	 *  clear, Explored stays sticky). */
	FSeinFogVisionGroup& GetOrCreateGroup(FSeinPlayerID PlayerID);

	/** Per-tick entry — change-detect against the source's last stamp
	 *  state. Identical inputs ⇒ no work, returns false. Changed inputs ⇒
	 *  remove the old footprint, compute the new one, apply refcount deltas,
	 *  returns true. The stable-source path is the entire point of opt 2:
	 *  most units don't cross cells per tick, so most calls return early
	 *  after a few compares.
	 *
	 *  TickStamps consumes the return value to gate OnFogOfWarMutated —
	 *  without that gate, an idle world (no movement, no smoke change) would
	 *  still broadcast every tick, forcing the debug component to rebuild
	 *  its scene proxy at vision-tick frequency even with the showflag off. */
	bool UpdateSourceStamp(FSeinEntityHandle Handle, const FSeinVisionComponent& VData,
		const FFixedVector& WorldPos, const FFixedQuaternion& Rotation,
		FSeinPlayerID Owner);

	/** Tear down a source's footprints — decrement refcounts on every
	 *  stamped cell, clear bits where refcount hits 0, erase the state
	 *  entry. Called when an entity vanishes (destroyed, vision data
	 *  stripped) and during re-stamps from UpdateSourceStamp. Explored
	 *  bit is sticky and never decremented. */
	void RemoveSourceStamp(FSeinEntityHandle Handle);

	/** Generate the cell-index footprint for one stamp × one layer-bit, with
	 *  no refcount or bitfield mutation. Produces the candidate cells (apex
	 *  + LOS-visible covered cells) by appending into OutCells. Caller is
	 *  expected to union per-bit results across all stamps emitting on that
	 *  bit, then sort + diff against last tick's stored footprint via
	 *  ApplyFootprintDiff.
	 *
	 *  Pose: the stamp's apex world position is `EntityWorldPos +
	 *  Quat(EntityYaw)·LocalOffset` — so a building window cone casts from
	 *  the window, not the building center. EntityRotation also drives
	 *  rect/cone facing alongside the stamp's YawOffsetDegrees.
	 *
	 *  StampBit ∈ [1, 7]: 1 = V (Normal), 2..7 = N0..N5 custom layers.
	 *  Lampshade model: eye Z = `EntityWorldPos.Z + EyeHeight` — the
	 *  unit's actual sim Z (NOT the cell's baked GroundHeight, which
	 *  stores terrain BENEATH any blocker; a unit standing on a climbable
	 *  platform sees from its standing surface). Terrain occludes
	 *  layer-agnostically; static / dynamic blockers only occlude when
	 *  their LayerMask covers this stamp's bit — smoke that blocks Normal
	 *  but not Thermal lets a Thermal stamp pass through.
	 *
	 *  Per-target LOS is integer Bresenham (O(R³) per stamp; deterministic
	 *  and trivial to verify). Cells appended to OutCells are unsorted and
	 *  may include duplicates across multiple stamps (apex always emitted
	 *  first per stamp; LOS-walked cells appended after). Sort happens in
	 *  the caller before diffing. */
	void GenerateLayerFootprintCells(
		const FSeinStampShape& Shape,
		const FFixedVector& EntityWorldPos,
		const FFixedQuaternion& EntityRotation,
		FFixedPoint EyeHeight, uint8 StampBit,
		TArray<int32>& OutCells);

	/** Diff `OldSorted` against `NewSorted` (both ascending, multiset
	 *  semantics — duplicates pair off in iteration order) and apply the
	 *  minimal refcount + bitfield delta to `Group` for `StampBit`:
	 *   - Cells in OLD only: refcount decremented, bit cleared on 1→0
	 *   - Cells in NEW only: refcount incremented, bit set on 0→1, Explored
	 *     bit set (sticky)
	 *   - Cells in BOTH: skipped — refcount + bitfield already correct from
	 *     last tick's stamping
	 *
	 *  Replaces "decrement-everything then increment-everything" with
	 *  diff-only updates. For typical movement (unit shifts one cell), the
	 *  intersection is huge and the diff size is tiny, so most cells touch
	 *  zero refcount writes per tick.
	 *
	 *  Defensive against grid-resize: skips cells whose index is invalid
	 *  for the current Group sizing (matches DecrementFootprintsForState's
	 *  IsValidIndex pattern). RefCounts[StampBit] is lazy-allocated to
	 *  Width*Height on first stamp into this bit. */
	void ApplyFootprintDiff(
		FSeinFogVisionGroup& Group,
		uint8 StampBit,
		const TArray<int32>& OldSorted,
		const TArray<int32>& NewSorted);

	/** Integer-Bresenham LOS from (X0,Y0,EyeZ) to (X1,Y1,TargetZ). At each
	 *  intermediate cell the ray's Z is linearly interpolated between
	 *  EyeZ and TargetZ; the cell is tested against that interpolated Z
	 *  (not the constant EyeZ). This is what gives true-sight its
	 *  elevation behavior: a unit on a roof looking DOWN at the ground
	 *  has its ray descend, so a wall halfway between them — shorter than
	 *  the unit's eye but taller than the ray's Z at that midpoint — will
	 *  still occlude. Only blockers whose LayerMask covers `StampBitMask`
	 *  contribute. */
	bool HasLineOfSightToCell(int32 X0, int32 Y0, int32 X1, int32 Y1,
		FFixedPoint EyeZ, FFixedPoint TargetZ, uint8 StampBitMask) const;

	/** Cell is opaque to an eye at EyeZ iff any of:
	 *   - GroundHeight > EyeZ (terrain always occludes), OR
	 *   - Static BlockerHeight > EyeZ AND BlockerLayerMask & StampBitMask, OR
	 *   - Dynamic BlockerHeight > EyeZ AND DynamicBlockerLayerMask & StampBitMask.
	 *  Static + dynamic test independently — a short-but-layered dynamic
	 *  blocker doesn't inherit a tall static blocker's reach, and vice
	 *  versa. Lets smoke-blocks-Normal-but-not-Thermal style policies work
	 *  correctly alongside baked geometry. */
	bool IsCellOpaqueToEye(int32 X, int32 Y, FFixedPoint EyeZ, uint8 StampBitMask) const;

	/** Clear the dynamic blocker overlay, then walk every entity carrying
	 *  `FSeinExtentsComponent` (with bBlocksFogOfWar) and stamp its shape contribution into the
	 *  overlay. Runs at the top of TickStamps so the vision passes below
	 *  see the freshest occlusion state. Returns true if the overlay's
	 *  contents differ from last tick — TickStamps uses that to invalidate
	 *  every source's stable-fast-path cache (otherwise a smoke grenade
	 *  appearing on a sightline wouldn't update LOS until the source
	 *  itself moved or changed props). */
	bool RebuildDynamicBlockers(UWorld* World);

	/** Walk every per-bit footprint stored on `State`, decrement the
	 *  matching refcount in `Group`, clear bits where refcount hits 0,
	 *  and reset the footprint arrays. Explored bit is sticky and never
	 *  touched. Used by RemoveSourceStamp + UpdateSourceStamp's
	 *  "old-then-new" path. */
	void DecrementFootprintsForState(FSeinFogSourceState& State, FSeinFogVisionGroup& Group);

	/** Stamp one shape's footprint of effective height + layer mask into
	 *  the dynamic blocker overlay. `HeightAboveGround` applies uniformly
	 *  across the shape's cells; at each cell the stored value is
	 *  `GroundHeight[Idx] + HeightAboveGround` (absolute world Z, matching
	 *  the static array convention). Multiple overlapping stamps take the
	 *  taller height + OR'd layer mask per cell. */
	void StampDynamicBlockerShape(const FSeinStampShape& Shape,
		const FFixedVector& EntityWorldPos,
		const FFixedQuaternion& EntityRotation,
		FFixedPoint HeightAboveGround, uint8 LayerMask);
};

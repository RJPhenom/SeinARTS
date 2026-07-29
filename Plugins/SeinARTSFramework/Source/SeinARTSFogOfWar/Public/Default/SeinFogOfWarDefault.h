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
 *          - Stamp cadence: driven by an ordered PostTick simulation system,
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
 *          - Shadowcast + lampshade eye-height blocker test (elevation-aware true-line-of-sight)
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
// FSeinFogStampWork embeds TArray<FSeinVisionStamp> by value (the per-source
// stamp set the parallel footprint compute rasterizes), so the full definition
// must be visible here — a forward declaration no longer suffices.
#include "Components/SeinVisionComponent.h"
#include "SeinFogOfWarDefault.generated.h"

class UWorld;
class USeinLevelData;
struct FSeinStampShape;
struct FSeinFogOfWarDefaultStateCodec;

#if WITH_DEV_AUTOMATION_TESTS
namespace UE::SeinARTSTests
{
	struct FFogOfWarDefaultTestAccess;
}
#endif

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

	/** Entities this observer has had live vision of at least once this match
	 *  — the per-entity sticky latch backing the VisibleOnceSeen policy.
	 *  Parallel to the per-cell sticky Explored bit (CellBitfield bit 0), but
	 *  keyed by entity instead of cell, so a thing that appears in explored-
	 *  but-unseen fog isn't revealed until it is actually seen. Added to by
	 *  UpdateSeenLatches each tick; read by HasObserverSeenEntity. Entity
	 *  handles are generational, so a recycled pool slot can't false-positive;
	 *  stale entries for destroyed entities are harmless (bounded, never
	 *  matched again). Reset with the rest of the group on grid reload. */
	TSet<FSeinEntityHandle> SeenEntities;
};

/**
 * Per-source memo of the last stamped state for delta-refcount updates.
 * One per live FSeinEntityHandle that carries FSeinVisionComponent.
 *
 * Each tick: a source whose pose AND exact effective stamp set equal the last-tick value
 * skips entirely (the big perf win — most units don't move per tick). On
 * change: we walk the stored Footprints to decrement old refcounts, then
 * re-stamp every shape and accumulate footprints into the new arrays.
 *
 * Cache key is pose (WorldPos + Rotation, since shaped stamps care about
 * orientation) + EyeHeight + the Stamps array (shape geometry
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
	TArray<FSeinVisionStamp> Stamps;

	/** Cells this source last stamped per bit (1-7), aggregated across all
	 *  the source's stamps that emitted on that bit — a building with two
	 *  separate cone stamps both on V folds both footprints into
	 *  Footprints[1]. Empty for bits the source doesn't emit. Bit 0
	 *  (Explored) not tracked — sticky.
	 *
	 *  Invariant: ascending-sorted. Multiset (duplicates kept) so per-stamp
	 *  apex / overlap with covered-cell scans pair off cleanly with the
	 *  next-tick generated set in ApplyFootprintDiff. TickStamps' parallel
	 *  compute Sort()s the new generated set before the serial apply commits
	 *  it here; DecrementFootprintsForState's Reset() leaves the array empty
	 *  (sorted by definition). */
	TArray<int32> Footprints[8];
};

/** Exact input to one dynamic-blocker rasterization. The entity pool and each
 *  component's Shapes array provide canonical ordering, so array equality is
 *  a collision-free change detector. Identity is intentionally absent: two
 *  entities producing byte-for-byte equivalent overlay inputs are visually
 *  equivalent. */
struct FSeinFogDynamicBlockerSnapshot
{
	FFixedVector WorldPos;
	FFixedQuaternion Rotation;
	FSeinStampShape Shape;
	FFixedPoint Height;
	uint8 LayerMask = 0;

	FORCEINLINE bool operator==(const FSeinFogDynamicBlockerSnapshot& Other) const
	{
		return WorldPos == Other.WorldPos
			&& Rotation == Other.Rotation
			&& Shape == Other.Shape
			&& Height == Other.Height
			&& LayerMask == Other.LayerMask;
	}

	FORCEINLINE bool operator!=(const FSeinFogDynamicBlockerSnapshot& Other) const
	{
		return !(*this == Other);
	}
};

/**
 * Computes what each player can see: hides the map under fog, reveals cells around units that have
 * vision, and remembers explored ground once you've been there. This is the fog-of-war system
 * selected out of the box.
 *
 * Each observer (one per player ID) gets its own grid of per-cell visibility bits: bit 0 is a
 * sticky Explored flag that stays lit for the rest of the match once revealed, and bits 1-7 are
 * live vision layers (bit 1 = Normal sight, bits 2-7 = custom layers such as Thermal or Stealth)
 * that clear the moment nothing is looking. Live bits are reference-counted per cell, so a cell
 * stays visible while any source covers it and only goes dark when the last one leaves.
 *
 * Vision is stamped each sim tick from every entity carrying a vision component: for each source
 * an eye position is taken at the unit's sim Z plus its EyeHeight, and line-of-sight to every cell
 * in range is tested with an integer Bresenham walk whose ray Z is interpolated from the eye down
 * to the target cell. That elevation-aware trace is the true-sight behavior — a unit on a roof
 * looking down still has a wall between them block the far ground. Terrain always occludes; static
 * baked blockers and runtime dynamic blockers (smoke, destructibles) occlude only when their layer
 * mask covers the stamp's bit, so smoke can hide Normal sight while letting Thermal through. A
 * delta-refcount cache short-circuits any source whose pose and stamp set are unchanged since last
 * tick, so most stationary units cost nothing; changed sources fan out across worker threads
 * (parallel compute, serial apply). All observers stamp against the same tick-N snapshot, keeping
 * the result lockstep-identical on every client.
 *
 * This is also the "FogOfWar" layer provider on the unified level-data bake: it runs its own
 * occluder box-sweep at its own coarser cell size (an integer multiple of the shared grid) and
 * adopts the substrate's shared ground height, loading its runtime grid back from that baked
 * channel. When no bake is present the grid auto-sizes from the Sein Level Volumes in the level.
 */
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

	virtual bool HasObserverSeenEntity(FSeinPlayerID Observer,
		FSeinEntityHandle Target) const override;

	virtual void CollectDebugCellQuads(FSeinPlayerID Observer,
		int32 VisibleBitIndex,
		TArray<FVector>& OutCenters,
		TArray<FColor>& OutColors,
		float& OutHalfExtent) const override;

	/** Hands back a copy of `Observer`'s row-major CellBitfield + grid geometry.
	 *  Zero-filled (but true) when the observer has no group yet; false only when
	 *  the grid is uninitialized. See USeinFogOfWar::GetObserverGrid. */
	virtual bool GetObserverGrid(FSeinPlayerID Observer, TArray<uint8>& OutCells,
		FFixedVector& OutOrigin, FFixedPoint& OutCellSize,
		int32& OutWidth, int32& OutHeight) const override;

private:

	friend struct FSeinFogOfWarDefaultStateCodec;

#if WITH_DEV_AUTOMATION_TESTS
	friend struct UE::SeinARTSTests::FFogOfWarDefaultTestAccess;
#endif

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

	/** Exact ordered rasterization inputs currently represented in the
	 *  dynamic overlay. An identical next-tick list retains the overlay; a
	 *  change dirty-clears/rebuilds it and invalidates affected source caches. */
	TArray<FSeinFogDynamicBlockerSnapshot> DynamicBlockerSnapshots;

	/** Cells written into the dynamic-blocker overlay last tick. Drives the
	 *  dirty-rect clear at the top of RebuildDynamicBlockers — only those
	 *  indices get zeroed, instead of memzeroing the full W*H arrays. Most
	 *  ticks dynamic blockers cover ≪ W*H cells, so this is a massive win
	 *  on large maps (saves ~50MB/tick of array clear at 1km² @ 400cm cells).
	 *  Reset (without shrinking allocation) whenever the blocker snapshot changes; populated by
	 *  StampDynamicBlockerShape on first-write per cell per tick. May
	 *  contain duplicates only across grid-resize transitions (handled by
	 *  the size-mismatch fallback in RebuildDynamicBlockers). Grid reloads
	 *  explicitly reset it before adopting a new index space. */
	TArray<int32> LastDynamicBlockerCells;

	/** Per-observer visibility state. Keyed by FSeinPlayerID; lazily
	 *  created on first stamp by that owner. Each group's CellBitfield is
	 *  sized Width*Height; bit 0 is sticky Explored, bits 1..7 are
	 *  refcount-managed (set on 0→1, cleared on 1→0). Neutral (player ID
	 *  0) is a legal key — useful for neutral-owned structures with cheap
	 *  passive vision. */
	TMap<FSeinPlayerID, FSeinFogVisionGroup> VisionGroups;

	/** Per-source last-stamped memo. Drives the delta-refcount path:
	 *  TickStamps' serial change-detection short-circuits a source on
	 *  identical inputs, otherwise queues it for the parallel footprint
	 *  compute + serial diff-apply (which removes the old footprint + commits
	 *  the new). Sources that vanish (entity destroyed, vision component
	 *  removed) get their footprint torn down on the next tick. */
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

	/** Update the per-observer VisibleOnceSeen latch: for every entity authored
	 *  with that policy, add it to each vision group whose live (non-Explored)
	 *  bits currently cover the entity's footprint. Sticky — once added, an
	 *  entity is never removed, so it ghost-reveals for the rest of the match.
	 *  Called at the tail of TickStamps off the freshly-stamped grid (so a
	 *  thing that spawns inside existing vision latches the same tick it
	 *  appears). Deterministic: reads only the per-player grid + sim state,
	 *  identical on every client. */
	void UpdateSeenLatches(USeinWorldSubsystem& Sim);

	/** One changed vision source queued by TickStamps' serial gather for the
	 *  parallel footprint compute + serial apply (the "parallel-compute,
	 *  serial-apply" pattern — mirrors FSeinAvoidanceSystem / the Jacobi
	 *  collision resolver). Holds everything the parallel body needs (pose +
	 *  the possibly terrain-scaled stamp set + owner), the change-detection
	 *  exact stamp set the serial commit will write back, and the per-bit
	 *  cell-list SCRATCH the parallel body fills (the disjoint per-source write
	 *  slot). Only sources whose pose/stamps changed since last tick get a
	 *  work item; unchanged sources skip exactly as the old stable-fast-path did.
	 *
	 *  GenScratch[bit] (1..7): the new footprint cells this tick, generated +
	 *  sorted by the parallel body, consumed by the serial ApplyFootprintDiff
	 *  and then moved into FSeinFogSourceState::Footprints[bit]. Body-disjoint:
	 *  body i writes only WorkItems[i].GenScratch, never another item's. */
	struct FSeinFogStampWork
	{
		FSeinEntityHandle      Handle;
		FSeinPlayerID          Owner;
		FFixedVector           WorldPos;
		FFixedQuaternion       Rotation;
		FFixedPoint            EyeHeight;
		TArray<FSeinVisionStamp> Stamps;   // the (possibly terrain-scaled) stamp set to rasterize
		TArray<int32>          GenScratch[8];
	};

	/** Tear down a source's footprints — decrement refcounts on every
	 *  stamped cell, clear bits where refcount hits 0, erase the state
	 *  entry. Called from TickStamps when an entity vanishes (destroyed,
	 *  vision data stripped); a still-live source that merely moved is
	 *  diffed in place by the serial apply, not torn down. Explored bit is
	 *  sticky and never decremented. */
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
	 *  the caller before diffing.
	 *
	 *  CONST + PURE: reads only the immutable per-tick grid state (GroundHeight
	 *  / Blocker / DynamicBlocker arrays, all finalized before the source loop)
	 *  plus its own arguments, and appends into the caller-owned OutCells. This
	 *  is what lets TickStamps call it from inside a SeinParallelFor body — each
	 *  body writes only its own scratch slot, never any FoW member. */
	void GenerateLayerFootprintCells(
		const FSeinStampShape& Shape,
		const FFixedVector& EntityWorldPos,
		const FFixedQuaternion& EntityRotation,
		FFixedPoint EyeHeight, uint8 StampBit,
		TArray<int32>& OutCells) const;

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
	 *  touched. Used by RemoveSourceStamp + TickStamps' serial apply when a
	 *  source changes owner (footprints can't diff across per-player groups,
	 *  so the old group is fully decremented and the new one diffed from an
	 *  empty baseline). */
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

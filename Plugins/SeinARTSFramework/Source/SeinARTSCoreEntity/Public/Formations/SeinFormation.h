/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFormation.h
 * @brief   Abstract Blueprintable formation — given an order target + member set,
 *          compute per-member world positions and the formation's facing.
 *
 *          The pluggable "how do N units arrange" seam, decoupled from dispatch
 *          (which ability / which member — that stays on the command broker
 *          resolver). Stateless / pure compute: the framework invokes formations
 *          on their CDO (formations carry only config UPROPERTYs, never per-order
 *          state), so there is no instancing or pooling. Deterministic — fixed-
 *          point only, no float / RNG — because the destination preview calls this
 *          EXACTLY as the commit dispatch does and the two must agree bit-for-bit
 *          (root CLAUDE invariant #6) and lockstep must not desync.
 *
 *          Framework ships USeinBlobFormation (shared destination — the default),
 *          USeinGridFormation, USeinBoxFormation, USeinWedgeFormation,
 *          USeinRingFormation and USeinSquareFormation. Designers subclass for
 *          custom shapes and select one via the command broker resolver's formation
 *          class / tag map.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Core/SeinEntityHandle.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "Brokers/SeinBrokerTypes.h"
#include "SeinFormation.generated.h"

class USeinWorldSubsystem;

/**
 * Result of USeinFormation::PackFootprints — a tight cell-grid packing of mixed-footprint members.
 * Span / BlockRow / BlockCol are index-aligned with the Radii passed in. Cell is the grid unit (the
 * smallest member's diameter, floored at MinCell); each member occupies a Span×Span block whose
 * top-left cell is (BlockRow, BlockCol), row 0 = front. Columns/Rows are the allocated grid extents
 * (Rows over-allocates so the big-box pass always fits — callers centre on the OCCUPIED bounds, not
 * these). Pure compute, no UObject — a plain value struct shared by the Grid and Box formations.
 */
struct FSeinFootprintPacking
{
	FFixedPoint   Cell = FFixedPoint::Zero;
	TArray<int32> Span;
	TArray<int32> BlockRow;
	TArray<int32> BlockCol;
	int32         Columns = 1;
	int32         Rows    = 1;
};

UCLASS(Abstract, Blueprintable, EditInlineNew, ClassGroup = (SeinARTS),
	meta = (DisplayName = "Formation"))
class SEINARTSCOREENTITY_API USeinFormation : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Lay out the given members for an order. Returns per-member world positions
	 * (index-aligned with `Members`) plus the formation's facing at the target.
	 *
	 * Pure compute: MUST NOT mutate sim state. Deterministic (fixed-point only) —
	 * the destination preview calls this exactly as commit dispatch does, so a
	 * non-deterministic override splits preview from commit AND desyncs lockstep.
	 *
	 * `Target` carries the guide geometry (GuidePoints), the anchor, and the
	 * formation's current centroid / facing (filled by the resolver). Default impl
	 * is a blob: every member to `Target.Anchor`, facing rotated centroid → anchor.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Formation", meta = (DisplayName = "Build Formation"))
	FSeinFormationLayout BuildFormation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target);
	virtual FSeinFormationLayout BuildFormation_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target);

	/**
	 * Yaw-only facing that rotates the formation forward axis from `CurrentCentroid`
	 * toward `TargetLocation` (2-D, top-down — vertical ignored). Move-to-where-we-
	 * stand keeps `CurrentFacing` rather than degenerating to identity.
	 *
	 * Pure compute, static — preview consumers (no instance) call it directly.
	 * Shared by every stock formation and the default resolver's fallback path.
	 * (Moved here from the default broker resolver — the formation is now the home
	 * of facing logic.)
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Compute Formation Facing"))
	static FFixedQuaternion ComputeFormationFacing(
		FFixedVector CurrentCentroid,
		FFixedQuaternion CurrentFacing,
		FFixedVector TargetLocation);

	/** Yaw-only facing that points the formation forward axis along `DirectionXY`
	 *  (XY plane; Z ignored). Identity for a near-zero direction. The primitive
	 *  behind ComputeFormationFacing; formations call it directly for facings that
	 *  aren't "toward the target" — e.g. a line facing perpendicular to itself. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Facing From Direction"))
	static FFixedQuaternion FacingFromDirection(FFixedVector DirectionXY);

	/** Facing DIRECTION (XY) for a right-click-drag: the guide line's perpendicular on a
	 *  fixed handedness derived from the drag DIRECTION (Start->End rotated a quarter turn).
	 *  The drag is the authority: facing is independent of unit/centroid position, so a
	 *  dragged formation faces by how the line was drawn, not where the units stand. Returns
	 *  the zero vector when the guide isn't a usable 2+ point line (caller keeps its non-drag
	 *  facing). Feed the result to FacingFromDirection; drag the other way to flip the side. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Drag Facing Direction"))
	static FFixedVector DragFacingDir(const TArray<FFixedVector>& GuidePoints);

	/**
	 * Project a candidate slot to navigable ground (per-slot, occupancy-BLIND). On WALKABLE terrain the
	 * slot's X/Y is kept exactly and only its Z is snapped to the ground (a formation keeps its top-down
	 * shape on a hill). A slot off the play area / on an obstacle is snapped to the NEAREST WALKABLE cell
	 * on the nav grid — independent of where the other slots land, so two slots can snap to the same edge
	 * cell here; the resolver's batch `ProjectPositionsToNavigable` (occupancy-aware) + `SeparatePositions`
	 * passes are what guarantee the final layout is both on-nav and non-overlapping. Returns the raw
	 * `Position` when no nav is bound (tests / nav-less games), and `Fallback` only when there is no World
	 * at all. Deterministic — the nav grid is baked.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Project To Navigable"))
	static FFixedVector ProjectToNavigable(
		USeinWorldSubsystem* World,
		FFixedVector Position,
		FFixedVector Fallback);

	/**
	 * Footprint radius (world units) of an entity — the basis for footprint-aware
	 * formation spacing and preview dot sizing. Reads FSeinExtentsComponent: a
	 * Capsule shape contributes its Radius, a Box shape its circumscribed radius
	 * (√(hx²+hy²), orientation-independent so a rotating formation never overlaps),
	 * taking the MAX over all shapes (each pushed out by its LocalOffset XY). No
	 * extents / empty shapes → an ~infantry-sized fallback, so spacing degrades to
	 * the old uniform feel rather than collapsing to zero. Deterministic.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Get Footprint Radius"))
	static FFixedPoint GetFootprintRadius(USeinWorldSubsystem* World, FSeinEntityHandle Handle);

	/** Fill OutRadii (index-aligned with Members) with each member's footprint
	 *  radius via GetFootprintRadius. */
	static void GatherFootprintRadii(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		TArray<FFixedPoint>& OutRadii);

	/** Centered 1-D placement that never overlaps footprints. Returns per-member
	 *  signed offsets along an axis: adjacent centers are spaced max(MinGap,
	 *  r[i]+r[i+1]); if TargetLength exceeds the tight span the slack is shared
	 *  evenly across gaps (a long drag spreads the line without overlap), otherwise
	 *  the tight no-overlap span is used. Centered on 0; index-aligned with Radii.
	 *  Pass TargetLength = 0 for the pure no-overlap span. */
	static void Spread1D(
		const TArray<FFixedPoint>& Radii,
		FFixedPoint MinGap,
		FFixedPoint TargetLength,
		TArray<FFixedPoint>& OutOffsets);

	/** Member indices sorted by footprint radius DESCENDING (largest first), tie-broken by
	 *  index for determinism. Formations place big units first for clean size-graded shapes
	 *  (wedge tip, box front-centre, grid embed). */
	static TArray<int32> SortIndicesByRadiusDesc(const TArray<FFixedPoint>& Radii);

	/**
	 * Pack members (by footprint radius) into a tight cell grid for a clean size-graded block.
	 * Algorithm: the cell is the SMALLEST footprint diameter (floored at MinCell); each member
	 * becomes a ceil(diameter/cell)² block; the biggest blocks are placed front-and-centre (most-
	 * forward row, most-central free column there) and the 1×1s fill every remaining cell row-major.
	 * `DesiredFrontWidth` > 0 sets the front width in WORLD units (a drag draws it) — the grid is as
	 * many cells wide as span it; <= 0 → a square-ish grid (Columns = ceil(sqrt(total cells))). The
	 * column count is never narrower than the biggest box and is nudged so an even-span big box
	 * centres. Deterministic (fixed-point only). The shared packer behind the Grid (square) and Box
	 * (drag-width) formations; each applies its own world anchoring to the returned blocks.
	 */
	static void PackFootprints(
		const TArray<FFixedPoint>& Radii,
		FFixedPoint DesiredFrontWidth,
		FFixedPoint MinCell,
		FSeinFootprintPacking& Out);

	/**
	 * De-overlap / de-dup safety pass: push apart any two positions whose CENTRES are closer than the
	 * sum of their footprint radii (r_i + r_j) until none overlap — using each member's ACTUAL
	 * footprint, so a big unit clears more room than a small one. Touching (centre distance == r_i +
	 * r_j) is left alone; only strict overlaps move. Coincident points separate along a deterministic
	 * index-derived direction. A bounded, deterministic relaxation (fixed pair order, fixed-point) so
	 * no formation ever returns two members on top of each other. Index-aligned with Radii.
	 */
	static void SeparatePositions(
		const TArray<FFixedPoint>& Radii,
		TArray<FFixedVector>& Positions,
		int32 MaxIterations);

	/**
	 * Final off-nav safety pass: any position that landed OFF the nav area is projected to the nearest
	 * FREE cell — walkable AND not within footprint distance of any other slot — with every peer slot
	 * treated as occupied so two overflowing slots never resolve onto the same spot. Positions already
	 * on the nav area are left EXACTLY where they are (this never reshapes an in-bounds formation). The
	 * shared net the resolver runs AFTER SeparatePositions so a formation spilling past the play-area
	 * edge (a blob spread off a corner, a slot the de-overlap push shoved out) packs onto the inside
	 * edge instead of floating in the void. Index-aligned with Radii; a missing radius counts as zero.
	 * No-op when no nav is bound (tests / nav-less games). Deterministic — the nav grid is baked.
	 */
	static void ProjectPositionsToNavigable(
		USeinWorldSubsystem* World,
		const TArray<FFixedPoint>& Radii,
		TArray<FFixedVector>& Positions);
};

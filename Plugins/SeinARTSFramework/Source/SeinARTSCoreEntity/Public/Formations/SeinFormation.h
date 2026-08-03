/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFormation.h
 * @brief   Abstract Blueprintable formation — given an order target + member set,
 *          compute per-member world positions and the formation's facing.
 *
 *          The pluggable "how do N units arrange" seam, decoupled from dispatch
 *          (which ability / which member — that stays on the command broker
 *          resolver). Stateless / pure compute: the framework invokes formations
 *          on per-world scratch instances copied from their CDO configuration;
 *          reflected writes are rejected and never persist between calls.
 *          Deterministic — fixed-
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
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinFootprintPacking
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Formation")
	FFixedPoint   Cell = FFixedPoint::Zero;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Formation")
	TArray<int32> Span;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Formation")
	TArray<int32> BlockRow;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Formation")
	TArray<int32> BlockCol;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Formation")
	int32         Columns = 1;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Formation")
	int32         Rows    = 1;
};

/**
 * How a formation orients each member relative to its slot. Uniform = everyone faces the formation
 * facing (box / grid / line / blob); RadialOutward / RadialInward = face away from / toward the
 * formation centre (ring, square). The parent formation hands the resulting facing to each element —
 * a squad rotates its whole authored body to it. Resolved AFTER layout from the FINAL positions, so
 * it survives the de-overlap / nav-projection passes.
 */
UENUM(BlueprintType)
enum class ESeinFormationFacing : uint8
{
	Uniform        UMETA(DisplayName = "Uniform"),
	RadialOutward  UMETA(DisplayName = "Radial Outward"),
	RadialInward   UMETA(DisplayName = "Radial Inward"),
};

/**
 * Decides how a group of ordered units arranges itself on the ground when you give them a move
 * order — the shape they spread into (a blob, a grid, a box, a wedge, a ring, a square) and which
 * way they end up facing. This is the abstract base; pick a concrete shape, or subclass it in
 * Blueprint to author your own.
 *
 * Given an order target plus the set of members, a formation computes each member's world position
 * (index-aligned with the members) and the formation's facing. It is the pluggable "how do N units
 * arrange" seam, kept separate from dispatch (which ability runs, which member gets it — that stays
 * on the command broker resolver). Formations are stateless pure compute: the framework runs them on
 * per-world scratch instances initialized from class-default configuration, and rejects any reflected
 * state written by an invocation. They are strictly deterministic — fixed-point math only, no float and no
 * RNG — because the on-screen destination preview calls the exact same formation code the committed
 * order does, and the two must agree bit-for-bit while lockstep networking must never desync.
 *
 * The base ships a small footprint-aware toolkit shared by every shape: real-radius spacing that
 * never overlaps (a big unit clears more room than a small one), 1-D line spreading, a size-graded
 * cell-grid packer that places the biggest units front-and-centre, a de-overlap relaxation pass, and
 * nav projection that folds any slot spilling off the play area back onto the nearest walkable cell.
 * Framework subclasses are Formation (Blob) — everyone to the shared anchor, the default — plus the
 * Grid, Box, Wedge, Ring and Square formations. Designers subclass for custom shapes and select one
 * on the command broker resolver's formation class / tag map.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, ClassGroup = (SeinARTS),
	meta = (DisplayName = "Formation"))
class SEINARTSCOREENTITY_API USeinFormation : public UObject
{
	GENERATED_BODY()

public:
	/** Per-member facing policy (see ESeinFormationFacing). Default Uniform; Ring / Square default to
	 *  RadialOutward via their constructors. Designer-overridable per formation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Facing Mode"))
	ESeinFormationFacing FacingMode = ESeinFormationFacing::Uniform;

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
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Formation", meta = (DisplayName = "Build Formation", SeinDeterministic))
	FSeinFormationLayout BuildFormation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target);
	virtual FSeinFormationLayout BuildFormation_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target);

	/**
	 * Execute one formation through the framework's stateless boundary.
	 *
	 * The CDO is configuration only. Runtime work happens on a per-world scratch
	 * instance, under read-only world access, and every reflected property must
	 * still match the CDO after the call. A violation fail-stops the world's
	 * deterministic execution contract. Reentrant calls receive an isolated
	 * temporary instance, so nested preview/dispatch cannot observe partial state.
	 */
	static bool ExecuteStateless(
		USeinWorldSubsystem* World,
		const UClass* FormationClass,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target,
		FSeinFormationLayout& OutLayout,
		ESeinFormationFacing& OutFacingMode,
		FString* OutError = nullptr);

	/**
	 * Native admission tripwire for the stateless execution boundary. The base
	 * admits Blueprints whose nearest native anchor is USeinFormation. Every
	 * concrete native subclass must override and explicitly admit its own anchor;
	 * admission is never inherited accidentally by a later native subclass.
	 */
	virtual bool IsStatelessExecutionAdmitted(FString& OutError) const;

	/**
	 * Whether this formation places members at their squad's AUTHORED per-slot OffsetTransforms
	 * (FSeinSquadComponent::Slots) rather than computing positions from footprints — only the slot
	 * formation does. Editor hint: a squad shows its per-slot OffsetTransform authoring ONLY when its
	 * chosen Formation Class returns true here (a footprint-laid formation ignores the offsets, so
	 * authoring them would mislead). Pure config query — no sim state. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Formation", meta = (DisplayName = "Uses Authored Slot Offsets"))
	bool UsesAuthoredSlotOffsets() const;
	virtual bool UsesAuthoredSlotOffsets_Implementation() const { return false; }

	/**
	 * Build the order target for laying out a COMPOSITE element's own contents (a squad's members) around
	 * the anchor the PARENT formation gave it. Carries the anchor, centroid and parent-given facing + the
	 * element's own FormationClass — but DELIBERATELY no gesture guide and no formation tag: the parent
	 * formation already consumed the gesture to SPACE the element anchors, so the inner layout must keep
	 * its own compact shape (a guide here re-expands every element to fill the whole drag, overlapping
	 * them into one). The SINGLE constructor for inner-layout targets — used by the squad dispatch commit
	 * AND the preview — so the two can never drift and the gesture can never leak back into the inner
	 * layout. Top-level formations stay blind: they only ever see element count + footprint sizes. */
	static FSeinOrderTarget MakeInnerLayoutTarget(
		const FFixedVector& Anchor,
		const FFixedVector& Centroid,
		const FFixedQuaternion& Facing,
		const TSoftClassPtr<USeinFormation>& FormationClass);

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
		meta = (DisplayName = "Compute Formation Facing", SeinDeterministic))
	static FFixedQuaternion ComputeFormationFacing(
		FFixedVector CurrentCentroid,
		FFixedQuaternion CurrentFacing,
		FFixedVector TargetLocation);

	/** Yaw-only facing that points the formation forward axis along `DirectionXY`
	 *  (XY plane; Z ignored). Identity for a near-zero direction. The primitive
	 *  behind ComputeFormationFacing; formations call it directly for facings that
	 *  aren't "toward the target" — e.g. a line facing perpendicular to itself. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Facing From Direction", SeinDeterministic))
	static FFixedQuaternion FacingFromDirection(FFixedVector DirectionXY);

	/** Facing DIRECTION (XY) for a right-click-drag: the guide line's perpendicular on a
	 *  fixed handedness derived from the drag DIRECTION (Start->End rotated a quarter turn).
	 *  The drag is the authority: facing is independent of unit/centroid position, so a
	 *  dragged formation faces by how the line was drawn, not where the units stand. Returns
	 *  the zero vector when the guide isn't a usable 2+ point line (caller keeps its non-drag
	 *  facing). Feed the result to FacingFromDirection; drag the other way to flip the side. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Drag Facing Direction", SeinDeterministic))
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
		meta = (DisplayName = "Project To Navigable", SeinDeterministic))
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
		meta = (DisplayName = "Get Footprint Radius", SeinDeterministic))
	static FFixedPoint GetFootprintRadius(USeinWorldSubsystem* World, FSeinEntityHandle Handle);

	/** Fill OutRadii (index-aligned with Members) with each member's footprint
	 *  radius via GetFootprintRadius. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Gather Footprint Radii", SeinDeterministic))
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
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Spread 1D", SeinDeterministic))
	static void Spread1D(
		const TArray<FFixedPoint>& Radii,
		FFixedPoint MinGap,
		FFixedPoint TargetLength,
		TArray<FFixedPoint>& OutOffsets);

	/** Member indices sorted by footprint radius DESCENDING (largest first), tie-broken by
	 *  index for determinism. Formations place big units first for clean size-graded shapes
	 *  (wedge tip, box front-centre, grid embed). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Sort Indices By Radius Desc", SeinDeterministic))
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
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Pack Footprints", SeinDeterministic))
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
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Separate Positions", SeinDeterministic))
	static void SeparatePositions(
		const TArray<FFixedPoint>& Radii,
		UPARAM(ref) TArray<FFixedVector>& Positions,
		int32 MaxIterations);

	/**
	 * Final placement safety pass: any position that landed off the nav area, or on top of a PARKED
	 * unit's body, is projected to the nearest free cell instead.
	 *
	 * Free = walkable, clear of runtime nav blockers, not within footprint distance of any other slot,
	 * and not on a parked unit (an idle body that is not part of this order — Exclude From Occupancy
	 * lists this order's own members, since they vacate their spots). Every peer slot and parked body
	 * is treated as occupied during relocation so two overflowing slots never resolve onto the same
	 * spot. Positions already on free ground are left EXACTLY where they are (this never reshapes a
	 * clean formation). The shared net the resolver runs AFTER Separate Positions, so a formation
	 * spilling past the play-area edge or ordered onto a settled crowd packs onto the nearest open
	 * ground instead of grinding into bodies. Index-aligned with Radii; a missing radius counts as
	 * zero. Only PARKED units count as occupancy — moving traffic is transient and ignored (this also
	 * keeps the destination preview and the committed order in agreement: parked bodies are stable
	 * between the preview frame and the click). No-op when no nav is bound (tests / nav-less games).
	 * Deterministic — baked grid + start-of-tick collision snapshot.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Project Positions To Navigable", SeinDeterministic,
		        AutoCreateRefTerm = "ExcludeFromOccupancy"))
	static void ProjectPositionsToNavigable(
		USeinWorldSubsystem* World,
		const TArray<FFixedPoint>& Radii,
		UPARAM(ref) TArray<FFixedVector>& Positions,
		const TArray<FSeinEntityHandle>& ExcludeFromOccupancy);

	/**
	 * Fill per-member facings (index-aligned with Positions) per `Mode`: Uniform → all = `UniformFacing`;
	 * RadialOutward / RadialInward → each faces away from / toward `Center` (the formation anchor),
	 * falling back to `UniformFacing` for a member sitting on the centre. Computed by the resolver from
	 * the FINAL positions (after the layout passes), so radial directions are exact. Deterministic.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Compute Member Facings", SeinDeterministic))
	static void ComputeMemberFacings(
		ESeinFormationFacing Mode,
		const TArray<FFixedVector>& Positions,
		FFixedVector Center,
		FFixedQuaternion UniformFacing,
		TArray<FFixedQuaternion>& OutFacings);

protected:
	/** Shared exact-native-or-Blueprint-child admission helper. */
	bool AdmitStatelessNativeAnchor(
		const UClass* ExpectedNativeAnchor,
		FString& OutError) const;
};

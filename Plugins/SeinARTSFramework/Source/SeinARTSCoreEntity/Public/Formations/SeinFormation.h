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
 *          USeinGridFormation (uniform grid) and USeinLineFormation (spread along
 *          the guide). Designers subclass for wedges, columns, spline/path marches,
 *          etc., and select one via the command broker resolver's formation class
 *          / tag map.
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
	 * Project a candidate slot onto the nearest navigable cell via the world's
	 * nav-project resolver. Returns the projected point on success, `Fallback` on
	 * projection failure, and `Position` unchanged when no nav is bound (tests /
	 * nav-less games). Deterministic — the nav grid is baked. Stock formations use
	 * this to keep slots off impassable terrain.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Formation",
		meta = (DisplayName = "Project To Navigable"))
	static FFixedVector ProjectToNavigable(
		USeinWorldSubsystem* World,
		FFixedVector Position,
		FFixedVector Fallback);
};

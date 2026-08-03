/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinStampShape.h
 * @brief:   Shared shape primitive for any subsystem that needs to "stamp"
 *           cells covered by an entity (nav blockers, vision sources, vision
 *           blockers, future combat firing arcs). Shape geometry is
 *           deterministic FFixedPoint math; helpers for cell iteration live
 *           in SeinStampUtils.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinStampShape.generated.h"

UENUM(BlueprintType, meta = (ScriptName = "SeinStampShapeEnum"))
enum class ESeinStampShape : uint8
{
	/** Disc centered at the stamp origin. Use for circular footprints
	 *  (tank chassis, smoke clouds, omnidirectional vision sources). */
	Radial      UMETA(DisplayName = "Radial"),

	/** Oriented bounding box, axes aligned with the stamp's facing direction.
	 *  Use for elongated footprints (long-chassis tanks, walls, rectangular
	 *  buildings). */
	Rect        UMETA(DisplayName = "Rect"),

	/** Wedge projected from the stamp origin along the stamp's facing
	 *  direction. Use for directional sight cones, MG firing arcs, projectors.
	 *  Far-cap geometry is configurable (round = flashlight, flat = pie-slice
	 *  triangle). */
	Conical     UMETA(DisplayName = "Conical")
};

/**
 * One stamp on an entity. Multiple stamps live on a single component (e.g.
 * a building with 4 cone stamps for window sight lines, plus 1 radial stamp
 * for its own footprint). The component owns the array; this struct owns
 * one stamp's worth of geometry.
 *
 * Pose is composed at stamp time:
 *   StampWorldPos      = EntityWorldPos + Quat(EntityYaw) ⋅ LocalOffset
 *   StampWorldFacing   = EntityYaw + YawOffsetDegrees (radians equivalent)
 *
 * Subsystems consume the stamp via SeinStampUtils::ForEachCoveredCell which
 * branches on Shape and visits cells matching the configured geometry.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinStampShape
{
	GENERATED_BODY()

	/** Shape kind. Per-shape parameters below show/hide on this selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Stamp")
	ESeinStampShape Shape = ESeinStampShape::Radial;

	/** Master enable flag. Disabled stamps are skipped before any cell-cover
	 *  iteration runs — no pathfinding contribution, no shadowcast cost, no
	 *  debug viz. Default true. Set false at authoring time for stamps that
	 *  should activate conditionally (e.g. building window-cone sight lines
	 *  that the garrison system flips on when occupied). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Stamp")
	bool bEnabled = true;

	/** Stamp origin offset from the entity's transform, in entity-LOCAL space.
	 *  Rotates with the entity — building windows authored at LocalOffset
	 *  stay attached to the same wall regardless of building yaw. Z is
	 *  ignored (stamps are planar). Use zero for "stamp originates at the
	 *  entity center." */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Stamp")
	FFixedVector LocalOffset = FFixedVector::ZeroVector;

	/** Stamp facing yaw (degrees), added to the entity's yaw. Drives rect
	 *  axes and cone direction; ignored for radial. A building with windows
	 *  on N/S/E/W walls would author 4 stamps with YawOffsets 0 / 90 / 180 /
	 *  270 (or whichever set matches the wall directions in the BP). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Stamp")
	FFixedPoint YawOffsetDegrees = FFixedPoint::Zero;

	// =========================================================================
	// Radial parameters
	// =========================================================================

	/** Disc radius in world units. Radial only — ignored when Shape != Radial.
	 *
	 *  Note: previously hidden via EditCondition when Shape didn't match, but
	 *  UE's EditCondition resolver can't reach sibling property names through
	 *  FInstancedStruct's type-erasure wrapper — every panel rebuild
	 *  (minimize, delete, undo) spammed LogEditCondition errors. Always-
	 *  visible avoids the noise; runtime ignores the field when Shape
	 *  disagrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Stamp",
		meta = (ClampMin = "0.0", DisplayName = "Radius"))
	FFixedPoint Radius = FFixedPoint::FromInt(100);

	// =========================================================================
	// Rect parameters
	// =========================================================================

	/** Half extent along the stamp's facing axis (entity forward + YawOffset).
	 *  Rect only — ignored when Shape != Rect. Total length = 2 × HalfExtentX.
	 *  Always visible — see Radius docstring for why EditCondition was dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Stamp",
		meta = (ClampMin = "0.0", DisplayName = "Half Extent (Forward)"))
	FFixedPoint HalfExtentX = FFixedPoint::FromInt(150);

	/** Half extent along the stamp's right axis (perpendicular to facing).
	 *  Rect only — ignored when Shape != Rect. Total width = 2 × HalfExtentY.
	 *  Always visible — see Radius docstring for why EditCondition was dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Stamp",
		meta = (ClampMin = "0.0", DisplayName = "Half Extent (Right)"))
	FFixedPoint HalfExtentY = FFixedPoint::FromInt(100);

	// =========================================================================
	// Conical parameters
	// =========================================================================

	/** Total apex angle in degrees. A 60° cone opens 30° on each side of the
	 *  facing direction. Conical only — ignored when Shape != Conical.
	 *  Bounded to (0, 180]: zero degenerates to a ray, 180 covers a
	 *  half-plane. Always visible — see Radius docstring for why
	 *  EditCondition was dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Stamp",
		meta = (ClampMin = "1.0", ClampMax = "180.0",
		        DisplayName = "Cone Angle (Total Degrees)"))
	FFixedPoint ConeAngleDegrees = FFixedPoint::FromInt(60);

	/** Projection length from the cone apex, in world units. Conical only —
	 *  ignored when Shape != Conical. Always visible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Stamp",
		meta = (ClampMin = "0.0", DisplayName = "Cone Length"))
	FFixedPoint ConeLength = FFixedPoint::FromInt(500);

	/** Far-cap geometry of the cone. Conical only — ignored when Shape !=
	 *  Conical. Always visible.
	 *    true  → round (circular arc, "flashlight" / "spotlight" projection).
	 *            Cells inside iff `distance_from_apex ≤ ConeLength`.
	 *    false → flat (straight cut perpendicular to facing, "pie-slice"
	 *            triangle). Cells inside iff `forward_projection ≤ ConeLength`.
	 *  Sides of the wedge (the angle constraint) are identical in both cases. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Stamp",
		meta = (DisplayName = "Round Far Edge"))
	bool bConeRoundEdge = true;

	FORCEINLINE bool operator==(const FSeinStampShape& Other) const
	{
		return Shape == Other.Shape
			&& bEnabled == Other.bEnabled
			&& LocalOffset == Other.LocalOffset
			&& YawOffsetDegrees == Other.YawOffsetDegrees
			&& Radius == Other.Radius
			&& HalfExtentX == Other.HalfExtentX
			&& HalfExtentY == Other.HalfExtentY
			&& ConeAngleDegrees == Other.ConeAngleDegrees
			&& ConeLength == Other.ConeLength
			&& bConeRoundEdge == Other.bConeRoundEdge;
	}

	FORCEINLINE bool operator!=(const FSeinStampShape& Other) const
	{
		return !(*this == Other);
	}
};

FORCEINLINE uint32 GetTypeHash(const FSeinStampShape& Stamp)
{
	uint32 Hash = GetTypeHash(static_cast<uint8>(Stamp.Shape));
	Hash = HashCombine(Hash, GetTypeHash(Stamp.bEnabled));
	Hash = HashCombine(Hash, GetTypeHash(Stamp.LocalOffset));
	Hash = HashCombine(Hash, GetTypeHash(Stamp.YawOffsetDegrees));
	Hash = HashCombine(Hash, GetTypeHash(Stamp.Radius));
	Hash = HashCombine(Hash, GetTypeHash(Stamp.HalfExtentX));
	Hash = HashCombine(Hash, GetTypeHash(Stamp.HalfExtentY));
	Hash = HashCombine(Hash, GetTypeHash(Stamp.ConeAngleDegrees));
	Hash = HashCombine(Hash, GetTypeHash(Stamp.ConeLength));
	Hash = HashCombine(Hash, GetTypeHash(Stamp.bConeRoundEdge));
	return Hash;
}

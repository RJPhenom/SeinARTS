/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinStampUtils.h
 * @brief:   Cell-cover iteration for FSeinStampShape. Branches on the shape
 *           kind, computes the appropriate world-space bounds, and visits
 *           each grid cell whose center lies inside the shape. Determinism
 *           is preserved via FFixedPoint math + integer cell-index arithmetic.
 */

#pragma once

#include "CoreMinimal.h"
#include "Stamping/SeinStampShape.h"
#include "Math/MathLib.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/Quat.h"

namespace SeinStampUtils
{
	/** FFixedPoint → int32 floor. Uses the deterministic SeinMath::Floor + a
	 *  ToFloat conversion bounded by the grid scale (cell counts are well
	 *  inside float's exact-integer range, so the round-trip is exact). */
	FORCEINLINE int32 FloorToInt(FFixedPoint X)
	{
		return FMath::FloorToInt(SeinMath::Floor(X).ToFloat());
	}

	/** π / 180 as an FFixedPoint ratio — converts degrees → radians without
	 *  a runtime float math dependency. Compile-time inlined. */
	FORCEINLINE FFixedPoint DegToRad(FFixedPoint Deg)
	{
		return Deg * (FFixedPoint::Pi / FFixedPoint::FromInt(180));
	}

	/** Extract entity yaw (radians) from a planar quaternion. Same logic the
	 *  movements use — picks forward direction and atan2's it, so we don't
	 *  pay the gimbal-singularity branches in Eulers(). */
	FORCEINLINE FFixedPoint YawFromRotation(const FFixedQuaternion& Rotation)
	{
		const FFixedVector Forward = Rotation.RotateVector(FFixedVector::ForwardVector);
		return SeinMath::Atan2(Forward.Y, Forward.X);
	}

	/** World-space stamp origin = entity position + Quat(EntityYaw)·LocalOffset.
	 *  For radial stamps with zero LocalOffset this is just the entity itself.
	 *  For shapes with an offset (window cones, asymmetric building stamps)
	 *  this gives the apex / center of the shape in world space. Z passes
	 *  through unchanged — stamp planes are 2D, vertical offsets aren't
	 *  configurable in the current model. Used by FoW to source LOS rays
	 *  from a window's apex rather than the building center. */
	FORCEINLINE FFixedVector ComputeStampWorldOrigin(
		const FSeinStampShape& Stamp,
		const FFixedVector& EntityWorldPos,
		const FFixedQuaternion& EntityRotation)
	{
		const FFixedPoint EntityYaw = YawFromRotation(EntityRotation);
		const FFixedPoint Cos = SeinMath::Cos(EntityYaw);
		const FFixedPoint Sin = SeinMath::Sin(EntityYaw);
		return FFixedVector(
			EntityWorldPos.X + Stamp.LocalOffset.X * Cos - Stamp.LocalOffset.Y * Sin,
			EntityWorldPos.Y + Stamp.LocalOffset.X * Sin + Stamp.LocalOffset.Y * Cos,
			EntityWorldPos.Z);
	}

	/**
	 * Visit every cell whose center lies inside the stamp's shape.
	 *
	 * Pose is composed from the entity transform and the stamp's local
	 * offset/yaw — the stamp's world position is `EntityWorldPos +
	 * Quat(EntityYaw)·LocalOffset` and its world facing is
	 * `EntityYaw + DegToRad(YawOffsetDegrees)`.
	 *
	 * `Visit` is invoked as `Visit(int32 X, int32 Y)` for each in-bounds
	 * cell that passes the per-shape membership test. Cell indices are
	 * already clamped to [0, GridWidth-1] / [0, GridHeight-1].
	 *
	 * Disabled stamps and degenerate shapes (zero radius / zero extent /
	 * non-positive cell size) early-return without invoking `Visit`.
	 *
	 * Templated so the lambda inlines through the dispatch — at one stamp
	 * per nav blocker × multiple per FindPath, the function call overhead
	 * adds up otherwise.
	 */
	template<typename FuncT>
	void ForEachCoveredCell(
		const FSeinStampShape& Stamp,
		const FFixedVector& EntityWorldPos,
		const FFixedQuaternion& EntityRotation,
		FFixedPoint CellSize,
		const FFixedVector& GridOrigin,
		int32 GridWidth,
		int32 GridHeight,
		FuncT&& Visit)
	{
		if (!Stamp.bEnabled) return;
		if (CellSize <= FFixedPoint::Zero) return;
		if (GridWidth <= 0 || GridHeight <= 0) return;

		// =====================================================================
		// Pose composition. Entity yaw rotates LocalOffset into world space,
		// then YawOffsetDegrees layers on top for the stamp's own facing.
		// =====================================================================
		const FFixedPoint EntityYaw = YawFromRotation(EntityRotation);
		const FFixedPoint EntityCos = SeinMath::Cos(EntityYaw);
		const FFixedPoint EntitySin = SeinMath::Sin(EntityYaw);

		const FFixedPoint OffsetX = Stamp.LocalOffset.X * EntityCos - Stamp.LocalOffset.Y * EntitySin;
		const FFixedPoint OffsetY = Stamp.LocalOffset.X * EntitySin + Stamp.LocalOffset.Y * EntityCos;

		const FFixedPoint StampX = EntityWorldPos.X + OffsetX;
		const FFixedPoint StampY = EntityWorldPos.Y + OffsetY;

		const FFixedPoint StampYaw = EntityYaw + DegToRad(Stamp.YawOffsetDegrees);
		const FFixedPoint StampCos = SeinMath::Cos(StampYaw);
		const FFixedPoint StampSin = SeinMath::Sin(StampYaw);

		// =====================================================================
		// Cell-iteration helper. Takes world-space AABB, clamps to grid, and
		// runs an inside-test predicate per cell.
		// =====================================================================
		const FFixedPoint Half = CellSize / FFixedPoint::FromInt(2);
		const FFixedPoint OriginX = GridOrigin.X;
		const FFixedPoint OriginY = GridOrigin.Y;

		auto VisitInBounds = [&](FFixedPoint MinWX, FFixedPoint MaxWX, FFixedPoint MinWY, FFixedPoint MaxWY, auto&& Inside)
		{
			int32 CMinX = FloorToInt((MinWX - OriginX) / CellSize);
			int32 CMaxX = FloorToInt((MaxWX - OriginX) / CellSize);
			int32 CMinY = FloorToInt((MinWY - OriginY) / CellSize);
			int32 CMaxY = FloorToInt((MaxWY - OriginY) / CellSize);
			if (CMinX < 0) CMinX = 0;
			if (CMinY < 0) CMinY = 0;
			if (CMaxX > GridWidth - 1)  CMaxX = GridWidth - 1;
			if (CMaxY > GridHeight - 1) CMaxY = GridHeight - 1;

			for (int32 Y = CMinY; Y <= CMaxY; ++Y)
			{
				const FFixedPoint CellCY = OriginY + CellSize * FFixedPoint::FromInt(Y) + Half;
				for (int32 X = CMinX; X <= CMaxX; ++X)
				{
					const FFixedPoint CellCX = OriginX + CellSize * FFixedPoint::FromInt(X) + Half;
					if (Inside(CellCX, CellCY))
					{
						Visit(X, Y);
					}
				}
			}
		};

		// =====================================================================
		// Per-shape dispatch.
		// =====================================================================
		switch (Stamp.Shape)
		{
		case ESeinStampShape::Radial:
		{
			const FFixedPoint R = Stamp.Radius;
			if (R <= FFixedPoint::Zero) return;

			// Add half-cell to the radius so the stamp catches cells whose
			// CENTERS sit just past the disc edge but whose squares overlap
			// the disc. Without this, near-cell-width radii leave diagonal
			// slivers unstamped.
			const FFixedPoint RPad = R + Half;
			const FFixedPoint RPadSq = RPad * RPad;

			VisitInBounds(StampX - R, StampX + R, StampY - R, StampY + R,
				[&](FFixedPoint CX, FFixedPoint CY)
				{
					const FFixedPoint DX = CX - StampX;
					const FFixedPoint DY = CY - StampY;
					return (DX * DX + DY * DY) <= RPadSq;
				});
			break;
		}

		case ESeinStampShape::Rect:
		{
			const FFixedPoint HX = Stamp.HalfExtentX;
			const FFixedPoint HY = Stamp.HalfExtentY;
			if (HX <= FFixedPoint::Zero || HY <= FFixedPoint::Zero) return;

			// World AABB of the OBB. AABB extents = |HX·cos| + |HY·sin| etc.
			// Cheaper than transforming corners since we only need bounds.
			const FFixedPoint AbsCos = (StampCos < FFixedPoint::Zero) ? -StampCos : StampCos;
			const FFixedPoint AbsSin = (StampSin < FFixedPoint::Zero) ? -StampSin : StampSin;
			const FFixedPoint AABBHalfX = HX * AbsCos + HY * AbsSin;
			const FFixedPoint AABBHalfY = HX * AbsSin + HY * AbsCos;

			// Half-cell padding so cells whose centers project just past the
			// box edge but whose squares overlap still stamp.
			const FFixedPoint HXPad = HX + Half;
			const FFixedPoint HYPad = HY + Half;

			VisitInBounds(StampX - AABBHalfX, StampX + AABBHalfX,
			              StampY - AABBHalfY, StampY + AABBHalfY,
				[&](FFixedPoint CX, FFixedPoint CY)
				{
					// Project (cell - stampOrigin) onto stamp axes:
					//   localX along  forward = (cos, sin)
					//   localY along  right   = (-sin, cos)
					const FFixedPoint DX = CX - StampX;
					const FFixedPoint DY = CY - StampY;
					const FFixedPoint LX = DX * StampCos + DY * StampSin;
					const FFixedPoint LY = -DX * StampSin + DY * StampCos;
					const FFixedPoint AbsLX = (LX < FFixedPoint::Zero) ? -LX : LX;
					const FFixedPoint AbsLY = (LY < FFixedPoint::Zero) ? -LY : LY;
					return AbsLX <= HXPad && AbsLY <= HYPad;
				});
			break;
		}

		case ESeinStampShape::Conical:
		{
			const FFixedPoint Length = Stamp.ConeLength;
			if (Length <= FFixedPoint::Zero) return;

			// Half-angle = TotalAngle / 2 (designer authors total apex angle).
			// Clamp upper at just under π to keep tan finite at 90° (we use
			// the half-angle in tan, so total 180° → half 90° → tan undefined).
			FFixedPoint HalfAngle = DegToRad(Stamp.ConeAngleDegrees) / FFixedPoint::FromInt(2);
			const FFixedPoint MaxHalf = FFixedPoint::Pi / FFixedPoint::FromInt(2)
			    - FFixedPoint::FromInt(1) / FFixedPoint::FromInt(1000);
			if (HalfAngle > MaxHalf) HalfAngle = MaxHalf;
			if (HalfAngle <= FFixedPoint::Zero) return;

			const FFixedPoint TanHalfAngle = SeinMath::Tan(HalfAngle);

			// AABB. The disc of radius Length is the loosest tight-bound that
			// works for both the round and flat far-cap variants — wider
			// AABBs cost more cells iterated, but the inside-test rejects.
			const FFixedPoint LengthPad = Length + Half;
			const FFixedPoint LengthSq = LengthPad * LengthPad;

			const bool bRoundEdge = Stamp.bConeRoundEdge;

			VisitInBounds(StampX - Length, StampX + Length,
			              StampY - Length, StampY + Length,
				[&](FFixedPoint CX, FFixedPoint CY)
				{
					// Local-axis projection of (cell - apex).
					const FFixedPoint DX = CX - StampX;
					const FFixedPoint DY = CY - StampY;
					const FFixedPoint LX =  DX * StampCos + DY * StampSin; // forward
					const FFixedPoint LY = -DX * StampSin + DY * StampCos; // right
					if (LX < -Half) return false; // behind the apex (with half-cell pad)

					// Wedge test: |LY| ≤ LX · tan(halfAngle). Equivalent to
					// |angle from forward| ≤ halfAngle, but no atan2 needed.
					// Pad both sides by Half so cells whose centers just clip
					// the wedge edge still stamp.
					const FFixedPoint AbsLY = (LY < FFixedPoint::Zero) ? -LY : LY;
					const FFixedPoint LXPlusPad = LX + Half;
					if (AbsLY > LXPlusPad * TanHalfAngle + Half) return false;

					// Far-cap test.
					if (bRoundEdge)
					{
						return (LX * LX + LY * LY) <= LengthSq;
					}
					return LX <= Length + Half;
				});
			break;
		}
		}
	}
}

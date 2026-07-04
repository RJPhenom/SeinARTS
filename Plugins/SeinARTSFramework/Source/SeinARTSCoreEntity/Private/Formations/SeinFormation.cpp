/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFormation.cpp
 * @brief   USeinFormation base: shared facing / nav-projection helpers and the
 *          default blob layout.
 */

#include "Formations/SeinFormation.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Collision/SeinCollisionSpatialHash.h"
#include "Components/SeinExtentsComponent.h"
#include "Components/SeinExtentsHelpers.h"
#include "Components/SeinCommandBrokerData.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Math/MathLib.h"
#include "Types/FixedPoint.h"

FFixedQuaternion USeinFormation::FacingFromDirection(FFixedVector DirectionXY)
{
	FFixedVector Flat(DirectionXY.X, DirectionXY.Y, FFixedPoint::Zero);
	if (Flat.IsNearlyZero()) return FFixedQuaternion::Identity;
	const FFixedPoint Yaw = SeinMath::Atan2(Flat.Y, Flat.X);
	return FFixedQuaternion::FromAxisAndAngle(FFixedVector::UpVector, Yaw);
}

void USeinFormation::ComputeMemberFacings(
	ESeinFormationFacing Mode,
	const TArray<FFixedVector>& Positions,
	FFixedVector Center,
	FFixedQuaternion UniformFacing,
	TArray<FFixedQuaternion>& OutFacings)
{
	const int32 N = Positions.Num();
	OutFacings.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		if (Mode == ESeinFormationFacing::Uniform)
		{
			OutFacings[i] = UniformFacing;
			continue;
		}
		// Radial: face away from (Outward) or toward (Inward) the formation centre, on the XY plane.
		const FFixedVector Dir = (Mode == ESeinFormationFacing::RadialOutward)
			? FFixedVector(Positions[i].X - Center.X, Positions[i].Y - Center.Y, FFixedPoint::Zero)
			: FFixedVector(Center.X - Positions[i].X, Center.Y - Positions[i].Y, FFixedPoint::Zero);
		OutFacings[i] = Dir.IsNearlyZero() ? UniformFacing : FacingFromDirection(Dir);
	}
}

FFixedVector USeinFormation::DragFacingDir(const TArray<FFixedVector>& GuidePoints)
{
	if (GuidePoints.Num() < 2) return FFixedVector::ZeroVector;
	FFixedVector Line = GuidePoints.Last() - GuidePoints[0];
	Line.Z = FFixedPoint::Zero;
	if (Line.IsNearlyZero()) return FFixedVector::ZeroVector;
	const FFixedVector Dir = FFixedVector::GetSafeNormal(Line);
	// Drag DIRECTION is the authority: the perpendicular on a fixed handedness
	// (Start->End rotated a quarter turn). No centroid: a drag's facing must not depend
	// on where the units stand. Drag the line the other way to flip the side.
	return FFixedVector(FFixedPoint::Zero - Dir.Y, Dir.X, FFixedPoint::Zero);
}

FFixedQuaternion USeinFormation::ComputeFormationFacing(
	FFixedVector CurrentCentroid,
	FFixedQuaternion CurrentFacing,
	FFixedVector TargetLocation)
{
	FFixedVector ToTarget = TargetLocation - CurrentCentroid;
	ToTarget.Z = FFixedPoint::Zero; // 2D — RTS top-down, ignore vertical

	// Move-to-where-we-are: keep current facing rather than degenerate-quat'ing.
	if (ToTarget.IsNearlyZero()) return CurrentFacing;

	// Facing ALWAYS rotates to face the move direction — the formation pivots to
	// align with where it's going, every move, including a straight 180° backpedal.
	return FacingFromDirection(FFixedVector::GetSafeNormal(ToTarget));
}

FFixedVector USeinFormation::ProjectToNavigable(
	USeinWorldSubsystem* World,
	FFixedVector Position,
	FFixedVector Fallback)
{
	// Ground-follow FIRST: on passable terrain, keep the slot's X/Y EXACTLY and snap only
	// its Z to the (bilinear-interpolated) terrain height. A formation on a hill keeps its
	// top-down shape — each slot just rides the ground. Without this, every slot carries the
	// flat anchor Z, and the elevation-aware nav projection below shoves any slot whose
	// terrain sits >NavProjectionElevationTolerance off that flat Z sideways to a same-
	// elevation cell — scattering the formation across slopes.
	//
	// HeightResolver is walkable-only, so a slot on an IMPASSABLE cell (an in-map obstacle, or off the
	// play area entirely) returns false here and falls through to the nav-projection below.
	if (World && World->HeightResolver.IsBound())
	{
		FFixedPoint GroundZ;
		if (World->HeightResolver.Execute(Position, GroundZ))
		{
			return FFixedVector(Position.X, Position.Y, GroundZ);
		}
	}

	if (!World) return Fallback;

	// Off the play area (or on an obstacle): snap the slot to the NEAREST WALKABLE cell on the nav grid.
	// Per-slot and occupancy-blind — several slots can snap to the same edge cell here. The resolver's
	// batch ProjectPositionsToNavigable (occupancy-aware) + SeparatePositions passes spread them onto
	// distinct free cells afterward so they don't pile up.
	if (World->NavProjectResolver.IsBound())
	{
		FFixedVector Projected;
		if (World->NavProjectResolver.Execute(Position, Projected)) { return Projected; }
	}
	return Position; // no nav bound / unprojectable → leave as-is
}

FFixedPoint USeinFormation::GetFootprintRadius(USeinWorldSubsystem* World, FSeinEntityHandle Handle)
{
	// Fallback for entities with no authored extents — roughly an infantry body, so
	// footprint-aware spacing degrades to the old uniform feel rather than zero.
	const FFixedPoint DefaultRadius = FFixedPoint::FromInt(40);
	if (!World) return DefaultRadius;

	// Composite-broker element (a squad): its footprint is the whole formation's bounding circle,
	// maintained by the owning system (e.g. the squad system). When present, a parent formation places
	// the entire squad as ONE element of this size; its members lay out internally around the anchor.
	if (const FSeinCommandBrokerData* Broker = World->GetComponent<FSeinCommandBrokerData>(Handle))
	{
		if (Broker->FormationRadius > FFixedPoint::Zero) { return Broker->FormationRadius; }
	}

	const FSeinExtentsComponent* Extents = World->GetComponent<FSeinExtentsComponent>(Handle);
	if (!Extents || Extents->Shapes.Num() == 0) return DefaultRadius;

	FFixedPoint Best = FFixedPoint::Zero;
	for (const FSeinExtentsShape& Shape : Extents->Shapes)
	{
		// Per-shape circumscribed radius (orientation-independent so a formation that
		// rotates to face the drag never overlaps): Capsule → Radius, Box → √(hx²+hy²).
		FFixedPoint R = (Shape.Shape == ESeinExtentsShape::Capsule)
			? Shape.Radius
			: SeinMath::Sqrt(Shape.HalfExtentX * Shape.HalfExtentX + Shape.HalfExtentY * Shape.HalfExtentY);
		// Push an offset shape (turret forward of hull center, building wing) out by
		// its planar offset so the entity's overall reach is covered.
		const FFixedVector Off(Shape.LocalOffset.X, Shape.LocalOffset.Y, FFixedPoint::Zero);
		R = R + Off.Size();
		if (R > Best) Best = R;
	}
	return (Best > FFixedPoint::Zero) ? Best : DefaultRadius;
}

void USeinFormation::GatherFootprintRadii(
	USeinWorldSubsystem* World,
	const TArray<FSeinEntityHandle>& Members,
	TArray<FFixedPoint>& OutRadii)
{
	OutRadii.Reset();
	OutRadii.Reserve(Members.Num());
	for (const FSeinEntityHandle& M : Members)
	{
		OutRadii.Add(GetFootprintRadius(World, M));
	}
}

void USeinFormation::Spread1D(
	const TArray<FFixedPoint>& Radii,
	FFixedPoint MinGap,
	FFixedPoint TargetLength,
	TArray<FFixedPoint>& OutOffsets)
{
	const int32 N = Radii.Num();
	OutOffsets.Reset();
	OutOffsets.SetNum(N);
	if (N == 0) return;
	if (N == 1) { OutOffsets[0] = FFixedPoint::Zero; return; }

	// Tight no-overlap gaps between adjacent centers.
	TArray<FFixedPoint> Gaps;
	Gaps.SetNum(N - 1);
	FFixedPoint Span = FFixedPoint::Zero;
	for (int32 i = 1; i < N; ++i)
	{
		FFixedPoint Gap = Radii[i - 1] + Radii[i];
		if (Gap < MinGap) { Gap = MinGap; }
		Gaps[i - 1] = Gap;
		Span = Span + Gap;
	}

	// A longer requested span (e.g. a long drag) shares its slack evenly across gaps,
	// so the line fills the drag without ever overlapping. Shorter → keep tight span.
	if (TargetLength > Span)
	{
		const FFixedPoint Slack = (TargetLength - Span) / FFixedPoint::FromInt(N - 1);
		for (int32 i = 0; i < Gaps.Num(); ++i) { Gaps[i] = Gaps[i] + Slack; }
		Span = TargetLength;
	}

	OutOffsets[0] = FFixedPoint::Zero;
	for (int32 i = 1; i < N; ++i) { OutOffsets[i] = OutOffsets[i - 1] + Gaps[i - 1]; }

	// Center the run on 0.
	const FFixedPoint Half = Span / FFixedPoint::Two;
	for (int32 i = 0; i < N; ++i) { OutOffsets[i] = OutOffsets[i] - Half; }
}

TArray<int32> USeinFormation::SortIndicesByRadiusDesc(const TArray<FFixedPoint>& Radii)
{
	TArray<int32> Order;
	Order.Reserve(Radii.Num());
	for (int32 i = 0; i < Radii.Num(); ++i) { Order.Add(i); }
	Order.Sort([&Radii](int32 A, int32 B)
	{
		if (Radii[A] != Radii[B]) { return Radii[A] > Radii[B]; } // largest first
		return A < B;                                              // deterministic tie-break
	});
	return Order;
}

void USeinFormation::PackFootprints(
	const TArray<FFixedPoint>& Radii,
	FFixedPoint DesiredFrontWidth,
	FFixedPoint MinCell,
	FSeinFootprintPacking& Out)
{
	Out = FSeinFootprintPacking();
	const int32 N = Radii.Num();
	if (N == 0) return;

	// STEP 1 — cell = smallest footprint diameter (floored at MinCell so a formation can ask for
	// extra breathing room); each unit → a span×span box, span = ceil(footprint / cell).
	FFixedPoint Cell = FFixedPoint::Zero;
	for (int32 i = 0; i < N; ++i)
	{
		const FFixedPoint D = Radii[i] * FFixedPoint::Two;
		if (i == 0 || D < Cell) { Cell = D; }
	}
	if (Cell < MinCell) { Cell = MinCell; }
	if (Cell <= FFixedPoint::Zero) { Cell = FFixedPoint::FromInt(50); }
	Out.Cell = Cell;

	Out.Span.SetNum(N);
	int32 TotalCells = 0, MaxSpan = 1;
	for (int32 i = 0; i < N; ++i)
	{
		const FFixedPoint Diameter = Radii[i] * FFixedPoint::Two;
		int32 s = 1;
		while (Cell * FFixedPoint::FromInt(s) < Diameter) { ++s; }
		Out.Span[i] = s;
		TotalCells += s * s;
		if (s > MaxSpan) { MaxSpan = s; }
	}

	// STEP 2 — column count. A drag passes its front WIDTH (world units) → as many cells as span it;
	// a click passes <= 0 → a square-ish grid (Y = ceil(sqrt(total cells))). Never narrower than the
	// biggest box; nudged so (Y - MaxSpan) is EVEN so an even-span big box centres. Rows (X) over-
	// allocate (centring uses the occupied bounds). Column count derived by accumulation to avoid a
	// fixed→int conversion.
	int32 Y;
	if (DesiredFrontWidth > FFixedPoint::Zero)
	{
		Y = 1;
		FFixedPoint Accum = Cell;
		while (Accum < DesiredFrontWidth && Y < TotalCells) { Accum = Accum + Cell; ++Y; }
	}
	else
	{
		Y = 1;
		while (Y * Y < TotalCells) { ++Y; }
	}
	if (Y < MaxSpan) { Y = MaxSpan; }
	if (MaxSpan > 1 && (((Y - MaxSpan) & 1) != 0)) { ++Y; }
	const int32 X = (TotalCells + Y - 1) / Y + MaxSpan;
	Out.Columns = Y;
	Out.Rows = X;

	TArray<bool> Occ; Occ.Init(false, X * Y);
	Out.BlockRow.Init(0, N);
	Out.BlockCol.Init(0, N);

	auto BlockFree = [&](int32 r, int32 c, int32 s) -> bool
	{
		for (int32 rr = r; rr < r + s; ++rr)
			for (int32 cc = c; cc < c + s; ++cc) { if (Occ[rr * Y + cc]) { return false; } }
		return true;
	};
	auto MarkBlock = [&](int32 r, int32 c, int32 s)
	{
		for (int32 rr = r; rr < r + s; ++rr)
			for (int32 cc = c; cc < c + s; ++cc) { Occ[rr * Y + cc] = true; }
	};

	// STEP 3 — largest first.
	const TArray<int32> Order = SortIndicesByRadiusDesc(Radii);

	// STEP 4 — big boxes front-and-centre: most-forward (top) row that fits, most-central column there.
	for (const int32 Idx : Order)
	{
		const int32 s = Out.Span[Idx];
		if (s <= 1) { continue; }
		int32 PlaceR = 0, PlaceC = 0;
		for (int32 r = 0; r + s <= X; ++r)
		{
			int32 RowBestC = -1, RowBestDist = MAX_int32;
			for (int32 c = 0; c + s <= Y; ++c)
			{
				if (!BlockFree(r, c, s)) { continue; }
				const int32 dist = FMath::Abs(2 * c + s - Y); // 2×(box-centre col − grid-centre col)
				if (dist < RowBestDist) { RowBestDist = dist; RowBestC = c; }
			}
			if (RowBestC >= 0) { PlaceR = r; PlaceC = RowBestC; break; }
		}
		MarkBlock(PlaceR, PlaceC, s);
		Out.BlockRow[Idx] = PlaceR;
		Out.BlockCol[Idx] = PlaceC;
	}

	// STEP 5 — 1×1 boxes fill every remaining cell, row-major.
	for (const int32 Idx : Order)
	{
		if (Out.Span[Idx] != 1) { continue; }
		int32 PlaceR = 0, PlaceC = 0;
		bool bPlaced = false;
		for (int32 r = 0; r < X && !bPlaced; ++r)
			for (int32 c = 0; c < Y && !bPlaced; ++c)
				if (!Occ[r * Y + c]) { PlaceR = r; PlaceC = c; bPlaced = true; }
		Occ[PlaceR * Y + PlaceC] = true;
		Out.BlockRow[Idx] = PlaceR;
		Out.BlockCol[Idx] = PlaceC;
	}
}

void USeinFormation::SeparatePositions(
	const TArray<FFixedPoint>& Radii,
	TArray<FFixedVector>& Positions,
	int32 MaxIterations)
{
	const int32 N = Positions.Num();
	if (N < 2) return;
	const FFixedPoint Eps = FFixedPoint::One / FFixedPoint::FromInt(100);
	for (int32 Iter = 0; Iter < MaxIterations; ++Iter)
	{
		bool bMoved = false;
		for (int32 i = 0; i < N; ++i)
		{
			for (int32 j = i + 1; j < N; ++j)
			{
				FFixedVector D = Positions[j] - Positions[i]; D.Z = FFixedPoint::Zero;
				const FFixedPoint DistSq = D.X * D.X + D.Y * D.Y;
				const FFixedPoint Ri = Radii.IsValidIndex(i) ? Radii[i] : FFixedPoint::Zero;
				const FFixedPoint Rj = Radii.IsValidIndex(j) ? Radii[j] : FFixedPoint::Zero;
				// Rest with breathing room: space slots a small margin BEYOND footprint contact
				// (Ri+Rj) so units settled onto them sit just OUTSIDE the collision floor's
				// separation threshold — the floor then never fires at rest and a formation holds
				// its shape instead of being shoved apart. In the shared resolver path, so the
				// preview shows the same spacing the commit lands on (root CLAUDE.md #6).
				const FFixedPoint RestMargin = FFixedPoint::FromInt(25);
				const FFixedPoint MinDist = Ri + Rj + RestMargin;
				if (DistSq >= MinDist * MinDist) { continue; } // touching or clear — leave it
				FFixedPoint Dist = SeinMath::Sqrt(DistSq);
				FFixedVector Dir;
				if (Dist > Eps)
				{
					Dir = FFixedVector(D.X / Dist, D.Y / Dist, FFixedPoint::Zero);
				}
				else
				{
					// Coincident → deterministic arbitrary direction derived from the index pair.
					const FFixedPoint Ang = FFixedPoint::TwoPi * FFixedPoint::FromInt((i * 7 + j) % 16) / FFixedPoint::FromInt(16);
					Dir = FFixedVector(SeinMath::Cos(Ang), SeinMath::Sin(Ang), FFixedPoint::Zero);
					Dist = FFixedPoint::Zero;
				}
				const FFixedPoint Push = (MinDist - Dist) / FFixedPoint::Two; // each moves half the overlap
				Positions[i].X = Positions[i].X - Dir.X * Push;
				Positions[i].Y = Positions[i].Y - Dir.Y * Push;
				Positions[j].X = Positions[j].X + Dir.X * Push;
				Positions[j].Y = Positions[j].Y + Dir.Y * Push;
				bMoved = true;
			}
		}
		if (!bMoved) break;
	}
}

void USeinFormation::ProjectPositionsToNavigable(
	USeinWorldSubsystem* World,
	const TArray<FFixedPoint>& Radii,
	TArray<FFixedVector>& Positions,
	const TArray<FSeinEntityHandle>& ExcludeFromOccupancy)
{
	const int32 N = Positions.Num();
	if (!World || N == 0) return;
	// No nav projection bound (tests / nav-less games) → nothing to clamp to; leave positions as-is.
	if (!World->NavProjectFreeResolver.IsBound()) return;

	// PARKED-UNIT OCCUPANCY. Gather idle bodies near the formation footprint so no slot is placed
	// ON one — the "order into a settled crowd" case, where occupancy-blind slots made arrivers
	// grind against parked bodies until the stall failsafe fired each one. Rules: only PARKED units
	// count (idle, movement-capable, not in ExcludeFromOccupancy — this order's own members vacate
	// their spots; moving traffic is transient and ignored, which also keeps preview↔commit parity:
	// parked bodies are stable between the preview frame and the click's processing tick). Radius
	// from the same collider cascade the broadphase stamps (Extents bounding radius → nav fallback
	// footprint). Query = the collision hash's start-of-tick snapshot, handle-sorted → deterministic.
	TArray<FFixedVector> ParkedCentres;
	TArray<FFixedPoint>  ParkedRadii;
	{
		// Formation bounds → one hash query covering every slot plus a body-sized margin.
		FFixedVector Min = Positions[0];
		FFixedVector Max = Positions[0];
		for (int32 i = 1; i < N; ++i)
		{
			if (Positions[i].X < Min.X) Min.X = Positions[i].X;
			if (Positions[i].Y < Min.Y) Min.Y = Positions[i].Y;
			if (Positions[i].X > Max.X) Max.X = Positions[i].X;
			if (Positions[i].Y > Max.Y) Max.Y = Positions[i].Y;
		}
		const FFixedVector Centre(
			(Min.X + Max.X) / FFixedPoint::Two, (Min.Y + Max.Y) / FFixedPoint::Two, Positions[0].Z);
		FFixedVector HalfSpan(Max.X - Centre.X, Max.Y - Centre.Y, FFixedPoint::Zero);
		const FFixedPoint QueryRadius = HalfSpan.Size() + FFixedPoint::FromInt(300); // body-sized margin

		TSet<FSeinEntityHandle> Excluded;
		Excluded.Reserve(ExcludeFromOccupancy.Num());
		for (const FSeinEntityHandle& H : ExcludeFromOccupancy) { Excluded.Add(H); }

		TArray<FSeinEntityHandle> Nearby;
		World->GetCollisionSpatialHash().QueryRadius(Centre, QueryRadius, Nearby, FSeinEntityHandle());
		for (const FSeinEntityHandle& H : Nearby)
		{
			if (Excluded.Contains(H)) continue;
			const FSeinMovementComponent* Move = World->GetComponent<FSeinMovementComponent>(H);
			if (!Move || Move->bHasTarget) continue; // not a unit, or in motion → not occupancy
			const FSeinEntity* Entity = World->GetEntityPool().Get(H);
			if (!Entity) continue;
			FFixedPoint BodyRadius = FFixedPoint::Zero;
			if (const FSeinExtentsComponent* Ext = World->GetComponent<FSeinExtentsComponent>(H))
			{
				BodyRadius = SeinExtentsHelpers::GetColliderBoundingRadius(*Ext);
			}
			if (BodyRadius <= FFixedPoint::Zero)
			{
				if (const FSeinNavigationComponent* Nav = World->GetComponent<FSeinNavigationComponent>(H))
				{
					BodyRadius = Nav->FallbackFootprintRadius;
				}
			}
			if (BodyRadius <= FFixedPoint::Zero) continue; // intangible → doesn't occupy ground
			ParkedCentres.Add(Entity->Transform.GetLocation());
			ParkedRadii.Add(BodyRadius);
		}
	}

	// A slot is "off nav" when its cell isn't passable — off the play area, on a BAKED obstacle, OR under
	// a runtime DYNAMIC blocker (bBlocksNav — a non-baked cover wall / deployable). We classify with the
	// DYNAMIC-aware resolver so on/off-nav reflects DE-FACTO availability (bake MINUS runtime blockers),
	// not just the static bake. Otherwise a plain slot landing on a dynamic wall reads "on-nav", the
	// preview marker renders ON the wall, and the unit is delivered short of it (the movement floor stops
	// it clear) — a WYSIWYG break. Preview and commit both run THIS shared function, so swapping it keeps
	// them identical (root CLAUDE.md #6). Cover slots are unaffected: the cover PostProcessPositions hook
	// runs AFTER this and overrides, sourcing slots from FindNearbySlots (itself dynamic-filtered) near
	// the target — so this can't relocate a cover slot out from under the hook. (The relocation target
	// below, NavProjectFreeResolver, is still static-only; a relocated slot lands on the nearest
	// bake-walkable + peer-free cell, which is almost always dynamic-clear too since blockers are sparse.
	// A relocation landing on ANOTHER dynamic blocker is a rare residual, tracked separately.)
	// With no resolver we can't tell → treat everything as on-nav (permit-on-no-data).
	const bool bCanTestPassable = World->DynamicPassableResolver.IsBound();

	auto RadiusAt = [&Radii](int32 i) -> FFixedPoint
	{
		return Radii.IsValidIndex(i) ? Radii[i] : FFixedPoint::Zero;
	};

	// Slot-vs-parked overlap test (planar, footprint-vs-footprint) — a slot ON a body relocates.
	const auto OverlapsParked = [&ParkedCentres, &ParkedRadii](const FFixedVector& P, FFixedPoint R) -> bool
	{
		for (int32 k = 0; k < ParkedCentres.Num(); ++k)
		{
			const FFixedPoint DX = P.X - ParkedCentres[k].X;
			const FFixedPoint DY = P.Y - ParkedCentres[k].Y;
			const FFixedPoint MinDist = R + ParkedRadii[k];
			if (DX * DX + DY * DY < MinDist * MinDist) return true;
		}
		return false;
	};

	// CLEAN slots keep their exact spot and join the occupied set (relocated slots must avoid them
	// too). A slot relocates when it is off-nav (baked wall / play-area edge / dynamic blocker) OR
	// on a parked body. Occupied is seeded with the parked bodies FIRST so every relocation avoids
	// them. Deterministic index order throughout.
	TArray<FFixedVector> Occupied = ParkedCentres;
	TArray<FFixedPoint>  OccupiedRadii = ParkedRadii;
	TArray<int32>        Relocate;
	Occupied.Reserve(ParkedCentres.Num() + N);
	OccupiedRadii.Reserve(ParkedRadii.Num() + N);
	for (int32 i = 0; i < N; ++i)
	{
		const bool bOnNav = !bCanTestPassable || World->DynamicPassableResolver.Execute(Positions[i]);
		if (bOnNav && !OverlapsParked(Positions[i], RadiusAt(i)))
		{
			Occupied.Add(Positions[i]);
			OccupiedRadii.Add(RadiusAt(i));
		}
		else
		{
			Relocate.Add(i);
		}
	}
	if (Relocate.Num() == 0) return; // whole formation already clean — common case, zero work

	// Relocate each flagged slot to its nearest free cell, accumulating occupancy as we go so the
	// overflowing slots neither collide with parked bodies, the clean slots, nor each other.
	for (const int32 i : Relocate)
	{
		const FFixedPoint Ri = RadiusAt(i);
		FFixedVector Projected;
		if (World->NavProjectFreeResolver.Execute(Positions[i], Ri, Occupied, OccupiedRadii, Projected))
		{
			Positions[i] = Projected;
		}
		Occupied.Add(Positions[i]);
		OccupiedRadii.Add(Ri);
	}
}

FSeinOrderTarget USeinFormation::MakeInnerLayoutTarget(
	const FFixedVector& Anchor,
	const FFixedVector& Centroid,
	const FFixedQuaternion& Facing,
	const TSoftClassPtr<USeinFormation>& FormationClass)
{
	FSeinOrderTarget Target;
	Target.Anchor          = Anchor;
	Target.CurrentCentroid = Centroid;
	Target.CurrentFacing   = Facing;
	Target.FormationClass  = FormationClass;
	// No GuidePoints and no FormationTag, BY CONSTRUCTION — the parent formation owns the gesture; the
	// inner layout keeps its own compact shape. Do NOT add them here or at call sites.
	return Target;
}

FSeinFormationLayout USeinFormation::BuildFormation_Implementation(
	USeinWorldSubsystem* World,
	const TArray<FSeinEntityHandle>& Members,
	const FSeinOrderTarget& Target)
{
	// Default: BLOB. Every member shares the one (already nav-projected) anchor —
	// the mass-select single-destination model; the hard collision floor packs them on arrival. Facing
	// rotates to face the move direction. USeinBlobFormation inherits this as-is.
	FSeinFormationLayout Layout;
	Layout.Facing = ComputeFormationFacing(Target.CurrentCentroid, Target.CurrentFacing, Target.Anchor);
	Layout.Positions.Init(Target.Anchor, Members.Num());
	GatherFootprintRadii(World, Members, Layout.Radii); // sized dots even for the blob
	return Layout;
}

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovement.cpp
 */

#include "Movement/SeinMovement.h"
#include "Math/MathLib.h"
#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"
#include "SeinPathTypes.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Types/Entity.h"
#include "Types/Quat.h"
#include "Types/Vector.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Components/SeinExtentsComponent.h"
#include "Components/SeinExtentsHelpers.h"  // SeinExtentsHelpers::BoundingRadius

#if UE_ENABLE_DEBUG_DRAWING
#include "Debug/SeinDebugDrawCull.h"
#include "DrawDebugHelpers.h"
#endif

FFixedPoint USeinMovement::ShortestAngleDelta(FFixedPoint From, FFixedPoint To)
{
	FFixedPoint Delta = To - From;
	// Wrap to [-π, π]. At most two iterations needed if inputs are already in
	// [-π, π] (typical case); guard with a loop anyway for safety.
	while (Delta > FFixedPoint::Pi)    { Delta = Delta - FFixedPoint::TwoPi; }
	while (Delta < -FFixedPoint::Pi)   { Delta = Delta + FFixedPoint::TwoPi; }
	return Delta;
}

FFixedPoint USeinMovement::ClampFP(FFixedPoint Val, FFixedPoint Min, FFixedPoint Max)
{
	if (Val < Min) return Min;
	if (Val > Max) return Max;
	return Val;
}

FFixedPoint USeinMovement::SmoothAngleToward(
	FFixedPoint Current,
	FFixedPoint Target,
	FFixedPoint MaxChangePerSec,
	FFixedPoint DeltaTime)
{
	// Per-tick change budget. Non-positive Dt or rate degrades to a hard
	// snap (Target) — safer than producing NaN/inf in pathological cases.
	const FFixedPoint MaxChange = MaxChangePerSec * DeltaTime;
	if (MaxChange <= FFixedPoint::Zero) return Target;

	FFixedPoint Delta = Target - Current;
	if (Delta > MaxChange)  Delta = MaxChange;
	if (Delta < -MaxChange) Delta = -MaxChange;
	return Current + Delta;
}

FFixedQuaternion USeinMovement::YawOnly(FFixedPoint YawRadians)
{
	// Roll=0, Pitch=0, Yaw=input. Matches MakeFromEulers' (X=roll, Y=pitch,
	// Z=yaw) convention.
	return FFixedQuaternion::MakeFromEulers(FFixedVector(FFixedPoint::Zero, FFixedPoint::Zero, YawRadians));
}

FFixedQuaternion USeinMovement::YawPitchRoll(FFixedPoint YawRadians, FFixedPoint PitchRadians, FFixedPoint RollRadians)
{
	return FFixedQuaternion::MakeFromEulers(FFixedVector(RollRadians, PitchRadians, YawRadians));
}

FFixedPoint USeinMovement::ComputeSlopePitch(
	const FFixedVector& Pos,
	FFixedPoint Yaw,
	USeinNavigation* Nav) const
{
	if (!Nav) return FFixedPoint::Zero;

	// Sample terrain height at the current position and a short distance
	// ahead along the facing direction. 50cm sample distance — responsive
	// on one-cell slopes, numerically stable, and smaller than any
	// reasonable CellSize so the pitch reacts within a single cell.
	const FFixedPoint SampleDist = FFixedPoint::FromInt(50);
	const FFixedPoint CosY = SeinMath::Cos(Yaw);
	const FFixedPoint SinY = SeinMath::Sin(Yaw);

	const FFixedVector AheadPos(
		Pos.X + CosY * SampleDist,
		Pos.Y + SinY * SampleDist,
		Pos.Z);

	// Route through QueryReferenceZ so the walkability gate matches the
	// Z-snap in ApplyGroundSnapAndAltitude. Ground movements (default)
	// use bWalkableOnly=true — wall/cliff cells return false and pitch
	// zeros out, preventing the model from visually ramping up walls.
	// Flying overrides use bWalkableOnly=false and see terrain below.
	FFixedPoint Z0, Z1;
	if (!QueryReferenceZ(Nav, Pos, Z0)) return FFixedPoint::Zero;
	if (!QueryReferenceZ(Nav, AheadPos, Z1)) return FFixedPoint::Zero;

	// Step-height guard: if either sample's height is far from the
	// entity's actual Z, the sample crossed into a wall-top cell at a
	// corner. The symmetric bilinear SafeHeight clamp pulls ground
	// neighbors UP to wall-top PrimaryZ at corners, producing extreme
	// height reads. No traversable slope produces a delta > MaxStepHeight
	// within 50 cm — treat as flat.
	if (CachedMaxStepHeight > FFixedPoint::Zero)
	{
		FFixedPoint Diff0 = Z0 - Pos.Z;
		if (Diff0 < FFixedPoint::Zero) Diff0 = -Diff0;
		if (Diff0 > CachedMaxStepHeight) return FFixedPoint::Zero;

		FFixedPoint Diff1 = Z1 - Pos.Z;
		if (Diff1 < FFixedPoint::Zero) Diff1 = -Diff1;
		if (Diff1 > CachedMaxStepHeight) return FFixedPoint::Zero;
	}

	const FFixedPoint DeltaZ = Z1 - Z0;
	FFixedPoint Pitch = SeinMath::Atan2(DeltaZ, SampleDist);

	// Cap the raw pitch magnitude. The step-height guard above handles
	// unwalkable walls (returns 0). But a wall *just below* MaxStepHeight
	// — short enough to step onto — still produces extreme slopes here
	// (a 60cm step over 50cm sample = 50°). That magnitude is not natural
	// terrain; cap it so a brief wall-edge sample doesn't pitch the model
	// 50° forward. Callers further rate-limit the per-tick change via
	// SmoothAngleToward so even the capped target reaches the rig
	// gradually rather than as a snap.
	//
	// 20° (π/9) — natural-terrain max walkable slope is typically 25-30°;
	// 20° preserves visual cue without ever crossing into "extreme."
	const FFixedPoint SlopeCap = FFixedPoint::Pi / FFixedPoint::FromInt(9);
	if (Pitch >  SlopeCap) Pitch =  SlopeCap;
	if (Pitch < -SlopeCap) Pitch = -SlopeCap;
	return Pitch;
}

FFixedPoint USeinMovement::ComputeSlopeRoll(
	const FFixedVector& Pos,
	FFixedPoint Yaw,
	USeinNavigation* Nav) const
{
	if (!Nav) return FFixedPoint::Zero;

	// Sample terrain height at two points perpendicular to facing —
	// one on the right, one on the left. Same 50cm sample distance as
	// ComputeSlopePitch for consistency.
	const FFixedPoint SampleDist = FFixedPoint::FromInt(50);
	const FFixedPoint CosY = SeinMath::Cos(Yaw);
	const FFixedPoint SinY = SeinMath::Sin(Yaw);

	// Right perpendicular at yaw θ: (-sin θ, cos θ, 0). Verified:
	// at yaw=0, right = (0, 1, 0) = +Y = RightVector.
	const FFixedPoint RightX = -SinY;
	const FFixedPoint RightY = CosY;

	const FFixedVector RightPos(
		Pos.X + RightX * SampleDist,
		Pos.Y + RightY * SampleDist,
		Pos.Z);
	const FFixedVector LeftPos(
		Pos.X - RightX * SampleDist,
		Pos.Y - RightY * SampleDist,
		Pos.Z);

	// Same QueryReferenceZ gate as ComputeSlopePitch — ground movements
	// don't see wall heights, preventing extreme roll near cliffs/walls.
	FFixedPoint ZRight, ZLeft;
	if (!QueryReferenceZ(Nav, RightPos, ZRight)) return FFixedPoint::Zero;
	if (!QueryReferenceZ(Nav, LeftPos, ZLeft)) return FFixedPoint::Zero;

	// Step-height guard: same as ComputeSlopePitch — if either sample's
	// height is far from the entity's Z, it crossed into a wall-top cell
	// at a corner. Treat as flat to prevent extreme roll.
	if (CachedMaxStepHeight > FFixedPoint::Zero)
	{
		FFixedPoint DiffR = ZRight - Pos.Z;
		if (DiffR < FFixedPoint::Zero) DiffR = -DiffR;
		if (DiffR > CachedMaxStepHeight) return FFixedPoint::Zero;

		FFixedPoint DiffL = ZLeft - Pos.Z;
		if (DiffL < FFixedPoint::Zero) DiffL = -DiffL;
		if (DiffL > CachedMaxStepHeight) return FFixedPoint::Zero;
	}

	// MakeFromEulers: positive Roll tilts right side DOWN (UE left-
	// handed). When right terrain is higher (ZRight > ZLeft), we need
	// negative Roll so the entity banks right-side-up to match. The
	// atan2(ZLeft - ZRight, ...) form naturally produces negative when
	// right is higher.
	const FFixedPoint Dist = SampleDist * FFixedPoint::Two;
	FFixedPoint Roll = SeinMath::Atan2(ZLeft - ZRight, Dist);

	// Cap the raw roll magnitude — same rationale as ComputeSlopePitch's
	// cap. A unit walking parallel to a wall *just below* MaxStepHeight
	// would otherwise see roll = atan2(75, 100) ≈ 37° on the wall-side
	// roll sample. 20° (π/9) cap preserves visual cue without crossing
	// into "extreme lean."
	const FFixedPoint SlopeCap = FFixedPoint::Pi / FFixedPoint::FromInt(9);
	if (Roll >  SlopeCap) Roll =  SlopeCap;
	if (Roll < -SlopeCap) Roll = -SlopeCap;
	return Roll;
}

FFixedPoint USeinMovement::YawFromRotation(const FFixedQuaternion& Rotation)
{
	// Extract via the forward vector rather than Eulers() — Eulers has branches
	// for gimbal singularities at ±90° pitch that add cost we don't need for
	// upright yaw-only rotations.
	const FFixedVector Forward = Rotation.RotateVector(FFixedVector::ForwardVector);
	return SeinMath::Atan2(Forward.Y, Forward.X);
}

void USeinMovement::AdvanceWaypointAlongPath(
	int32& CurrentWaypointIndex,
	const FSeinPath& Path,
	const FFixedVector& AgentPos,
	FFixedPoint CloseRadius)
{
	const int32 N = Path.Waypoints.Num();
	if (N <= 1) return;
	if (CurrentWaypointIndex < 0) CurrentWaypointIndex = 0;

	const FFixedPoint CloseRadiusSq = CloseRadius * CloseRadius;

	while (CurrentWaypointIndex < N - 1)
	{
		const FFixedVector& Wp = Path.Waypoints[CurrentWaypointIndex];

		// Cross-over test: dot(AgentPos - Wp, NextWp - Wp) > 0 means the
		// agent's offset from the current waypoint projects positively
		// onto the segment direction toward the next waypoint — i.e., the
		// agent has already CROSSED the current waypoint. Robust against
		// overshoot at speed where the distance test below misses
		// (waypoint behind chassis but outside CloseRadius).
		const FFixedVector& NextWp = Path.Waypoints[CurrentWaypointIndex + 1];
		const FFixedPoint SegDx = NextWp.X - Wp.X;
		const FFixedPoint SegDy = NextWp.Y - Wp.Y;
		const FFixedPoint OffDx = AgentPos.X - Wp.X;
		const FFixedPoint OffDy = AgentPos.Y - Wp.Y;
		const FFixedPoint Dot   = OffDx * SegDx + OffDy * SegDy;

		bool bAdvance = (Dot > FFixedPoint::Zero);

		// Distance fallback: agent close to the waypoint but not past it
		// (e.g., waypoint sits in a tight spline-sample cluster near a
		// corner). Catches the "approach + barely arrive" case while the
		// crossover test catches the "overshoot at speed" case.
		if (!bAdvance)
		{
			const FFixedPoint DistSq = OffDx * OffDx + OffDy * OffDy;
			if (DistSq <= CloseRadiusSq) bAdvance = true;
		}

		if (bAdvance) ++CurrentWaypointIndex;
		else break;
	}
}

FFixedVector USeinMovement::ResolveLookAheadPoint(
	const FFixedVector& AgentPos,
	const FSeinPath& Path,
	int32 CurrentWaypointIndex,
	FFixedPoint LookAhead,
	FFixedPoint /*MaxCornerAngleRadians (deprecated, no-op)*/)
{
	const int32 N = Path.Waypoints.Num();
	if (N == 0) return AgentPos;
	if (CurrentWaypointIndex >= N) return Path.Waypoints[N - 1];
	if (CurrentWaypointIndex < 0) CurrentWaypointIndex = 0;

	// Pure linear look-ahead walker with controller-side cluster skip.
	//
	// History: previously this function carried a `MaxCornerAngleRadians`-
	// driven cos-falloff weight on each segment past the current one — meant
	// to smoothly reduce the carrot's reach across corners and prevent
	// pure-pursuit corner-cutting. In practice it tangled badly with off-path
	// drift (the synthetic AgentPos→Waypoints[CurIdx] segment contaminated
	// cumulative turn angle), produced multiple regressions, and ultimately
	// got stripped. The parameter is kept for signature ABI but ignored.
	//
	// What this function does NOW:
	//   1. Pre-thins the working polyline: drops waypoints that are CLOSE to
	//      their predecessor AND COLLINEAR with the next segment. These are
	//      smoother-emitted intermediates (LoS failed on an off-path wall-
	//      edge cell), not real corners. Skipping them prevents the carrot
	//      from pinning near the agent and causing perpendicular steering
	//      jogs. Per-tick, so as the agent moves clusters re-evaluate.
	//   2. Walks the thinned polyline at 1:1 budget consumption — pure linear.
	//      The carrot lands at `LookAhead` world units forward along the
	//      thinned polyline from the agent.
	//
	// Path.Waypoints itself is NOT modified — drift detection, repath, and
	// arrival checks still see all waypoints. This is a controller-side
	// view-of-the-path, not an emitter-side mutation.

	// ----------------------------------------------------------------
	// Step 1: build thinned working polyline.
	//
	// Thresholds expressed as rational expressions (FromInt) rather than
	// FromFloat so the CDO ctor never runs a runtime float→fixed conversion.
	// Bit-identical across PC / ARM / mobile / console.
	// ----------------------------------------------------------------
	// 2m — segments shorter than this almost never represent intentional
	// turns. Anything longer counts as a real turn and is preserved.
	const FFixedPoint CloseSegThreshold = FFixedPoint::FromInt(200);
	// 866/1000 ≈ cos(30°). Two consecutive segment directions within 30° of
	// each other count as "roughly the same direction." Tighter than this
	// would over-classify mild curves as real corners; looser would skip
	// real 30-45° turns.
	const FFixedPoint CollinearCosThreshold = FFixedPoint::FromInt(866) / FFixedPoint::FromInt(1000);

	TArray<FFixedVector, TInlineAllocator<16>> Thinned;
	Thinned.Reserve(N - CurrentWaypointIndex);
	for (int32 i = CurrentWaypointIndex; i < N; ++i)
	{
		const FFixedVector& Cand = Path.Waypoints[i];

		// Always include the LAST waypoint (destination) — never let the
		// thinning skip arrival.
		if (i == N - 1)
		{
			Thinned.Add(Cand);
			break;
		}

		// "Previous" position for the close-and-collinear test is either the
		// last waypoint we kept, or the agent itself if we haven't kept any
		// yet. This lets the first authored waypoint get skipped if it's a
		// near-origin cluster intermediate (the common "WP[0] right beside
		// the car" case).
		const FFixedVector PrevPos = (Thinned.Num() > 0) ? Thinned.Last() : AgentPos;
		const FFixedVector& NextRaw = Path.Waypoints[i + 1];

		FFixedVector PrevToCand = Cand - PrevPos;
		FFixedVector CandToNext = NextRaw - Cand;
		PrevToCand.Z = FFixedPoint::Zero;
		CandToNext.Z = FFixedPoint::Zero;
		const FFixedPoint LenA = PrevToCand.Size();
		const FFixedPoint LenB = CandToNext.Size();

		if (LenA > FFixedPoint::Epsilon && LenA < CloseSegThreshold
			&& LenB > FFixedPoint::Epsilon)
		{
			const FFixedVector NormA = PrevToCand / LenA;
			const FFixedVector NormB = CandToNext / LenB;
			const FFixedPoint Dot = NormA.X * NormB.X + NormA.Y * NormB.Y;
			if (Dot >= CollinearCosThreshold)
			{
				// Cluster intermediate — skip.
				continue;
			}
		}
		Thinned.Add(Cand);
	}

	if (Thinned.Num() == 0) return Path.Waypoints[N - 1];

	// ----------------------------------------------------------------
	// Step 2: linear walk on the thinned polyline.
	//
	// First segment runs from the agent itself to Thinned[0], then each
	// subsequent segment connects Thinned[i] to Thinned[i+1]. Walking from
	// the agent keeps the carrot strictly ahead even when the unit is
	// partway through a segment. Planar (XY) measurement only — Z drift on
	// slopes shouldn't shorten the look-ahead.
	// ----------------------------------------------------------------
	FFixedVector SegStart = AgentPos;
	FFixedVector SegEnd = Thinned[0];
	int32 ThinIdx = 0;
	FFixedPoint Remaining = (LookAhead < FFixedPoint::Zero) ? FFixedPoint::Zero : LookAhead;

	while (true)
	{
		FFixedVector Seg = SegEnd - SegStart;
		Seg.Z = FFixedPoint::Zero;
		const FFixedPoint SegLen = Seg.Size();

		if (Remaining <= SegLen)
		{
			// Carrot lands within this segment.
			if (SegLen > FFixedPoint::Epsilon)
			{
				const FFixedVector Dir = FFixedVector::GetSafeNormal(Seg);
				FFixedVector Out = SegStart + Dir * Remaining;
				// Z interpolation along the segment by XY fraction so the
				// carrot's elevation tracks the path's slope continuously
				// between waypoints (steering-vector debug viz consumes this).
				const FFixedPoint T = Remaining / SegLen;
				Out.Z = SegStart.Z + (SegEnd.Z - SegStart.Z) * T;
				return Out;
			}
			// Zero-length segment with Remaining ≈ 0 → sit at SegEnd.
			return SegEnd;
		}

		// Consume this segment and advance.
		Remaining = Remaining - SegLen;
		++ThinIdx;
		if (ThinIdx >= Thinned.Num())
		{
			// Walked off the end — clamp to terminal waypoint.
			return Thinned.Last();
		}
		SegStart = SegEnd;
		SegEnd = Thinned[ThinIdx];
	}
}

FFixedPoint USeinMovement::ComputeAdaptiveLookAhead(
	FFixedPoint BaseDistance,
	FFixedPoint TimeHorizon,
	FFixedPoint AbsSpeed)
{
	// Negative TimeHorizon is treated as 0 — designer can disable the speed
	// boost by setting TimeHorizon=0 without breaking the formula.
	if (TimeHorizon < FFixedPoint::Zero) TimeHorizon = FFixedPoint::Zero;
	// AbsSpeed is expected ≥ 0 by contract; clamp defensively in case caller
	// passed a signed scalar.
	if (AbsSpeed < FFixedPoint::Zero) AbsSpeed = -AbsSpeed;

	FFixedPoint Effective = BaseDistance + AbsSpeed * TimeHorizon;
	if (Effective < FFixedPoint::Zero) Effective = FFixedPoint::Zero;
	return Effective;
}

FFixedPoint USeinMovement::StepSpeedToward(
	FFixedPoint Current, FFixedPoint Target,
	FFixedPoint Accel, FFixedPoint Decel, FFixedPoint Dt)
{
	// Choose accel vs decel by whether |speed| is growing or shrinking. Sign
	// flips (e.g. forward → reverse transition) count as decel since the
	// magnitude must first cross zero.
	const FFixedPoint AbsCur = (Current < FFixedPoint::Zero) ? -Current : Current;
	const FFixedPoint AbsTgt = (Target  < FFixedPoint::Zero) ? -Target  : Target;
	const bool bSignFlip = (Current * Target) < FFixedPoint::Zero;
	const FFixedPoint Rate = (bSignFlip || AbsTgt < AbsCur) ? Decel : Accel;
	const FFixedPoint MaxStep = Rate * Dt;

	const FFixedPoint Delta = Target - Current;
	if (Delta > MaxStep)  return Current + MaxStep;
	if (Delta < -MaxStep) return Current - MaxStep;
	return Target;
}

FFixedPoint USeinMovement::KinematicArrivalSpeedCap(
	FFixedPoint DistToFinal, FFixedPoint Deceleration)
{
	// No decel = no kinematic cap. Return a value larger than any reasonable
	// MoveSpeed so the cap effectively "doesn't apply" at the call site.
	if (Deceleration <= FFixedPoint::Zero) return FFixedPoint::FromInt(1000000);
	if (DistToFinal <= FFixedPoint::Zero) return FFixedPoint::Zero;

	const FFixedPoint TwoAD = FFixedPoint::Two * Deceleration * DistToFinal;
	if (TwoAD <= FFixedPoint::Zero) return FFixedPoint::Zero;
	return SeinMath::Sqrt(TwoAD);
}

bool USeinMovement::QueryReferenceZ(USeinNavigation* Nav, const FFixedVector& WorldPos, FFixedPoint& OutZ) const
{
	// Default: walkable-only gate ON. Refuses on blocked cells (wall tops,
	// cube interiors) so a ground unit sliding across a blocked sliver
	// holds its previous Z instead of popping onto the wall.
	return Nav ? Nav->GetCellHeightAt(WorldPos, OutZ, /*bWalkableOnly=*/ true) : false;
}

void USeinMovement::ApplyGroundSnapAndAltitude(
	FFixedVector& NewPos,
	const FSeinMovementComponent* MovementData,
	USeinNavigation* Nav,
	FFixedPoint DeltaTime) const
{
	if (!Nav) return;

	FFixedPoint RefZ;
	if (QueryReferenceZ(Nav, NewPos, RefZ))
	{
		// Target Z = reference + altitude offset. Altitude is sourced from the
		// virtual `GetAltitude(MovementData)` — default returns 0 (ground
		// movements snap directly to RefZ); hover / flight subclasses override
		// to read their altitude out of `MovementClassData` (the polymorphic
		// per-class sub-data). Decoupling Altitude from FSeinMovementComponent
		// keeps the top-level component free of class-specific fields.
		const FFixedPoint TargetZ = RefZ + GetAltitude(MovementData);

		// Rate-limited Z snap. NewPos.Z at function entry IS the entity's
		// previous Z (movement subclasses only modify X/Y before this call),
		// so the cap-against-Delta naturally smooths multi-tick transitions
		// without needing dedicated storage on the component. Cap at
		// `TopSpeed × DeltaTime` — vertical motion bounded by horizontal
		// walking speed. Natural slopes ≤45° produce Z changes ≤ horizontal
		// step, so they pass through fully. Wall-edge snaps (the bilinear
		// SafeHeight clamp jumping primary-cell-height by 30-70cm in a
		// single tick) get smoothed to a multi-tick ramp.
		//
		// Skipped (snap-to-target) when MovementData is null or TopSpeed is
		// 0 — there's no meaningful rate to cap against. Flying subclasses
		// that want a different vertical rate (e.g. instant takeoff to
		// cruise altitude) can override this method.
		if (MovementData && DeltaTime > FFixedPoint::Zero && MovementData->TopSpeed > FFixedPoint::Zero)
		{
			const FFixedPoint MaxZChange = MovementData->TopSpeed * DeltaTime;
			FFixedPoint Delta = TargetZ - NewPos.Z;
			if (Delta > MaxZChange)  Delta = MaxZChange;
			if (Delta < -MaxZChange) Delta = -MaxZChange;
			NewPos.Z = NewPos.Z + Delta;
		}
		else
		{
			NewPos.Z = TargetZ;
		}
	}
	// No reference sample at NewPos (out of bounds, blocked-cell refusal
	// from the default ground accessor) — leave Z as the movement subclass
	// set it. Avoids surprise teleports for units near nav edges or sliding
	// across wall slivers.
}

bool USeinMovement::IsFootprintPassable(const FFixedVector& Pos, USeinNavigation* Nav) const
{
	if (!Nav) return true;
	if (!Nav->IsPassable(Pos)) return false;
	for (int32 i = 0; i < CachedNumFootprintSamples; ++i)
	{
		const FFixedVector SamplePos(
			Pos.X + CachedFootprintSamples[i].X,
			Pos.Y + CachedFootprintSamples[i].Y,
			Pos.Z);
		if (!Nav->IsPassable(SamplePos)) return false;
	}
	return true;
}

FFixedVector USeinMovement::ResolveNavCollision(
	const FFixedVector& OldPos,
	const FFixedVector& NewPos,
	USeinNavigation* Nav,
	const FFixedVector* AuthoritativeDest) const
{
	if (!Nav) return NewPos;

	// Authoritative-destination overrule: when the candidate sits within reach of
	// an authoritative destination (a cover slot), let the unit move there even
	// though the cell is bake-blocked. The slot is a valid standing spot; the
	// blocked ("red") cell under it is a coarse-resolution false-negative, not a
	// wall (root CLAUDE.md #6). Scoped tightly to the slot's immediate vicinity so
	// it never lets the body clip walls anywhere else along the path.
	if (AuthoritativeDest)
	{
		FFixedVector ToDest = NewPos - *AuthoritativeDest;
		ToDest.Z = FFixedPoint::Zero;
		const FFixedPoint ExemptRadius = CachedCollisionRadius + FFixedPoint::FromInt(50);
		if (ToDest.SizeSquared() <= ExemptRadius * ExemptRadius)
		{
			return NewPos;
		}
	}

	// Combined check: footprint passability AND step-height gate. The step-
	// height gate prevents units from stepping onto passable cells whose
	// ground height is too far from the current position (wall-top cells
	// connected to ground via a ramp elsewhere). Without this, a unit next
	// to a wall whose top is passable can teleport vertically in one tick.
	const auto IsValidStep = [&](const FFixedVector& Candidate) -> bool
	{
		if (!IsFootprintPassable(Candidate, Nav)) return false;
		if (CachedMaxStepHeight > FFixedPoint::Zero)
		{
			FFixedPoint OldGroundZ, NewGroundZ;
			if (QueryReferenceZ(Nav, OldPos, OldGroundZ) &&
				QueryReferenceZ(Nav, Candidate, NewGroundZ))
			{
				FFixedPoint HeightDiff = NewGroundZ - OldGroundZ;
				if (HeightDiff < FFixedPoint::Zero) HeightDiff = -HeightDiff;
				if (HeightDiff > CachedMaxStepHeight) return false;
			}
		}
		return true;
	};

	// Fast path — full step is passable and within step height.
	if (IsValidStep(NewPos)) return NewPos;

	// X-axis-only slide.
	const FFixedVector XOnly(NewPos.X, OldPos.Y, NewPos.Z);
	if (IsValidStep(XOnly)) return XOnly;

	// Y-axis-only slide.
	const FFixedVector YOnly(OldPos.X, NewPos.Y, NewPos.Z);
	if (IsValidStep(YOnly)) return YOnly;

	// Cornered / dead-ended. Hold position.
	return OldPos;
}

FFixedVector USeinMovement::ApplyAvoidanceSteer(const FSeinMovementContext& Ctx, const FFixedVector& DesiredDir) const
{
	// PURE READ — never query the spatial hash or read neighbour state here.
	// Movement runs through the insertion-ordered latent-action manager (live
	// neighbour transforms), so any neighbour read at this point would be
	// order-dependent → desync. The steer was computed ONE-SIDED at PreTick by
	// FSeinAvoidanceSystem; here we only consume our own already-written field.
	if (!Ctx.MovementData) return DesiredDir;
	const FFixedVector& Steer = Ctx.MovementData->AvoidanceSteer;

	// Bit-exact no-op when not avoiding: return the input direction UNCHANGED (no
	// renormalize), so AvoidanceStrength = 0 / no-neighbour units move identically
	// to a world with no avoidance.
	if (Steer.SizeSquared() <= FFixedPoint::Epsilon) return DesiredDir;

	// Bend the (unit) desired direction by the lateral steer, then renormalize.
	return FFixedVector::GetSafeNormal(
		FFixedVector(DesiredDir.X + Steer.X, DesiredDir.Y + Steer.Y, DesiredDir.Z));
}

#if UE_ENABLE_DEBUG_DRAWING
void USeinMovement::DrawSteeringDebugViz(
	UWorld* World,
	const FFixedVector& EntityPos,
	float FootprintRadius,
	const FFixedVector& Velocity,
	const FFixedVector& AvoidanceSteer)
{
	if (!World || FootprintRadius <= 0.0f) return;

	// Lifetime 0 = one frame. ZLift floats the geometry just above the nav-floor tint so it
	// doesn't z-fight / blend into invisibility on baked terrain.
	const float DrawLifetime = 0.0f;
	const float ZLift = 25.0f;

	const FVector EntityPosFloat(
		EntityPos.X.ToFloat(),
		EntityPos.Y.ToFloat(),
		EntityPos.Z.ToFloat());
	const FVector Center(EntityPosFloat.X, EntityPosFloat.Y, EntityPosFloat.Z + ZLift);

	const FColor OrangeColor(255, 220, 0, 255); // footprint ring + velocity vector
	const FColor AvoidColor(255, 0, 0, 255);    // avoidance vector

	// Footprint ring. DrawDebugCircle defaults to the XZ plane — the explicit Y/Z axis pair
	// lays it flat in XY on the ground.
	DrawDebugCircle(
		World, Center, FootprintRadius, /*Segments*/ 32,
		OrangeColor, /*PersistentLines*/ false, DrawLifetime, /*DepthPriority*/ 0,
		/*Thickness*/ 5.0f,
		/*YAxis*/ FVector(1, 0, 0),
		/*ZAxis*/ FVector(0, 1, 0),
		/*DrawAxis*/ false);

	// VELOCITY arrow — ORANGE, drawn straight from the entity along the WORLD-SPACE velocity
	// (entity → velocity) at its true magnitude. Velocity is ALREADY a world vector — it is NOT
	// rotated by the chassis transform; it lines up with facing only because these units travel
	// along their facing. Origin offset to the footprint edge so short arrows clear large units.
	// Skips at rest (|velocity| ~ 0); the caller also passes zero when there's no active order.
	const FVector VelocityFloat(Velocity.X.ToFloat(), Velocity.Y.ToFloat(), 0.0f);
	const float VelocitySize = static_cast<float>(VelocityFloat.Size());
	if (VelocitySize > KINDA_SMALL_NUMBER)
	{
		const FVector Origin = UE::SeinARTSMovement::DebugDraw::ComputeFootprintOriginAlong(
			EntityPosFloat, VelocityFloat, FootprintRadius, ZLift);
		DrawDebugDirectionalArrow(World, Origin, Origin + VelocityFloat,
			/*ArrowSize*/ 20.0f, OrangeColor,
			/*PersistentLines*/ false, DrawLifetime, /*DepthPriority*/ 0, /*Thickness*/ 5.0f);

		// AVOIDANCE arrow — RED, the world-space steer expressed as the sideways velocity it adds
		// (AvoidanceSteer × current speed), directly comparable to the orange velocity arrow and
		// likewise NOT chassis-rotated. Skips when not avoiding.
		const FVector AvoidFloat(
			AvoidanceSteer.X.ToFloat() * VelocitySize,
			AvoidanceSteer.Y.ToFloat() * VelocitySize,
			0.0f);
		if (AvoidFloat.SizeSquared() > KINDA_SMALL_NUMBER)
		{
			const FVector AvoidOrigin = UE::SeinARTSMovement::DebugDraw::ComputeFootprintOriginAlong(
				EntityPosFloat, AvoidFloat, FootprintRadius, ZLift);
			DrawDebugDirectionalArrow(World, AvoidOrigin, AvoidOrigin + AvoidFloat,
				/*ArrowSize*/ 20.0f, AvoidColor,
				/*PersistentLines*/ false, DrawLifetime, /*DepthPriority*/ 0, /*Thickness*/ 5.0f);
		}
	}
}
#endif // UE_ENABLE_DEBUG_DRAWING

FFixedPoint USeinMovement::ResolveCollisionRadius(
	USeinWorldSubsystem* World,
	FSeinEntityHandle SelfHandle,
	const FSeinNavigationComponent* NavData)
{
	// Cascade for the effective collision radius:
	//   Tier 1: FSeinExtentsComponent on the entity (if present).
	//           Per-shape bounding radius via SeinExtentsHelpers::BoundingRadius:
	//             - Capsule → Shape.Radius
	//             - Box     → sqrt(HalfExtentX² + HalfExtentY²) (DIAGONAL)
	//           Diagonal — not max half-extent — is the smallest circle
	//           that fully contains the box (center-to-corner reach).
	//           Compound entities take the max across all shapes.
	//   Tier 2: FSeinNavigationComponent.FallbackFootprintRadius.
	//   Tier 3: 0 — point-only fallback.
	//
	// Designer ergonomics: configuring an Extents component on a unit BP
	// (which is already required for FoW / nav blocking / hit detection)
	// automatically drives the correct collision radius here. No second
	// "footprint" prop to keep in sync. NavComp.FallbackFootprintRadius is
	// the fallback for units that don't have Extents.
	FFixedPoint Radius = FFixedPoint::Zero;

	if (World)
	{
		const FSeinExtentsComponent* Extents = World->GetComponent<FSeinExtentsComponent>(SelfHandle);
		if (Extents && Extents->Shapes.Num() > 0)
		{
			for (const FSeinExtentsShape& Shape : Extents->Shapes)
			{
				const FFixedPoint ShapeRadius = SeinExtentsHelpers::BoundingRadius(Shape);
				if (ShapeRadius > Radius) Radius = ShapeRadius;
			}
		}
	}

	if (Radius <= FFixedPoint::Zero && NavData)
	{
		Radius = NavData->FallbackFootprintRadius;
	}

	return Radius;
}

void USeinMovement::CacheFootprintFromContext(const FSeinMovementContext& Ctx)
{
	// Single source of truth — ResolveCollisionRadius is also called by
	// PlanPath when building the path request, so path planning and
	// collision agree on body size end-to-end.
	const FFixedPoint Radius = ResolveCollisionRadius(Ctx.World, Ctx.SelfHandle, Ctx.NavData);

	CachedCollisionRadius = Radius;

	if (Radius > FFixedPoint::Zero)
	{
		// 8 ring samples at 45° spacing. Computed once per move action via
		// SeinMath::Cos / SeinMath::Sin (~1µs total). Per-tick cost in
		// ResolveNavCollision becomes 9 IsPassable calls (center + ring) =
		// ~450ns per step attempt; with 3 step attempts worst case (full,
		// X-only, Y-only) ≈ 1.35µs per Tick per vehicle. Negligible.
		CachedNumFootprintSamples = 8;
		for (int32 i = 0; i < 8; ++i)
		{
			// Angle in radians: i × π/4. Covers 0° / 45° / 90° / ... / 315°.
			const FFixedPoint Angle = (FFixedPoint::Pi * FFixedPoint::FromInt(i)) / FFixedPoint::FromInt(4);
			CachedFootprintSamples[i].X = SeinMath::Cos(Angle) * Radius;
			CachedFootprintSamples[i].Y = SeinMath::Sin(Angle) * Radius;
			CachedFootprintSamples[i].Z = FFixedPoint::Zero;
		}
	}
	else
	{
		CachedNumFootprintSamples = 0;
	}
}

bool USeinMovement::IsOvershootArrival(
	const FFixedVector& AgentPos,
	const FFixedVector& FinalWp,
	const FFixedQuaternion& Rotation,
	FFixedPoint CurrentSpeed,
	FFixedPoint VicinityRadiusSq,
	FFixedPoint MaxSpeedForOvershoot)
{
	FFixedVector ToFinal = FinalWp - AgentPos;
	ToFinal.Z = FFixedPoint::Zero;
	if (ToFinal.SizeSquared() > VicinityRadiusSq) return false;

	const FFixedPoint AbsSpeed = (CurrentSpeed < FFixedPoint::Zero) ? -CurrentSpeed : CurrentSpeed;
	if (AbsSpeed > MaxSpeedForOvershoot) return false;

	// "Heading away" — forward · toFinal < 0. ToFinal is non-zero here only
	// if the unit is offset from FinalWp; degenerate-zero falls through to
	// the dot returning 0 and not triggering.
	const FFixedVector Forward = Rotation.RotateVector(FFixedVector::ForwardVector);
	const FFixedPoint Dot = Forward.X * ToFinal.X + Forward.Y * ToFinal.Y;
	return Dot < FFixedPoint::Zero;
}

bool USeinMovement::ShouldAutoReverse(
	const FFixedVector& AgentPos,
	const FFixedQuaternion& Rotation,
	const FFixedVector& FinalGoal,
	const FSeinMovementComponent& MovementData)
{
	if (!MovementData.bCanReverse) return false;

	FFixedVector ToGoal = FinalGoal - AgentPos;
	ToGoal.Z = FFixedPoint::Zero;
	const FFixedPoint DistSq = ToGoal.SizeSquared();
	const FFixedPoint MaxDistSq = MovementData.ReverseEngageDistanceThreshold * MovementData.ReverseEngageDistanceThreshold;
	if (DistSq > MaxDistSq) return false;
	if (DistSq <= FFixedPoint::Epsilon) return false; // already on goal

	// Compare normalized dot against threshold. Threshold is typically
	// negative (target is behind) — using <= so the boundary case engages.
	const FFixedVector ToGoalN = FFixedVector::GetSafeNormal(ToGoal);
	const FFixedVector Forward = Rotation.RotateVector(FFixedVector::ForwardVector);
	const FFixedPoint Dot = Forward.X * ToGoalN.X + Forward.Y * ToGoalN.Y;
	return Dot <= MovementData.ReverseEngageDotThreshold;
}


// ----------------------------------------------------------------------------
// Path planning
// ----------------------------------------------------------------------------

ESeinPathResult USeinMovement::PlanPath(const FSeinPlanPathContext& Ctx, FSeinPath& OutPath) const
{
	// Bypass branch — flying / hover movements consume a straight-line
	// [start, end] polyline directly. They fly over static obstacles and
	// don't benefit from A*, so we don't bother routing through the
	// pathfinder (and don't consume any path-request budget).
	if (BypassPathfinding())
	{
		OutPath.Clear();
		OutPath.Waypoints.Add(Ctx.Entity.Transform.GetLocation());
		OutPath.Waypoints.Add(Ctx.Destination);
		OutPath.bIsValid = true;
		OutPath.bIsPartial = false;
		// Derive the typed segment list. Two-waypoint straight-line path
		// → one Straight segment. Segment-aware consumers see a complete
		// path; legacy consumers continue reading Waypoints.
		OutPath.DeriveSegmentsFromWaypoints();
		return ESeinPathResult::Found;
	}

	// Ground branch — populate the nav request from NavData kinematics and
	// route through the budgeted subsystem call. Returning NoNavigation
	// rather than asserting lets the action turn it into a clean Fail()
	// (matches prior behavior where the action checked Nav/NavSub itself).
	if (!Ctx.NavSub)
	{
		return ESeinPathResult::NoNavigation;
	}

	FSeinPathRequest Req;
	Req.Start                = Ctx.Entity.Transform.GetLocation();
	Req.End                  = Ctx.Destination;
	Req.Requester            = Ctx.SelfHandle;
	if (Ctx.NavData)
	{
		Req.AgentNavLayerMask     = Ctx.NavData->NavLayerMask;
		Req.AgentWallPaddingCells = Ctx.NavData->WallPadding;
	}
	// Footprint via the shared cascade (Extents → NavComp → 0) — matches
	// what CacheFootprintFromContext uses for runtime collision, so the
	// path planner clears whatever the body actually occupies. Without
	// this, large vehicles with Extents components were getting stuck on
	// corners because A* planned for the (smaller) NavComp fallback
	// radius while the runtime body was the (larger) Extents radius.
	Req.AgentFootprintRadius = ResolveCollisionRadius(Ctx.World, Ctx.SelfHandle, Ctx.NavData);

	// Authoritative destination — ask the cover (or any) extension whether End is a
	// position that OVERRULES the coarse nav bake (a cover slot). When true, the
	// planner honors End as the exact final waypoint even on a partial path and
	// skips the wall-push on it (root CLAUDE.md #6). Unbound (cover absent / no-nav
	// games) → false, i.e. the default nearest-reachable behavior.
	Req.bAuthoritativeDestination = Ctx.World
		&& Ctx.World->AuthoritativeDestinationResolver.IsBound()
		&& Ctx.World->AuthoritativeDestinationResolver.Execute(Ctx.Destination);

	return Ctx.NavSub->RequestPath(Req, OutPath);
}

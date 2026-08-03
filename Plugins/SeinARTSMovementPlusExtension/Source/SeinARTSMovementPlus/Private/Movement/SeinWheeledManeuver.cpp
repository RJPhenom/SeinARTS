/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWheeledManeuver.cpp
 * @brief   Closed-form Reeds-Shepp-style start-maneuver planning for the
 *          wheeled vehicle mode. See the header for the live design contract;
 *          PlanStartManeuver below owns the canonical candidate ladder.
 */

#include "Movement/SeinWheeledManeuver.h"
#include "SeinNavigation.h"
#include "Math/MathLib.h"

namespace SeinWheeledManeuver
{

// ---------------------------------------------------------------------------
// Compile-time constants (determinism rule: never per-run tunables).
// Angles are radians; distances world units (cm).
// ---------------------------------------------------------------------------
namespace
{
	// Heading error to the path direction above which the planner engages.
	// Below it, pure pursuit arcs naturally without excessive braking.
	const FFixedPoint EngageAngle       = FFixedPoint::Pi * FFixedPoint::FromInt(100) / FFixedPoint::FromInt(180);
	// Residual heading error a forward join leg can hand to pure pursuit.
	const FFixedPoint JoinAngle         = FFixedPoint::Pi / FFixedPoint::FromInt(3);          // 60 deg
	const FFixedPoint JoinAngleSlack    = FFixedPoint::Pi / FFixedPoint::FromInt(9);          // +20 deg
	const FFixedPoint StraightJoinLimit = FFixedPoint::Pi * FFixedPoint::FromInt(75) / FFixedPoint::FromInt(180);
	// U-turn arcs sweeping close to a full circle are degenerate — reject.
	const FFixedPoint MaxUTurnSweep     = FFixedPoint::Pi * FFixedPoint::FromInt(240) / FFixedPoint::FromInt(180);
	// K-turn swing legs: probe granularity and per-leg cap.
	const FFixedPoint KTurnSweepStep    = FFixedPoint::Pi / FFixedPoint::FromInt(12);         // 15 deg
	const FFixedPoint KTurnLegMaxSweep  = FFixedPoint::Pi * FFixedPoint::FromInt(110) / FFixedPoint::FromInt(180);
	const FFixedPoint KTurnMinLegSweep  = FFixedPoint::Pi / FFixedPoint::FromInt(12);         // 15 deg
	// Tangent solve: target must sit this far outside the turn circle.
	const FFixedPoint TangentMargin     = FFixedPoint::FromInt(50);
	// Straight-reverse word gates.
	const FFixedPoint StraightReverseMaxTurn = FFixedPoint::Pi / FFixedPoint::FromInt(6);     // 30 deg total
	const int32 StraightReverseMaxWaypoints  = 6;
	// Reverse-out scan bounds.
	const int32 ReverseScanMaxWaypoints = 8;
	// Probe caps. 96 keeps footprint-spaced sampling honest out to a ~9.6 km
	// leg at the 100-unit floor — far beyond any candidate the ladder emits —
	// so the cap is a runaway backstop, not an effective spacing degradation.
	const int32 MaxProbeSamplesPerLeg   = 96;
	// Distances at/beyond this sentinel are "far" — returned by PlanarDist
	// instead of a wrapped FFixedPoint square (32.32 multiplication wraps
	// modulo 2^64 past ~46,340 units). Compared, never squared.
	const FFixedPoint FarComponentCap = FFixedPoint::FromInt(20000);
	const FFixedPoint FarDistSentinel = FFixedPoint::FromInt(100000);

	FFixedPoint AbsFP(FFixedPoint V) { return V < FFixedPoint::Zero ? -V : V; }
	FFixedPoint MinFP(FFixedPoint A, FFixedPoint B) { return A < B ? A : B; }
	FFixedPoint MaxFP(FFixedPoint A, FFixedPoint B) { return A > B ? A : B; }

	FFixedVector LeftNormal(FFixedPoint Yaw)
	{
		return FFixedVector(-SeinMath::Sin(Yaw), SeinMath::Cos(Yaw), FFixedPoint::Zero);
	}

	// Wrap-safe planar distance: FFixedPoint squares wrap past ~46,340 units,
	// so components at/beyond the far cap return a large sentinel instead of a
	// garbage Size(). Every caller only compares distances, so "far" is all
	// the fidelity needed out there.
	FFixedPoint PlanarDist(const FFixedVector& A, const FFixedVector& B)
	{
		FFixedVector D = B - A;
		D.Z = FFixedPoint::Zero;
		const FFixedPoint AX = D.X < FFixedPoint::Zero ? -D.X : D.X;
		const FFixedPoint AY = D.Y < FFixedPoint::Zero ? -D.Y : D.Y;
		if (AX >= FarComponentCap || AY >= FarComponentCap) return FarDistSentinel;
		return D.Size();
	}

	FFixedVector PointOnCircle(const FFixedVector& Center, FFixedPoint Radius, FFixedPoint Angle, FFixedPoint Z)
	{
		return FFixedVector(
			Center.X + Radius * SeinMath::Cos(Angle),
			Center.Y + Radius * SeinMath::Sin(Angle),
			Z);
	}

	// Probe spacing along a candidate leg: footprint-sized steps, floored so
	// tiny footprints don't explode the sample count.
	FFixedPoint ProbeSpacing(const FInputs& In)
	{
		return MaxFP(In.FootprintRadius, FFixedPoint::FromInt(100));
	}
}

FFixedPoint WrapSigned(FFixedPoint A)
{
	// Inputs are sums of a handful of Atan2 results — bounded, so the loop wrap
	// is a couple of iterations at most and fully deterministic.
	const FFixedPoint TwoPi = FFixedPoint::Pi * FFixedPoint::Two;
	while (A > FFixedPoint::Pi)  A -= TwoPi;
	while (A < -FFixedPoint::Pi) A += TwoPi;
	return A;
}

FFixedPoint WrapPositive(FFixedPoint A)
{
	const FFixedPoint TwoPi = FFixedPoint::Pi * FFixedPoint::Two;
	while (A < FFixedPoint::Zero) A += TwoPi;
	while (A >= TwoPi)            A -= TwoPi;
	return A;
}

bool ProbeClearAt(const FInputs& In, const FFixedVector& Pos)
{
	if (!In.Nav) return true;
	if (!In.Nav->IsWorldPositionClearForAgent(Pos, In.Agent)) return false;
	if (In.FootprintRadius <= FFixedPoint::Zero) return true;
	// 4-ring at the footprint radius — go/no-go for a plan candidate; the
	// runtime nav floor still owns the exact 8-ring enforcement.
	const FFixedPoint R = In.FootprintRadius;
	const FFixedVector Ring[4] = {
		FFixedVector(Pos.X + R, Pos.Y, Pos.Z),
		FFixedVector(Pos.X - R, Pos.Y, Pos.Z),
		FFixedVector(Pos.X, Pos.Y + R, Pos.Z),
		FFixedVector(Pos.X, Pos.Y - R, Pos.Z) };
	for (const FFixedVector& Sample : Ring)
	{
		if (!In.Nav->IsWorldPositionClearForAgent(Sample, In.Agent)) return false;
	}
	return true;
}

bool ProbeStraightClear(const FInputs& In, const FFixedVector& From, const FFixedVector& To)
{
	const FFixedPoint Len = PlanarDist(From, To);
	if (Len <= FFixedPoint::Epsilon) return true;
	int32 Steps = (Len / ProbeSpacing(In)).CeilToInt();
	if (Steps < 1) Steps = 1;
	if (Steps > MaxProbeSamplesPerLeg) Steps = MaxProbeSamplesPerLeg;
	const FFixedPoint StepsFP = FFixedPoint::FromInt(Steps);
	for (int32 i = 1; i <= Steps; ++i)
	{
		const FFixedPoint T = FFixedPoint::FromInt(i) / StepsFP;
		const FFixedVector P(
			From.X + (To.X - From.X) * T,
			From.Y + (To.Y - From.Y) * T,
			From.Z);
		if (!ProbeClearAt(In, P)) return false;
	}
	return true;
}

bool ProbeArcClear(const FInputs& In, const FFixedVector& Center, FFixedPoint Radius,
	FFixedPoint StartAngle, FFixedPoint Sweep)
{
	const FFixedPoint ArcLen = Radius * AbsFP(Sweep);
	if (ArcLen <= FFixedPoint::Epsilon) return true;
	int32 Steps = (ArcLen / ProbeSpacing(In)).CeilToInt();
	if (Steps < 1) Steps = 1;
	if (Steps > MaxProbeSamplesPerLeg) Steps = MaxProbeSamplesPerLeg;
	const FFixedPoint StepsFP = FFixedPoint::FromInt(Steps);
	for (int32 i = 1; i <= Steps; ++i)
	{
		const FFixedPoint Angle = StartAngle + Sweep * FFixedPoint::FromInt(i) / StepsFP;
		if (!ProbeClearAt(In, PointOnCircle(Center, Radius, Angle, Center.Z))) return false;
	}
	return true;
}

FFixedPoint ProbeArcMaxSweep(const FInputs& In, const FFixedVector& Center, FFixedPoint Radius,
	FFixedPoint StartAngle, FFixedPoint SweepSign, FFixedPoint MaxSweep)
{
	// Each 15-degree scan step probes its whole SLICE at the footprint spacing
	// (via ProbeArcClear), not just its endpoint — swing legs meet the same
	// sampling contract as every other leg.
	FFixedPoint Clear = FFixedPoint::Zero;
	FFixedPoint Sweep = KTurnSweepStep;
	while (Sweep <= MaxSweep)
	{
		const FFixedPoint SliceStart = StartAngle + SweepSign * (Sweep - KTurnSweepStep);
		if (!ProbeArcClear(In, Center, Radius, SliceStart, SweepSign * KTurnSweepStep)) break;
		Clear = Sweep;
		Sweep += KTurnSweepStep;
	}
	return Clear;
}

bool SolveTangentArc(const FFixedVector& Pos, FFixedPoint Yaw, FFixedPoint TurnSign,
	FFixedPoint Radius, const FFixedVector& Target,
	FFixedVector& OutCenter, FFixedVector& OutDepart, FFixedPoint& OutSweep)
{
	// Turn-circle center sits perpendicular to the heading on the turn side.
	const FFixedVector Left = LeftNormal(Yaw);
	FFixedVector C(
		Pos.X + Left.X * TurnSign * Radius,
		Pos.Y + Left.Y * TurnSign * Radius,
		Pos.Z);

	FFixedVector ToT = Target - C;
	ToT.Z = FFixedPoint::Zero;
	const FFixedPoint D = ToT.Size();
	if (D <= Radius + TangentMargin) return false; // target inside/near the circle — cusp territory

	// Tangency: cos(Alpha - PhiT) = R/D with the travel-direction branch
	// PhiT = Alpha - TurnSign * acos(R/D) (derivation: the tangent direction at
	// PhiT must point at the target for the chosen handedness).
	const FFixedPoint Alpha = SeinMath::Atan2(ToT.Y, ToT.X);
	FFixedPoint Ratio = Radius / D;
	if (Ratio > FFixedPoint::One) Ratio = FFixedPoint::One;
	const FFixedPoint PhiT = Alpha - TurnSign * SeinMath::Acos(Ratio);
	const FFixedPoint Phi0 = SeinMath::Atan2(Pos.Y - C.Y, Pos.X - C.X);

	const FFixedPoint SweepMag = WrapPositive(TurnSign * (PhiT - Phi0));
	OutCenter = C;
	OutDepart = PointOnCircle(C, Radius, PhiT, Pos.Z);
	OutSweep = TurnSign * SweepMag;
	return true;
}

FFixedPoint PolylineLengthFrom(const TArray<FFixedVector>& Waypoints, int32 FromIndex)
{
	FFixedPoint Len = FFixedPoint::Zero;
	for (int32 i = (FromIndex > 0 ? FromIndex : 0); i + 1 < Waypoints.Num(); ++i)
	{
		Len += PlanarDist(Waypoints[i], Waypoints[i + 1]);
	}
	return Len;
}

namespace
{

FLeg MakeStraightLeg(const FFixedVector& From, const FFixedVector& To, bool bReverse)
{
	FLeg Leg;
	Leg.bArc = false;
	Leg.bReverse = bReverse;
	Leg.From = From;
	Leg.To = To;
	Leg.Length = PlanarDist(From, To);
	return Leg;
}

FLeg MakeArcLeg(const FFixedVector& From, const FFixedVector& To, const FFixedVector& Center,
	FFixedPoint Radius, FFixedPoint Sweep, bool bReverse)
{
	FLeg Leg;
	Leg.bArc = true;
	Leg.bReverse = bReverse;
	Leg.From = From;
	Leg.To = To;
	Leg.Center = Center;
	Leg.Radius = Radius;
	Leg.Sweep = Sweep;
	Leg.Length = Radius * AbsFP(Sweep);
	return Leg;
}

struct FCandidate
{
	FPlan Plan;
	FFixedPoint EffectiveCost = FFixedPoint::Zero; // head + tail, bias-adjusted
	bool bStartsReverse = false;
	bool bValid = false;
};

/** Append reverse straight legs From `StartPos` through Waypoints[0..LastIndex],
 *  preserving the byte-exact From==To chain: sub-unit hops are ABSORBED into
 *  the next leg (the cursor doesn't advance past them), and the terminal point
 *  is guaranteed to be the exact last emitted To — a trailing micro-hop
 *  EXTENDS the previous leg instead of being dropped, so the chain never
 *  loses the destination. Returns false when nothing meaningful was emitted. */
bool AppendReverseStraights(FPlan& Plan, const FFixedVector& StartPos,
	const TArray<FFixedVector>& Waypoints, int32 LastIndex)
{
	FFixedVector Prev = StartPos;
	for (int32 i = 0; i <= LastIndex && i < Waypoints.Num(); ++i)
	{
		const FFixedVector& W = Waypoints[i];
		if (PlanarDist(Prev, W) > FFixedPoint::One)
		{
			Plan.Legs.Add(MakeStraightLeg(Prev, W, true));
			Prev = W;
		}
		else if (i == LastIndex && Plan.Legs.Num() > 0)
		{
			// Trailing micro-hop: extend the previous leg to the exact terminal.
			FLeg& Last = Plan.Legs.Last();
			Last.To = W;
			Last.Length = PlanarDist(Last.From, W);
			Prev = W;
		}
		// Mid-chain micro-hop: skip WITHOUT advancing Prev — absorbed into the
		// next leg, keeping every emitted From equal to the previous To.
	}
	return Plan.Legs.Num() > 0;
}

FFixedPoint LegCost(const FLeg& Leg, FFixedPoint ReversePenalty)
{
	return Leg.bReverse ? Leg.Length * ReversePenalty : Leg.Length;
}

FFixedPoint PlanHeadCost(const FPlan& Plan, FFixedPoint ReversePenalty)
{
	FFixedPoint Cost = FFixedPoint::Zero;
	for (const FLeg& Leg : Plan.Legs) Cost += LegCost(Leg, ReversePenalty);
	return Cost;
}

/** First polyline index whose cumulative distance from `Pos` (via the
 *  preceding waypoints, starting at `StartIndex`) reaches `LookAhead`;
 *  falls back to the last index. */
int32 FindJoinIndex(const FFixedVector& Pos, const TArray<FFixedVector>& Waypoints,
	int32 StartIndex, FFixedPoint LookAhead)
{
	FFixedPoint Cum = FFixedPoint::Zero;
	FFixedVector Prev = Pos;
	for (int32 i = StartIndex; i < Waypoints.Num(); ++i)
	{
		Cum += PlanarDist(Prev, Waypoints[i]);
		if (Cum >= LookAhead) return i;
		Prev = Waypoints[i];
	}
	return Waypoints.Num() - 1;
}

/** Candidate A — forward U-turn arc + tangent straight to the join waypoint.
 *  Tries the largest (cruise) radius first: open ground gets the full-speed
 *  swoop, and only confined ground shrinks toward R_min (which the driver
 *  brakes for via the arc speed law). */
FCandidate BuildUTurn(const FInputs& In, const TArray<FFixedVector>& Waypoints,
	int32 JoinIdx, const FFixedVector& J, FFixedPoint TurnSign)
{
	FCandidate Out;
	const FFixedPoint RMid = (In.CruiseTurnRadius + In.MinTurnRadius) * FFixedPoint::Half;
	const FFixedPoint Radii[3] = { In.CruiseTurnRadius, RMid, In.MinTurnRadius };
	FFixedPoint PrevR = FFixedPoint::Zero;
	for (int32 r = 0; r < 3; ++r)
	{
		const FFixedPoint R = Radii[r];
		if (R <= FFixedPoint::Zero) continue;
		if (r > 0 && R >= PrevR) continue; // dedupe when cruise == min
		PrevR = R;

		FFixedVector Center, Depart;
		FFixedPoint Sweep = FFixedPoint::Zero;
		if (!SolveTangentArc(In.Pos, In.Yaw, TurnSign, R, J, Center, Depart, Sweep)) continue;
		if (AbsFP(Sweep) > MaxUTurnSweep) continue;

		const FFixedPoint Phi0 = SeinMath::Atan2(In.Pos.Y - Center.Y, In.Pos.X - Center.X);
		if (!ProbeArcClear(In, Center, R, Phi0, Sweep)) continue;
		if (!ProbeStraightClear(In, Depart, J)) continue;

		Out.Plan.Legs.Add(MakeArcLeg(In.Pos, Depart, Center, R, Sweep, false));
		Out.Plan.Legs.Add(MakeStraightLeg(Depart, J, false));
		Out.Plan.JoinWaypointIndex = JoinIdx;
		Out.Plan.HeadCost = PlanHeadCost(Out.Plan, In.ReverseSpeedPenalty);
		Out.bValid = true;
		break; // largest feasible radius wins — full-speed-arc preference
	}
	if (Out.bValid)
	{
		const FFixedPoint Total = Out.Plan.HeadCost + PolylineLengthFrom(Waypoints, JoinIdx);
		Out.EffectiveCost = (In.ForwardPathBias > FFixedPoint::One)
			? Total / In.ForwardPathBias   // forward-only candidates get the bias advantage
			: Total;
	}
	return Out;
}

/** Candidate B — drive the whole (short, near-straight) path in reverse.
 *  The segment-native replacement for the old one-shot auto-reverse latch. */
FCandidate BuildStraightReverse(const FInputs& In, const TArray<FFixedVector>& Waypoints)
{
	FCandidate Out;
	const int32 N = Waypoints.Num();
	if (!In.bCanReverse || N == 0 || N > StraightReverseMaxWaypoints) return Out;

	FFixedVector ToFinal = Waypoints[N - 1] - In.Pos;
	ToFinal.Z = FFixedPoint::Zero;
	const FFixedPoint Dist = ToFinal.Size();
	if (Dist <= FFixedPoint::Epsilon || Dist > In.ReverseEngageDistance) return Out;

	const FFixedVector Fwd(SeinMath::Cos(In.Yaw), SeinMath::Sin(In.Yaw), FFixedPoint::Zero);
	const FFixedVector ToFinalN = FFixedVector::GetSafeNormal(ToFinal);
	if (Fwd.X * ToFinalN.X + Fwd.Y * ToFinalN.Y > In.ReverseEngageDot) return Out;

	// Near-straight gate: total direction change along Pos→W0→…→Wlast.
	FFixedPoint TotalTurn = FFixedPoint::Zero;
	FFixedVector Prev = In.Pos;
	FFixedPoint PrevDir = FFixedPoint::Zero;
	bool bHavePrevDir = false;
	for (int32 i = 0; i < N; ++i)
	{
		FFixedVector Seg = Waypoints[i] - Prev;
		Seg.Z = FFixedPoint::Zero;
		if (Seg.SizeSquared() > FFixedPoint::Epsilon)
		{
			const FFixedPoint Dir = SeinMath::Atan2(Seg.Y, Seg.X);
			if (bHavePrevDir) TotalTurn += AbsFP(WrapSigned(Dir - PrevDir));
			PrevDir = Dir;
			bHavePrevDir = true;
		}
		Prev = Waypoints[i];
	}
	if (TotalTurn > StraightReverseMaxTurn) return Out;

	// The polyline itself is A*-valid; only the Pos→W0 stub is new geometry.
	if (!ProbeStraightClear(In, In.Pos, Waypoints[0])) return Out;

	if (!AppendReverseStraights(Out.Plan, In.Pos, Waypoints, N - 1)) return Out;
	Out.Plan.JoinWaypointIndex = -1;
	Out.Plan.HeadCost = PlanHeadCost(Out.Plan, In.ReverseSpeedPenalty);
	Out.EffectiveCost = Out.Plan.HeadCost;
	Out.bStartsReverse = true;
	Out.bValid = true;
	return Out;
}

/** Candidate C — multi-point (K-) turn: two alternating min-radius swing arcs
 *  (fwd/rev or rev/fwd) that rotate the heading toward the join direction,
 *  then a tangent-arc or straight join. Swing sweeps are probe-limited, so the
 *  turn uses exactly as much room as the walls allow. */
FCandidate BuildKTurn(const FInputs& In, const TArray<FFixedVector>& Waypoints,
	int32 JoinIdx, const FFixedVector& J, FFixedPoint TurnSign, FFixedPoint AbsErr,
	bool bStartReverse)
{
	FCandidate Out;
	if (!In.bCanReverse) return Out;
	const FFixedPoint R = In.MinTurnRadius;

	FFixedVector Pose = In.Pos;
	FFixedPoint PoseYaw = In.Yaw;
	FFixedPoint Need = AbsErr - JoinAngle;
	bool bLegReverse = bStartReverse;

	for (int32 LegIdx = 0; LegIdx < 2 && Need > FFixedPoint::Zero; ++LegIdx)
	{
		// Swing-arc center: turn side for a forward leg, opposite side for a
		// reverse leg (backing with opposite steer continues the same yaw
		// rotation — the K-turn identity).
		const FFixedVector Left = LeftNormal(PoseYaw);
		const FFixedPoint CenterSign = bLegReverse ? -TurnSign : TurnSign;
		const FFixedVector Center(
			Pose.X + Left.X * CenterSign * R,
			Pose.Y + Left.Y * CenterSign * R,
			Pose.Z);
		const FFixedPoint Phi0 = SeinMath::Atan2(Pose.Y - Center.Y, Pose.X - Center.X);
		// The position angle around the center always advances in the SAME
		// direction as the yaw (theta = phi + CenterSign*pi/2, so dphi = dtheta)
		// — for a forward leg AND for a reverse leg with the opposite-side
		// center. Both leg flavors rotate yaw by +TurnSign here.
		const FFixedPoint PosSweepSign = TurnSign;

		const FFixedPoint MaxClear = ProbeArcMaxSweep(In, Center, R, Phi0, PosSweepSign, KTurnLegMaxSweep);
		FFixedPoint SweepMag = MinFP(MaxClear, MaxFP(Need, KTurnSweepStep * FFixedPoint::Two));
		if (SweepMag < KTurnMinLegSweep) return FCandidate(); // no room to swing this leg

		const FFixedPoint PhiEnd = Phi0 + PosSweepSign * SweepMag;
		const FFixedVector LegEnd = PointOnCircle(Center, R, PhiEnd, Pose.Z);
		Out.Plan.Legs.Add(MakeArcLeg(Pose, LegEnd, Center, R, PosSweepSign * SweepMag, bLegReverse));

		Pose = LegEnd;
		PoseYaw = WrapSigned(PoseYaw + TurnSign * SweepMag);
		Need -= SweepMag;
		bLegReverse = !bLegReverse;
	}
	if (Out.Plan.Legs.Num() == 0) return FCandidate();

	// Join: residual heading error to the join waypoint must be small enough
	// for a tangent arc or a pure-pursuit-absorbable straight.
	FFixedVector ToJ = J - Pose;
	ToJ.Z = FFixedPoint::Zero;
	if (ToJ.SizeSquared() <= FFixedPoint::Epsilon) return FCandidate();
	const FFixedPoint ErrJ = WrapSigned(SeinMath::Atan2(ToJ.Y, ToJ.X) - PoseYaw);
	if (AbsFP(ErrJ) > JoinAngle + JoinAngleSlack) return FCandidate();

	const FFixedPoint JoinSign = (ErrJ >= FFixedPoint::Zero) ? FFixedPoint::One : -FFixedPoint::One;
	FFixedVector JC, JT;
	FFixedPoint JSweep = FFixedPoint::Zero;
	bool bJoined = false;
	if (SolveTangentArc(Pose, PoseYaw, JoinSign, R, J, JC, JT, JSweep)
		&& AbsFP(JSweep) <= MaxUTurnSweep)
	{
		const FFixedPoint JPhi0 = SeinMath::Atan2(Pose.Y - JC.Y, Pose.X - JC.X);
		if (ProbeArcClear(In, JC, R, JPhi0, JSweep) && ProbeStraightClear(In, JT, J))
		{
			Out.Plan.Legs.Add(MakeArcLeg(Pose, JT, JC, R, JSweep, false));
			Out.Plan.Legs.Add(MakeStraightLeg(JT, J, false));
			bJoined = true;
		}
	}
	if (!bJoined && AbsFP(ErrJ) <= StraightJoinLimit && ProbeStraightClear(In, Pose, J))
	{
		Out.Plan.Legs.Add(MakeStraightLeg(Pose, J, false));
		bJoined = true;
	}
	if (!bJoined) return FCandidate();

	Out.Plan.JoinWaypointIndex = JoinIdx;
	Out.Plan.HeadCost = PlanHeadCost(Out.Plan, In.ReverseSpeedPenalty);
	Out.EffectiveCost = Out.Plan.HeadCost + PolylineLengthFrom(Waypoints, JoinIdx);
	Out.bStartsReverse = bStartReverse;
	Out.bValid = true;
	return Out;
}

/** Candidate D — reverse ALONG the path until a probed turnaround pocket, cusp
 *  there, U-turn forward, rejoin. The corridor-escape word: the vehicle backs
 *  down its own route to the first place it can physically turn around. */
FCandidate BuildReverseOut(const FInputs& In, const TArray<FFixedVector>& Waypoints)
{
	FCandidate Out;
	const int32 N = Waypoints.Num();
	if (!In.bCanReverse || N == 0) return Out;

	const FFixedPoint MinRevBeforeTurn = MaxFP(
		In.FootprintRadius * FFixedPoint::Two + FFixedPoint::FromInt(100),
		FFixedPoint::FromInt(300));
	const FFixedPoint TurnPocketRadius = In.MinTurnRadius * FFixedPoint::Two;

	// The Pos→W0 stub is the only NEW reverse geometry (the polyline itself is
	// A*-valid) — probe it BEFORE any pocket can be accepted, mirroring
	// BuildStraightReverse's ordering.
	if (Waypoints.Num() > 0 && !ProbeStraightClear(In, In.Pos, Waypoints[0])) return Out;

	FFixedPoint Cum = FFixedPoint::Zero;
	FFixedVector Prev = In.Pos;
	const int32 ScanMax = (N < ReverseScanMaxWaypoints) ? N : ReverseScanMaxWaypoints;
	for (int32 k = 0; k < ScanMax; ++k)
	{
		const FFixedVector Wk = Waypoints[k];
		Cum += PlanarDist(Prev, Wk);
		if (In.ReversePlanMaxDistance > FFixedPoint::Zero && Cum > In.ReversePlanMaxDistance) break;
		if (Cum >= MinRevBeforeTurn)
		{
			// Turnaround pocket: the U-turn disc around Wk must be clear.
			bool bFits = ProbeClearAt(In, Wk);
			for (int32 s = 0; bFits && s < 8; ++s)
			{
				const FFixedPoint Angle = FFixedPoint::Pi * FFixedPoint::FromInt(s) / FFixedPoint::FromInt(4);
				bFits = ProbeClearAt(In, PointOnCircle(Wk, TurnPocketRadius, Angle, Wk.Z));
			}
			if (bFits)
			{
				// Predicted cusp pose: nose opposite the reverse travel direction.
				FFixedVector Travel = Wk - Prev;
				Travel.Z = FFixedPoint::Zero;
				if (Travel.SizeSquared() <= FFixedPoint::Epsilon) { Prev = Wk; continue; }
				const FFixedPoint CuspYaw = SeinMath::Atan2(-Travel.Y, -Travel.X);

				const int32 JoinStart = (k + 1 < N - 1) ? (k + 1) : (N - 1);
				const int32 JoinIdx2 = FindJoinIndex(Wk, Waypoints, JoinStart,
					MaxFP(In.MinTurnRadius * FFixedPoint::Two, FFixedPoint::FromInt(400)));
				const FFixedVector J2 = Waypoints[JoinIdx2];
				FFixedVector ToJ2 = J2 - Wk;
				ToJ2.Z = FFixedPoint::Zero;
				if (ToJ2.SizeSquared() <= FFixedPoint::Epsilon) { Prev = Wk; continue; }
				const FFixedPoint Err2 = WrapSigned(SeinMath::Atan2(ToJ2.Y, ToJ2.X) - CuspYaw);
				const FFixedPoint Sign2 = (Err2 >= FFixedPoint::Zero) ? FFixedPoint::One : -FFixedPoint::One;

				FFixedVector C2, T2;
				FFixedPoint Sweep2 = FFixedPoint::Zero;
				if (!SolveTangentArc(Wk, CuspYaw, Sign2, In.MinTurnRadius, J2, C2, T2, Sweep2)) { Prev = Wk; continue; }
				if (AbsFP(Sweep2) > MaxUTurnSweep) { Prev = Wk; continue; }
				const FFixedPoint Phi02 = SeinMath::Atan2(Wk.Y - C2.Y, Wk.X - C2.X);
				if (!ProbeArcClear(In, C2, In.MinTurnRadius, Phi02, Sweep2)) { Prev = Wk; continue; }
				if (!ProbeStraightClear(In, T2, J2)) { Prev = Wk; continue; }

				// Legs: reverse straights along the path (terminal-exact at Wk —
				// the U-turn arc's From), then the forward U-turn.
				if (!AppendReverseStraights(Out.Plan, In.Pos, Waypoints, k)) break;
				Out.Plan.Legs.Add(MakeArcLeg(Wk, T2, C2, In.MinTurnRadius, Sweep2, false));
				Out.Plan.Legs.Add(MakeStraightLeg(T2, J2, false));
				Out.Plan.JoinWaypointIndex = JoinIdx2;
				Out.Plan.HeadCost = PlanHeadCost(Out.Plan, In.ReverseSpeedPenalty);
				Out.EffectiveCost = Out.Plan.HeadCost + PolylineLengthFrom(Waypoints, JoinIdx2);
				Out.bStartsReverse = true;
				Out.bValid = true;
				return Out;
			}
		}
		// The polyline between probed waypoints is A*-valid; only Pos→W0 is new.
		if (k == 0 && !ProbeStraightClear(In, In.Pos, Waypoints[0])) break;
		Prev = Wk;
	}
	return Out;
}

} // anonymous namespace

bool PlanStartManeuver(const FInputs& In, const TArray<FFixedVector>& Waypoints, FPlan& Out)
{
	const int32 N = Waypoints.Num();
	if (N == 0 || In.MinTurnRadius <= FFixedPoint::Zero) return false;

	// Join target: far enough along the polyline that the tangent solve isn't
	// chasing a point right under the chassis.
	const int32 JoinIdx = FindJoinIndex(In.Pos, Waypoints, 0,
		MaxFP(In.CruiseTurnRadius * FFixedPoint::Two, FFixedPoint::FromInt(600)));
	const FFixedVector J = Waypoints[JoinIdx];
	FFixedVector ToJ = J - In.Pos;
	ToJ.Z = FFixedPoint::Zero;
	if (ToJ.SizeSquared() <= FFixedPoint::Epsilon) return false;

	const FFixedPoint PathDir = SeinMath::Atan2(ToJ.Y, ToJ.X);
	const FFixedPoint Err = WrapSigned(PathDir - In.Yaw);
	const FFixedPoint AbsErr = (Err < FFixedPoint::Zero) ? -Err : Err;
	const FFixedPoint Engage = (In.EngageAngle > FFixedPoint::Zero) ? In.EngageAngle : EngageAngle;
	if (AbsErr < Engage) return false;
	const FFixedPoint TurnSign = (Err >= FFixedPoint::Zero) ? FFixedPoint::One : -FFixedPoint::One;

	// Candidate ladder — fixed order, deterministic.
	FCandidate Candidates[5];
	Candidates[0] = BuildUTurn(In, Waypoints, JoinIdx, J, TurnSign);
	Candidates[1] = BuildStraightReverse(In, Waypoints);
	Candidates[2] = BuildKTurn(In, Waypoints, JoinIdx, J, TurnSign, AbsErr, /*bStartReverse=*/false);
	Candidates[3] = BuildKTurn(In, Waypoints, JoinIdx, J, TurnSign, AbsErr, /*bStartReverse=*/true);
	if (!Candidates[0].bValid)
	{
		Candidates[4] = BuildReverseOut(In, Waypoints);
	}

	// Replan continuity: mid-reverse, prefer candidates that keep reversing so
	// interval repaths don't truncate an in-progress reverse leg into shuffle.
	bool bAnyReverseStart = false;
	if (In.bCurrentlyReversing)
	{
		for (const FCandidate& C : Candidates) { bAnyReverseStart |= (C.bValid && C.bStartsReverse); }
	}

	const FCandidate* Best = nullptr;
	for (const FCandidate& C : Candidates)
	{
		if (!C.bValid) continue;
		if (bAnyReverseStart && !C.bStartsReverse) continue;
		if (!Best || C.EffectiveCost < Best->EffectiveCost) Best = &C;
	}
	if (!Best) return false;

	Out = Best->Plan;
	return true;
}

} // namespace SeinWheeledManeuver

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
#include "Movement/SeinMoverHandle.h"
#include "Movement/SeinPlannerHandle.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/UnrealType.h"

#if UE_ENABLE_DEBUG_DRAWING
#include "Debug/SeinDebugDrawCull.h"
#include "DrawDebugHelpers.h"
#endif

// ======================================================================================
// Steering seam (two-tier). The base Tick(Ctx) is the shared MECHANISM HARNESS: waypoint advance →
// arrival (acceptance ring OR IsOvershootArrival) → the mode's ComputeMotion policy (desired
// velocity + facing) → translate + nav-collision floor + ground snap + TurnRate-clamped turn +
// slope tilt + velocity persist. Tier-1 modes (Basic / BasicUnit / Infantry, and BP-authored modes)
// override ComputeMotion only. Tier-2 modes (the Movement+ vehicles) override Tick(Ctx) directly for
// full control and never reach the harness or ComputeMotion.
// ======================================================================================

bool USeinMovement::Tick(const FSeinMovementContext& Ctx)
{
	if (!Ctx.MovementData) return true;

	FSeinEntity& Entity = Ctx.Entity;
	FSeinMovementComponent& MovementData = *Ctx.MovementData;
	const FSeinPath& Path = Ctx.Path;
	int32& CurrentWaypointIndex = Ctx.CurrentWaypointIndex;
	const FFixedPoint DeltaTime = Ctx.DeltaTime;
	USeinNavigation* Nav = Ctx.Nav;

	const int32 N = Path.Waypoints.Num();
	if (N == 0) return true;

	const FFixedVector PrePos  = Entity.Transform.GetLocation();
	const FFixedVector FinalWp = Path.Waypoints[N - 1];

	// MECHANISM: advance past any waypoint the unit has crossed/reached (dot-product crossover +
	// distance fallback), so the policy always steers at a waypoint that is ahead of it.
	{
		const FFixedPoint OneStep = MovementData.TopSpeed * DeltaTime;
		const FFixedPoint CloseRadius = (OneStep * FFixedPoint::Two > FFixedPoint::FromInt(50))
			? OneStep * FFixedPoint::Two : FFixedPoint::FromInt(50);
		AdvanceWaypointAlongPath(CurrentWaypointIndex, Path, PrePos, CloseRadius);
	}

	// MECHANISM: arrival. Within the acceptance ring, OR an overshoot (close + slow + heading away)
	// — the graceful-stop guard that stops a unit orbiting a slot it can't quite land on.
	{
		FFixedVector ToFinal = FinalWp - PrePos;
		ToFinal.Z = FFixedPoint::Zero;
		const bool bWithinAcceptance = ToFinal.SizeSquared() <= Ctx.AcceptanceRadiusSq;
		const FFixedPoint EntrySpeed        = MovementData.Velocity.Size();
		const FFixedPoint VicinityRadiusSq  = Ctx.AcceptanceRadiusSq * FFixedPoint::FromInt(4);
		const FFixedPoint OvershootSpeedCap = MovementData.TopSpeed / FFixedPoint::FromInt(3);
		if (bWithinAcceptance || IsOvershootArrival(PrePos, FinalWp, Entity.Transform.Rotation,
				EntrySpeed, VicinityRadiusSq, OvershootSpeedCap))
		{
			MovementData.Velocity = FFixedVector::ZeroVector;
			return true;
		}
	}

	// POLICY: the mode decides desired velocity + facing this tick.
	if (!CachedHandle) { CachedHandle = NewObject<USeinMoverHandle>(this); }
	CachedHandle->SetContext(&Ctx);
	const FSeinMotion Motion = ComputeMotion(CachedHandle);
	CachedHandle->SetContext(nullptr);  // never let the borrowed context escape the dispatch

	// MECHANISM: translate by the desired velocity, clamped so we don't overshoot the current
	// waypoint within one tick; then the hard nav-collision floor (respecting an authoritative
	// cover-slot destination) and ground snap.
	FFixedVector NewPos = PrePos;
	{
		FFixedVector Step(Motion.Velocity.X * DeltaTime, Motion.Velocity.Y * DeltaTime, FFixedPoint::Zero);
		FFixedVector ToWp = Path.Waypoints[CurrentWaypointIndex] - PrePos;
		ToWp.Z = FFixedPoint::Zero;
		const FFixedPoint DistWp  = ToWp.Size();
		const FFixedPoint StepLen = Step.Size();
		if (StepLen > DistWp && StepLen > FFixedPoint::Epsilon)
		{
			const FFixedPoint Scale = DistWp / StepLen;
			Step.X = Step.X * Scale;
			Step.Y = Step.Y * Scale;
		}
		NewPos.X = PrePos.X + Step.X;
		NewPos.Y = PrePos.Y + Step.Y;
		NewPos = ResolveNavCollision(PrePos, NewPos, Nav,
			Ctx.bAuthoritativeDestination ? &FinalWp : nullptr);
	}
	ApplyGroundSnapAndAltitude(NewPos, Ctx.MovementData, Nav, DeltaTime);
	Entity.Transform.SetLocation(NewPos);

	// MECHANISM: facing. Turn current yaw toward the policy's TargetYaw at TurnRate, then tilt to the
	// ground slope (ground modes) — or a flat yaw-only rotation when the mode doesn't snap to ground.
	// A mode that holds facing (Basic) sets bUpdateFacing = false and this is skipped.
	if (Motion.bUpdateFacing)
	{
		const FFixedPoint CurrentYaw = YawFromRotation(Entity.Transform.Rotation);
		const FFixedPoint YawDelta   = ShortestAngleDelta(CurrentYaw, Motion.TargetYaw);
		const FFixedPoint MaxTurn    = MovementData.TurnRate * DeltaTime;
		const FFixedPoint FinalYaw   = CurrentYaw + ClampFP(YawDelta, -MaxTurn, MaxTurn);
		Entity.Transform.Rotation = bSnapsToGround
			? ApplySlopeTilt(NewPos, FinalYaw, &MovementData, Nav, DeltaTime)
			: YawOnly(FinalYaw);
	}

	// MECHANISM: persist velocity from the ACTUAL post-collision moved delta (honest momentum — a
	// wall-pinned unit reports ~zero, so avoidance neighbours + anim graphs see the truth).
	FFixedVector MoveDelta = NewPos - PrePos;
	MoveDelta.Z = FFixedPoint::Zero;
	MovementData.Velocity = (DeltaTime > FFixedPoint::Zero)
		? FFixedVector(MoveDelta.X / DeltaTime, MoveDelta.Y / DeltaTime, FFixedPoint::Zero)
		: FFixedVector::ZeroVector;

	return false;
}

FSeinMotion USeinMovement::ComputeMotion_Implementation(USeinMoverHandle* Mover)
{
	// Default policy = ultra-basic ground mover: head to the current waypoint at terrain-scaled top
	// speed (bent by local avoidance, scaled by the avoidance speed-yield) and face the travel
	// direction. USeinBasicUnitMovement is exactly this; USeinBasicMovement drops the facing.
	FSeinMotion Motion;
	const FSeinMovementContext* C = Mover ? Mover->GetContext() : nullptr;
	if (!C || !C->MovementData) return Motion;
	const FSeinMovementContext& Ctx = *C;

	const int32 N = Ctx.Path.Waypoints.Num();
	if (N == 0 || Ctx.CurrentWaypointIndex >= N) return Motion;

	FFixedVector ToWp = Ctx.Path.Waypoints[Ctx.CurrentWaypointIndex] - Ctx.Entity.Transform.GetLocation();
	ToWp.Z = FFixedPoint::Zero;
	if (ToWp.SizeSquared() <= FFixedPoint::Epsilon) return Motion;

	const FFixedVector Dir   = ApplyAvoidanceSteer(Ctx, FFixedVector::GetSafeNormal(ToWp));
	const FFixedPoint  Speed = EffectiveTopSpeed(Ctx) * GetAvoidanceSpeedScale(Ctx);
	Motion.Velocity     = FFixedVector(Dir.X * Speed, Dir.Y * Speed, FFixedPoint::Zero);
	Motion.TargetYaw    = SeinMath::Atan2(Dir.Y, Dir.X);
	Motion.bUpdateFacing = true;
	return Motion;
}

void USeinMovement::OnMoveBegin(const FSeinMovementContext& Ctx)
{
	// Per-unit tuning hydrate: copy MovementClassData into matching instance vars so a
	// BP mode's graph reads its own (per-unit-correct) variables. No-op without tuning.
	if (Ctx.MovementData) { HydrateTuningFromData(Ctx.MovementData->MovementClassData); }

	if (!CachedHandle) { CachedHandle = NewObject<USeinMoverHandle>(this); }
	CachedHandle->SetContext(&Ctx);
	BP_OnMoveBegin(CachedHandle);
	CachedHandle->SetContext(nullptr);
}

void USeinMovement::OnMoveEnd(FSeinEntity& Entity)
{
	if (!CachedHandle) { CachedHandle = NewObject<USeinMoverHandle>(this); }
	CachedHandle->SetEntityOnly(&Entity);
	BP_OnMoveEnd(CachedHandle);
	CachedHandle->SetEntityOnly(nullptr);
}

void USeinMovement::TickIdle(const FSeinMovementContext& Ctx)
{
	if (!CachedHandle) { CachedHandle = NewObject<USeinMoverHandle>(this); }
	CachedHandle->SetContext(&Ctx);
	BP_TickIdle(CachedHandle);
	CachedHandle->SetContext(nullptr);
}

void USeinMovement::HydrateTuningFromData(const FInstancedStruct& Tuning)
{
	const UScriptStruct* SS = Tuning.GetScriptStruct();
	if (!SS) return;
	const uint8* Src = Tuning.GetMemory();
	if (!Src) return;

	// UDS fields carry GUID-mangled FNames; GetAuthoredNameForField (a UStruct virtual that
	// UUserDefinedStruct overrides) yields the friendly name, so it lines up with the BP
	// mode's clean variable names. Native structs return the plain field name.
	UClass* Cls = GetClass();
	for (TFieldIterator<FProperty> It(SS); It; ++It)
	{
		const FProperty* SrcProp = *It;
		const FName MatchName(*SS->GetAuthoredNameForField(SrcProp));
		FProperty* DstProp = FindFProperty<FProperty>(Cls, MatchName);
		if (!DstProp || !DstProp->SameType(SrcProp)) continue;
		DstProp->CopyCompleteValue(
			DstProp->ContainerPtrToValuePtr<void>(this),
			SrcProp->ContainerPtrToValuePtr<void>(Src));
	}
}

// The former value-hooks (ComputeSpeed / ComputeArrivalSpeedCap / ComputeSteer / ShouldReverse),
// the StepAlongPath inner integrator, and the BP_Tick_Implementation monolith were removed with the
// two-tier de-monolith. Their behavior now lives in: the base Tick harness + ComputeMotion policy
// (above), the shared static toolbox (KinematicArrivalSpeedCap / StepSpeedToward / ResolveNavCollision
// / IsOvershootArrival / slope helpers) that Tier-2 modes call directly, and — for accel/decel/reverse
// feel — the concrete Movement+ modes' own Tick overrides.

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
		const FFixedPoint OffDx = AgentPos.X - Wp.X;
		const FFixedPoint OffDy = AgentPos.Y - Wp.Y;

		bool bAdvance = false;

		// Overshoot test — has the agent passed Wp ALONG THE INCOMING travel
		// direction (PrevWp → Wp)? This MUST use the incoming direction, NOT the
		// outgoing (Wp → NextWp). The outgoing form fires whenever the agent is
		// merely on the NextWp side of the plane through Wp, regardless of lateral
		// offset — so a unit sitting off to the side of a long leg (e.g. NORTH of a
		// detour's south-going first leg) reads as "already past" waypoints it never
		// approached, skips the ENTIRE detour, and steers straight at the far /
		// destination waypoint THROUGH the wall the detour routed around (the unit
		// then pins on the movement floor at the wall face and never recovers,
		// because at the face it's still "past" by the outgoing test). The incoming
		// direction only registers a genuine overshoot beyond a waypoint the agent
		// actually traveled toward. The first waypoint has no incoming segment, so
		// it advances on distance alone (per-tick steps are << CloseRadius, so the
		// distance test reliably catches its arrival).
		if (CurrentWaypointIndex > 0)
		{
			const FFixedVector& PrevWp = Path.Waypoints[CurrentWaypointIndex - 1];
			const FFixedPoint InDx = Wp.X - PrevWp.X;
			const FFixedPoint InDy = Wp.Y - PrevWp.Y;
			if (OffDx * InDx + OffDy * InDy > FFixedPoint::Zero) bAdvance = true;
		}

		// Distance test — genuinely within CloseRadius of Wp. Primary trigger for
		// normal arrival (the mode steers straight at Wp, so the agent always passes
		// within CloseRadius of it) and the sole trigger for the first waypoint.
		if (!bAdvance && (OffDx * OffDx + OffDy * OffDy) <= CloseRadiusSq)
		{
			bAdvance = true;
		}

		if (bAdvance) ++CurrentWaypointIndex;
		else break;
	}
}

FFixedVector USeinMovement::ResolveLookAheadPoint(
	const FFixedVector& AgentPos,
	const FSeinPath& Path,
	int32 CurrentWaypointIndex,
	FFixedPoint LookAhead)
{
	const int32 N = Path.Waypoints.Num();
	if (N == 0) return AgentPos;
	if (CurrentWaypointIndex >= N) return Path.Waypoints[N - 1];
	if (CurrentWaypointIndex < 0) CurrentWaypointIndex = 0;

	// Pure linear look-ahead walker with controller-side cluster skip.
	//
	// History: this function once carried a `MaxCornerAngleRadians`-driven
	// cos-falloff weight on each segment past the current one — meant to
	// smoothly reduce the carrot's reach across corners and prevent pure-
	// pursuit corner-cutting. In practice it tangled badly with off-path
	// drift (the synthetic AgentPos→Waypoints[CurIdx] segment contaminated
	// cumulative turn angle), produced multiple regressions, and was stripped
	// (the parameter went with it).
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

FFixedPoint USeinMovement::EffectiveTopSpeed(const FSeinMovementContext& Ctx)
{
	if (!Ctx.MovementData) return FFixedPoint::Zero;
	// Authored cruise speed scaled by the terrain at the unit's position this tick.
	// Ctx.TerrainSpeedMultiplier defaults to 1 (and the settings getter floors it at
	// 0.05), so this is behaviour-preserving wherever no terrain speed is authored.
	return Ctx.MovementData->TopSpeed * Ctx.TerrainSpeedMultiplier;
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
	return Nav ? Nav->GetCellHeightAt(WorldPos, OutZ, /*bWalkableOnly=*/ bSnapsToGround) : false;
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

void USeinMovement::SnapToGroundImmediate(
	FSeinEntity& Entity,
	FSeinMovementComponent& MovementData,
	USeinNavigation* Nav) const
{
	if (!Nav) return;

	// Instant ground snap: DeltaTime 0 makes ApplyGroundSnapAndAltitude take its
	// snap-to-target branch (no rate limit). Z is left untouched if the nav can't
	// sample here (out of bounds / blocked sliver).
	FFixedVector Pos = Entity.Transform.GetLocation();
	ApplyGroundSnapAndAltitude(Pos, &MovementData, Nav, FFixedPoint::Zero);

	// Slope pitch/roll under the entity's current facing — set directly (no
	// smoothing; this is the initial pose). Mirrors the movement Tick's snap.
	const FFixedPoint Yaw   = YawFromRotation(Entity.Transform.Rotation);
	const FFixedPoint Pitch = ComputeSlopePitch(Pos, Yaw, Nav);
	const FFixedPoint Roll  = ComputeSlopeRoll(Pos, Yaw, Nav);
	MovementData.SmoothedPitch = Pitch;
	MovementData.SmoothedRoll  = Roll;

	Entity.Transform.SetLocation(Pos);
	Entity.Transform.Rotation = YawPitchRoll(Yaw, Pitch, Roll);
}

void USeinMovement::BP_TickIdle_Implementation(USeinMoverHandle* Mover)
{
	if (!Mover) return;
	const FSeinMovementContext* CtxPtr = Mover->GetContext();
	if (!CtxPtr) return;
	const FSeinMovementContext& Ctx = *CtxPtr;

	if (!Ctx.MovementData) return;

	FSeinEntity& Entity = Ctx.Entity;
	FSeinMovementComponent& MovementData = *Ctx.MovementData;
	USeinNavigation* Nav = Ctx.Nav;
	const FFixedPoint DeltaTime = Ctx.DeltaTime;

	// First contact: one-time immediate ground + slope snap (subsumes the
	// retired FSeinInitialSnapSystem). Gated on a loaded bake so level-placed
	// units that spawn before nav loads harmlessly retry next tick; the latch
	// lives on the (hashed, serialized) component so snapshot/replay agree.
	if (!MovementData.bInitialGroundSnapDone)
	{
		if (!Nav || !Nav->HasRuntimeData()) return;
		SnapToGroundImmediate(Entity, MovementData, Nav);
		MovementData.bInitialGroundSnapDone = true;
		return;
	}

	const FFixedVector InitialPos = Entity.Transform.GetLocation();
	FFixedVector Pos = InitialPos;

	// Coast-down: residual momentum (an order cancelled / preempted mid-stride
	// deliberately leaves Velocity set) decays to rest through the SAME decel
	// ramp orders use, instead of the unit freezing mid-stride. The footprint-
	// aware nav floor still applies — a coasting unit can't drift through a
	// wall. The footprint cache is rebuilt per coast tick: coasting is brief
	// and the cache may never have been primed for a never-ordered unit.
	FFixedPoint Speed = MovementData.Velocity.Size();
	if (Speed > FFixedPoint::Epsilon)
	{
		// The mode's braking rate: 0 for the ultra-basic modes (they stop crisply the moment the
		// order releases), or the per-class UDS Deceleration for Infantry / the vehicles (a cancelled
		// unit coasts to a halt through the same ramp its orders use).
		const FFixedPoint Decel = GetDeceleration(&MovementData);
		if (Decel <= FFixedPoint::Zero)
		{
			MovementData.Velocity = FFixedVector::ZeroVector;
		}
		else
		{
			CacheFootprintFromContext(Ctx);
			Speed = StepSpeedToward(Speed, FFixedPoint::Zero, Decel, Decel, DeltaTime);
			if (Speed <= FFixedPoint::Epsilon)
			{
				MovementData.Velocity = FFixedVector::ZeroVector;
			}
			else
			{
				const FFixedVector Dir = FFixedVector::GetSafeNormal(MovementData.Velocity);
				Pos.X = Pos.X + Dir.X * Speed * DeltaTime;
				Pos.Y = Pos.Y + Dir.Y * Speed * DeltaTime;
				MovementData.Velocity = FFixedVector(Dir.X * Speed, Dir.Y * Speed, FFixedPoint::Zero);
			}
			Pos = ResolveNavCollision(InitialPos, Pos, Nav);
		}
	}

	// Per-tick settle: re-snap Z/altitude (rate-limited) and smooth pitch/roll
	// toward the slope under the CURRENT position — yaw is never touched while
	// idle. This is what makes a collision-shoved unit settle where it lands
	// (settle-in-place semantics: no return-to-home) instead of floating / clipping at its
	// pre-shove pose. Stationary converged units: the samples still run (no
	// transform-dirty signal exists to skip on) but the write-guard below keeps
	// the rotation untouched — flagged in Roadmap_Multithreading.md territory
	// if idle-unit sampling ever shows up in profiles.
	ApplyGroundSnapAndAltitude(Pos, Ctx.MovementData, Nav, DeltaTime);
	Entity.Transform.SetLocation(Pos);

	const FFixedPoint Yaw = YawFromRotation(Entity.Transform.Rotation);
	const FFixedPoint TargetPitch = ComputeSlopePitch(Pos, Yaw, Nav);
	const FFixedPoint TargetRoll  = ComputeSlopeRoll(Pos, Yaw, Nav);
	const FFixedPoint OrientRate  = FFixedPoint::Pi / FFixedPoint::FromInt(3); // 60°/s — matches move ticks
	const FFixedPoint NewPitch = SmoothAngleToward(MovementData.SmoothedPitch, TargetPitch, OrientRate, DeltaTime);
	const FFixedPoint NewRoll  = SmoothAngleToward(MovementData.SmoothedRoll,  TargetRoll,  OrientRate, DeltaTime);
	if (NewPitch != MovementData.SmoothedPitch || NewRoll != MovementData.SmoothedRoll)
	{
		MovementData.SmoothedPitch = NewPitch;
		MovementData.SmoothedRoll  = NewRoll;
		Entity.Transform.Rotation  = YawPitchRoll(Yaw, NewPitch, NewRoll);
	}
}

bool USeinMovement::IsFootprintPassable(const FFixedVector& Pos, USeinNavigation* Nav) const
{
	if (!Nav) return true;
	// Dynamic-AWARE floor: IsWorldPositionClear rejects the static bake AND runtime
	// dynamic nav blockers (bBlocksNav — non-baked cover walls / deployables). A
	// static-only check (the former IsPassable) let a body slide THROUGH a dynamic
	// wall even though A* routed around it, because the steering/arrival step isn't
	// perfectly on-path and nothing downstream stopped it. Mask = the agent's own
	// layer, so a blocker only stops layers it's authored to hit; by default units
	// don't stamp nav, so this blocks against structures, not other units.
	if (!Nav->IsWorldPositionClear(Pos, CachedNavLayerMask)) return false;
	for (int32 i = 0; i < CachedNumFootprintSamples; ++i)
	{
		const FFixedVector SamplePos(
			Pos.X + CachedFootprintSamples[i].X,
			Pos.Y + CachedFootprintSamples[i].Y,
			Pos.Z);
		if (!Nav->IsWorldPositionClear(SamplePos, CachedNavLayerMask)) return false;
	}
	return true;
}

FFixedVector USeinMovement::ResolveNavCollision(
	const FFixedVector& OldPos,
	const FFixedVector& NewPos,
	USeinNavigation* Nav,
	const FFixedVector* AuthoritativeDest) const
{
	// SWEPT nav floor. A fast unit whose one-tick planar move exceeds its own footprint
	// radius could step OVER a thin blocker sitting BETWEEN its start and end footprints —
	// the continuous-space twin of the LoS supercover tunnel. Subdivide such a move into
	// footprint-radius-sized hops and resolve each, so the body is checked continuously.
	// SELF-GATING, NO SETTING: the hop length IS the footprint radius, so a unit only
	// subdivides when |step| > radius (TopSpeed·dt > radius → ~1500 cm/s for a 50 cm
	// footprint at 30 Hz) — genuinely fast movers only. Everyone else pays one Size() +
	// compare and takes the single-step path. Deterministic (fixed-point Size / division).
	const FFixedPoint SubStep = CachedCollisionRadius;
	if (Nav && SubStep > FFixedPoint::Zero)
	{
		const FFixedVector Delta(NewPos.X - OldPos.X, NewPos.Y - OldPos.Y, FFixedPoint::Zero);
		const FFixedPoint Dist = Delta.Size();
		if (Dist > SubStep)
		{
			const int32 Hops = (Dist / SubStep).ToInt() + 1;   // ceil-ish; >= 2 in this branch
			FFixedVector Cur = OldPos;
			for (int32 h = 1; h <= Hops; ++h)
			{
				const FFixedPoint T = FFixedPoint::FromInt(h) / FFixedPoint::FromInt(Hops);
				const FFixedVector Target(OldPos.X + Delta.X * T, OldPos.Y + Delta.Y * T, NewPos.Z);
				const FFixedVector Resolved = ResolveNavCollisionStep(Cur, Target, Nav, AuthoritativeDest);
				// Blocked / slid mid-sweep → stop at the last clear point (don't keep walking
				// the original line past a blocker the hop just refused).
				if (Resolved.X != Target.X || Resolved.Y != Target.Y) return Resolved;
				Cur = Resolved;
			}
			return Cur; // whole sweep clear → planar-equal to NewPos
		}
	}
	return ResolveNavCollisionStep(OldPos, NewPos, Nav, AuthoritativeDest);
}

FFixedVector USeinMovement::ResolveNavCollisionStep(
	const FFixedVector& OldPos,
	const FFixedVector& NewPos,
	USeinNavigation* Nav,
	const FFixedVector* AuthoritativeDest) const
{
	if (!Nav) return NewPos;

	// Escape valve — if the unit's CENTER is already inside a blocked cell (spawned
	// on / shoved onto a nav blocker before the floor could stop it), do NOT pin it
	// there: return the candidate so it can move toward its path (which routes OUT)
	// until its center clears, at which point the floor re-engages footprint-clamping.
	// Center-only (not the footprint) so it never false-triggers on a unit merely
	// grazing a wall edge — that case still wants the normal clamp below. Pairs with
	// A*'s dynamic-blocked-start tolerance so a spawn-on-a-blocker unit can extract.
	if (!Nav->IsWorldPositionClear(OldPos, CachedNavLayerMask)) return NewPos;

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
	const FFixedVector& Steer = Ctx.MovementData->AvoidanceOutput.SteerDir;

	// Bit-exact no-op when not avoiding: return the input direction UNCHANGED (no
	// renormalize), so AvoidanceStrength = 0 / no-neighbour units move identically
	// to a world with no avoidance.
	if (Steer.SizeSquared() <= FFixedPoint::Epsilon) return DesiredDir;

	// Bend the (unit) desired direction by the lateral steer, then renormalize.
	const FFixedVector Bent = FFixedVector::GetSafeNormal(
		FFixedVector(DesiredDir.X + Steer.X, DesiredDir.Y + Steer.Y, DesiredDir.Z));

	// WALL-TANGENT GUARD. Avoidance is wall-blind, so the bent heading can aim into static
	// geometry — the nav floor then pins the unit and it grinds. Do NOT just drop avoidance near
	// walls (that funnels every unit onto one path line → corner pile, the prior regression);
	// instead keep as much lateral steer as the wall allows: if the bent step is nav-blocked,
	// scale the steer down toward the path heading until it clears (a tangent-along-the-wall
	// approximation), and only fall back to the pure path heading if even a gentle steer is
	// blocked. Nav reads are the static bake (immutable this tick) → deterministic; a few probes,
	// first step only. (Dense corner piles are separately handled by the avoidance regime hand-off.)
	if (Ctx.Nav)
	{
		const FFixedVector Pos = Ctx.Entity.Transform.GetLocation();
		const FFixedPoint Probe = CachedCollisionRadius + FFixedPoint::FromInt(50);
		const FFixedVector BentCand(Pos.X + Bent.X * Probe, Pos.Y + Bent.Y * Probe, Pos.Z);
		if (!IsFootprintPassable(BentCand, Ctx.Nav))
		{
			const FFixedPoint S1 = FFixedPoint::FromInt(2) / FFixedPoint::FromInt(3); // 0.66
			const FFixedVector Cand1 = FFixedVector::GetSafeNormal(
				FFixedVector(DesiredDir.X + Steer.X * S1, DesiredDir.Y + Steer.Y * S1, DesiredDir.Z));
			if (IsFootprintPassable(FFixedVector(Pos.X + Cand1.X * Probe, Pos.Y + Cand1.Y * Probe, Pos.Z), Ctx.Nav))
				return Cand1;

			const FFixedPoint S2 = FFixedPoint::One / FFixedPoint::FromInt(3); // 0.33
			const FFixedVector Cand2 = FFixedVector::GetSafeNormal(
				FFixedVector(DesiredDir.X + Steer.X * S2, DesiredDir.Y + Steer.Y * S2, DesiredDir.Z));
			if (IsFootprintPassable(FFixedVector(Pos.X + Cand2.X * Probe, Pos.Y + Cand2.Y * Probe, Pos.Z), Ctx.Nav))
				return Cand2;

			return DesiredDir; // even a gentle steer hits the wall — follow the path, let the floor resolve
		}
	}

	return Bent;
}

FFixedPoint USeinMovement::GetAvoidanceSpeedScale(const FSeinMovementContext& Ctx) const
{
	// PURE READ of the PreTick-written speed-yield channel (same one-sided, order-
	// independent discipline as ApplyAvoidanceSteer — never read neighbour state here).
	// 1 = no change. The shipped boids model never modulates speed, so this is a
	// byte-identical no-op until a custom avoidance model writes SpeedScale < 1 to make
	// a unit YIELD by braking (rather than only turning). The base RTS loop multiplies
	// its cruise target by this; a custom Tick reads it via the Mover Handle.
	return Ctx.MovementData ? Ctx.MovementData->AvoidanceOutput.SpeedScale : FFixedPoint::One;
}

FFixedQuaternion USeinMovement::ApplySlopeTilt(
	const FFixedVector& Pos,
	FFixedPoint Yaw,
	FSeinMovementComponent* MovementData,
	USeinNavigation* Nav,
	FFixedPoint DeltaTime) const
{
	if (!MovementData) return YawPitchRoll(Yaw, FFixedPoint::Zero, FFixedPoint::Zero);

	const FFixedPoint TargetPitch = ComputeSlopePitch(Pos, Yaw, Nav);
	const FFixedPoint TargetRoll  = ComputeSlopeRoll(Pos, Yaw, Nav);
	const FFixedPoint OrientRate  = FFixedPoint::Pi / FFixedPoint::FromInt(3);
	MovementData->SmoothedPitch = SmoothAngleToward(MovementData->SmoothedPitch, TargetPitch, OrientRate, DeltaTime);
	MovementData->SmoothedRoll  = SmoothAngleToward(MovementData->SmoothedRoll,  TargetRoll,  OrientRate, DeltaTime);
	return YawPitchRoll(Yaw, MovementData->SmoothedPitch, MovementData->SmoothedRoll);
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
	// Fetch Extents from storage, then run the shared cascade (the pointer
	// overload below). Hot loops that have hoisted their Extents storage call
	// that overload directly, skipping this per-call GetComponent lookup.
	const FSeinExtentsComponent* Extents = World
		? World->GetComponent<FSeinExtentsComponent>(SelfHandle)
		: nullptr;
	return ResolveCollisionRadius(Extents, NavData);
}

FFixedPoint USeinMovement::ResolveCollisionRadius(
	const FSeinExtentsComponent* Extents,
	const FSeinNavigationComponent* NavData)
{
	FFixedPoint Radius = FFixedPoint::Zero;
	if (Extents && Extents->Shapes.Num() > 0)
	{
		for (const FSeinExtentsShape& Shape : Extents->Shapes)
		{
			const FFixedPoint ShapeRadius = SeinExtentsHelpers::BoundingRadius(Shape);
			if (ShapeRadius > Radius) Radius = ShapeRadius;
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
	// Agent nav layer (default ground 0x01) — the nav floor passes this to
	// IsWorldPositionClear so a dynamic blocker only stops layers it's authored to.
	CachedNavLayerMask = Ctx.NavData ? Ctx.NavData->NavLayerMask : uint8(0x01);

	if (Radius > FFixedPoint::Zero)
	{
		// 8 ring samples at 45° spacing. Computed once per move action via
		// SeinMath::Cos / SeinMath::Sin (~1µs total). Per-tick cost in
		// ResolveNavCollision becomes 9 IsWorldPositionClear calls (center + ring) =
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
	// Sealed sim-facing entry — bind the reusable planner handle to the context + output and
	// dispatch to the BP-overridable BP_PlanPath. (const: the cached scratch handle is not
	// movement-instance state, so the localized const_cast is safe — same idiom as the per-tick path.)
	USeinMovement* Self = const_cast<USeinMovement*>(this);
	if (!Self->CachedPlannerHandle)
	{
		Self->CachedPlannerHandle = NewObject<USeinPlannerHandle>(Self);
	}
	Self->CachedPlannerHandle->SetContext(&Ctx, &OutPath);
	const ESeinPathResult Result = BP_PlanPath(Self->CachedPlannerHandle);
	Self->CachedPlannerHandle->SetContext(nullptr, nullptr);
	return Result;
}

ESeinPathResult USeinMovement::BP_PlanPath_Implementation(USeinPlannerHandle* Planner) const
{
	if (!Planner) return ESeinPathResult::NoNavigation;
	// Built-in default (the former inline PlanPath, now composed from the planner handle's nodes —
	// the single source of each branch): flyers consume a straight [start,end] line; ground units
	// route a budgeted A* request.
	if (BypassPathfinding())
	{
		return Planner->BuildStraightLinePath();
	}
	return Planner->RequestNavPath();
}

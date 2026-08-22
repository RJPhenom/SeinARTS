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
#include "Components/SeinBrokerMembershipData.h"  // settle-facing: my broker
#include "Components/SeinCommandBrokerData.h"     // settle-facing: broker's persisted slots
#include "Settings/PluginSettings.h"              // bSettleToFormationFacing
#include "Movement/SeinMoverHandle.h"
#include "Movement/SeinPlannerHandle.h"
#include "Simulation/SeinMovementTraceLog.h"       // [ARRIVE] movement-trace events
#include "StructUtils/InstancedStruct.h"
#include "Math/BigInt.h"
#include "UObject/UnrealType.h"

#if UE_ENABLE_DEBUG_DRAWING
#include "Debug/SeinDebugDrawCull.h"
#include "DrawDebugHelpers.h"
#endif

namespace
{
	FFixedPoint SaturatingPositiveAdd(FFixedPoint A, FFixedPoint B)
	{
		if (A > FFixedPoint::Zero && B > FFixedPoint::Zero
			&& A > FFixedPoint::MaxValue - B)
		{
			return FFixedPoint::MaxValue;
		}
		return A + B;
	}

	FFixedPoint SaturatingPositiveScale(FFixedPoint Value, int32 Scale)
	{
		const FFixedPoint FixedScale = FFixedPoint::FromInt(Scale);
		if (Value > FFixedPoint::Zero && Scale > 0
			&& Value > FFixedPoint::MaxValue / FixedScale)
		{
			return FFixedPoint::MaxValue;
		}
		return Value * FixedScale;
	}

	struct FSeinUInt128
	{
		uint64 High = 0;
		uint64 Low = 0;
	};

	FSeinUInt128 MultiplyUnsigned64(uint64 A, uint64 B)
	{
		FSeinUInt128 Result;
#if defined(__GNUC__) || defined(__clang__)
		const unsigned __int128 Product =
			static_cast<unsigned __int128>(A) * B;
		Result.High = static_cast<uint64>(Product >> 64);
		Result.Low = static_cast<uint64>(Product);
#elif defined(_MSC_VER)
		Result.Low = _umul128(A, B, &Result.High);
#else
#error "Platform does not support 128-bit multiplication"
#endif
		return Result;
	}

	bool IsSquareWithin(uint64 Value, const FSeinUInt128& Limit)
	{
		const FSeinUInt128 Square = MultiplyUnsigned64(Value, Value);
		return Square.High < Limit.High
			|| (Square.High == Limit.High && Square.Low <= Limit.Low);
	}

	FFixedPoint FloorSqrtTwiceRawProduct(
		FFixedPoint A,
		FFixedPoint B)
	{
		FSeinUInt128 Radicand = MultiplyUnsigned64(
			static_cast<uint64>(A.Value),
			static_cast<uint64>(B.Value));
		Radicand.High = (Radicand.High << 1) | (Radicand.Low >> 63);
		Radicand.Low <<= 1;

		uint64 Low = 0;
		uint64 High = static_cast<uint64>(INT64_MAX);
		while (Low < High)
		{
			const uint64 Span = High - Low;
			const uint64 Mid = Low + (Span >> 1) + (Span & 1ULL);
			if (IsSquareWithin(Mid, Radicand))
			{
				Low = Mid;
			}
			else
			{
				High = Mid - 1;
			}
		}
		return FFixedPoint(static_cast<int64>(Low));
	}

	int512 MakeSignedInt512(int64 Value)
	{
		int512 Result(Value);
		if (Value < 0)
		{
			uint32* Words = Result.GetBits();
			for (int32 WordIndex = 2; WordIndex < 512 / 32; ++WordIndex)
			{
				Words[WordIndex] = MAX_uint32;
			}
		}
		return Result;
	}

	int512 ExactPlanarLengthRawFloor(
		const FFixedVector& Start,
		const FFixedVector& End)
	{
		const int512 DX = MakeSignedInt512(End.X.Value)
			- MakeSignedInt512(Start.X.Value);
		const int512 DY = MakeSignedInt512(End.Y.Value)
			- MakeSignedInt512(Start.Y.Value);
		const int512 LengthSquared = DX * DX + DY * DY;
		const int512 One(int64(1));
		int512 Low(int64(0));
		int512 High = One << 65;
		while (High - Low > One)
		{
			const int512 Mid = (Low + High) >> 1;
			if (Mid * Mid <= LengthSquared)
			{
				Low = Mid;
			}
			else
			{
				High = Mid;
			}
		}
		return Low;
	}

	FFixedPoint InterpolateZByPlanarDistanceExact(
		const FFixedVector& Start,
		const FFixedVector& End,
		FFixedPoint PlanarDistance,
		const int512& PlanarLengthRaw)
	{
		const int512 DeltaZ = MakeSignedInt512(End.Z.Value)
			- MakeSignedInt512(Start.Z.Value);
		const int512 Adjustment =
			(DeltaZ * int512(PlanarDistance.Value)) / PlanarLengthRaw;
		return FFixedPoint(
			(MakeSignedInt512(Start.Z.Value) + Adjustment).ToInt());
	}

	bool IsRawDifferenceRepresentable(int64 A, int64 B)
	{
		const uint64 Magnitude = A >= B
			? static_cast<uint64>(A) - static_cast<uint64>(B)
			: static_cast<uint64>(B) - static_cast<uint64>(A);
		return Magnitude <= static_cast<uint64>(INT64_MAX);
	}
}

// ======================================================================================
// Steering seam (two-tier). The base Tick(Ctx) is the shared MECHANISM HARNESS: waypoint advance →
// arrival (acceptance ring OR IsOvershootArrival) → the mode's ComputeMotion policy (desired
// velocity + facing) → translate + nav-collision floor + ground snap + TurnRate-clamped turn +
// slope tilt + velocity persist. Tier-1 modes (Basic / BasicUnit / Infantry, and BP-authored modes)
// override ComputeMotion only. Tier-2 modes (the Movement+ vehicles) override Tick(Ctx) directly for
// full control and never reach the harness or ComputeMotion.
// ======================================================================================

bool USeinMovement::TryFinalizeAuthoritativeArrival(
	const FSeinMovementContext& Ctx)
{
	if (!Ctx.bAuthoritativeDestination)
	{
		return true;
	}

	const FSeinPath& Path = Ctx.Path;
	if (Path.Waypoints.IsEmpty())
	{
		return false;
	}

	const FFixedVector CurrentPos = Ctx.Entity.Transform.GetLocation();
	const FFixedVector& FinalWp = Path.Waypoints.Last();
	const FFixedPoint VicinityRadius = SaturatingPositiveScale(
		Ctx.GetAcceptanceRadius(), 2);
	if (!FFixedVector::IsPlanarDistanceWithin(
			CurrentPos, FinalWp, VicinityRadius))
	{
		return false;
	}

	FFixedVector ExactPos = ResolveNavCollision(
		CurrentPos, FinalWp, Ctx.Nav, &FinalWp);
	ApplyGroundSnapAndAltitude(
		ExactPos, Ctx.MovementData, Ctx.Nav, Ctx.DeltaTime);
	if (ExactPos != FinalWp)
	{
		return false;
	}

	Ctx.Entity.Transform.SetLocation(ExactPos);
	return true;
}

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
	const FFixedPoint AcceptanceRadius = Ctx.GetAcceptanceRadius();

	// MECHANISM: advance past any waypoint the unit has crossed/reached (incoming-direction
	// crossover + distance fallback), so the policy always steers at a waypoint ahead of it.
	// Harness-owned — see the AdvanceWaypointAlongPath ownership contract in the header.
	AdvanceWaypointAlongPath(Ctx);

	// MECHANISM: arrival TRIGGER. Within the acceptance ring, OR an overshoot (close + slow +
	// heading away) — the graceful-stop guard that stops a unit orbiting a slot it can't quite
	// land on. An authoritative destination must first consume the exact final step through the
	// same nav/dynamic-safety resolver; ordinary destinations retain radius-based arrival. WHAT
	// the unit is left doing is the mode's arrival POLICY (ComputeArrivalMotion — default hard
	// stop), applied via DispatchArrivalMotion so the action's crowd-stall failsafe leaves the
	// unit in the same per-class state.
	{
		const bool bWithinAcceptance =
			Ctx.IsWithinPlanarAcceptance(PrePos, FinalWp);
		const FFixedPoint EntrySpeed        = MovementData.Velocity.Size();
		const FFixedPoint VicinityRadius =
			SaturatingPositiveScale(AcceptanceRadius, 2);
		const FFixedPoint OvershootSpeedCap = MovementData.TopSpeed / FFixedPoint::FromInt(3);
		const bool bOvershoot = Ctx.HasExactAcceptanceRadius()
			? IsOvershootArrivalRadius(
				PrePos, FinalWp, Entity.Transform.Rotation,
				EntrySpeed, VicinityRadius, OvershootSpeedCap)
			: IsOvershootArrival(
				PrePos, FinalWp, Entity.Transform.Rotation,
				EntrySpeed,
				SaturatingPositiveScale(Ctx.AcceptanceRadiusSq, 4),
				OvershootSpeedCap);
		if (bWithinAcceptance || bOvershoot)
		{
			if (TryFinalizeAuthoritativeArrival(Ctx))
			{
#if !UE_BUILD_SHIPPING
				// Movement-trace event: WHICH trigger arrived the unit. An "overshoot" burst
				// right after a mass order — with dist well outside acceptance and entry≈0 —
				// is the spurious-order-start-arrival signature (stale zero Velocity passes
				// the winding-down gate on tick one).
				UE_LOG(LogSeinMoveTrace, Verbose,
					TEXT("[ARRIVE] t=%d h=%d:%d cause=%s dist=%.0f accept=%.0f entry=%.0f"),
					Ctx.World ? Ctx.World->GetCurrentTick() : -1,
					Ctx.SelfHandle.Index, Ctx.SelfHandle.Generation,
					bWithinAcceptance ? TEXT("ring") : TEXT("overshoot"),
					FFixedVector::DistanceSaturated(PrePos, FinalWp).ToFloat(),
					AcceptanceRadius.ToFloat(),
					EntrySpeed.ToFloat());
#endif
				DispatchArrivalMotion(Ctx);
				return true;
			}
			// A live blocker can still reject the final step. Continue the normal
			// movement/stall path rather than settling a reservation off-slot.
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
		const FFixedPoint DistWp  = FFixedVector::DistanceSaturated(
			FFixedVector(PrePos.X, PrePos.Y, FFixedPoint::Zero),
			FFixedVector(
				Path.Waypoints[CurrentWaypointIndex].X,
				Path.Waypoints[CurrentWaypointIndex].Y,
				FFixedPoint::Zero));
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
#if !UE_BUILD_SHIPPING
	// Movement-trace event: the policy commanded real motion but the applied step came out
	// ~zero — the straggler signature (a face-pinned unit far from the final leg has NO
	// failsafe: the stall-settle is final-leg-only and repaths keep succeeding). distWp
	// discriminates the two sub-causes: large distWp = the nav floor refused the whole step
	// (direct + both slides — wall face-pin); distWp≈0 = degenerate carrot (waypoint step-
	// clamp scaled the step away). ~1 line/s per held unit while Verbose.
	if (UE_LOG_ACTIVE(LogSeinMoveTrace, Verbose)
		&& Ctx.World && (Ctx.World->GetCurrentTick() % 30) == 0)
	{
		FFixedVector HeldDelta = NewPos - PrePos;
		HeldDelta.Z = FFixedPoint::Zero;
		const FFixedPoint CmdSpeed = Motion.Velocity.Size();
		if (CmdSpeed > FFixedPoint::FromInt(10) && HeldDelta.SizeSquared() <= FFixedPoint::Epsilon)
		{
			FFixedVector ToWpDiag = Path.Waypoints[CurrentWaypointIndex] - PrePos;
			ToWpDiag.Z = FFixedPoint::Zero;
			UE_LOG(LogSeinMoveTrace, Verbose,
				TEXT("[HOLD] t=%d h=%d:%d cmd=%.0f wp=%d/%d distWp=%.0f"),
				Ctx.World->GetCurrentTick(), Ctx.SelfHandle.Index, Ctx.SelfHandle.Generation,
				CmdSpeed.ToFloat(), CurrentWaypointIndex, N, ToWpDiag.Size().ToFloat());
		}
	}
#endif
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

	const FFixedVector CurrentLocation = Ctx.Entity.Transform.GetLocation();
	FFixedVector Waypoint = Ctx.Path.Waypoints[Ctx.CurrentWaypointIndex];
	Waypoint.Z = CurrentLocation.Z;
	if (FFixedVector::IsDistanceWithin(
		CurrentLocation, Waypoint, FFixedPoint::Epsilon))
	{
		return Motion;
	}

	const FFixedVector Dir   = ApplyAvoidanceSteer(
		Ctx, FFixedVector::GetSafeNormalDifference(CurrentLocation, Waypoint));
	const FFixedPoint  Speed = EffectiveTopSpeed(Ctx) * GetAvoidanceSpeedScale(Ctx);
	Motion.Velocity     = FFixedVector(Dir.X * Speed, Dir.Y * Speed, FFixedPoint::Zero);
	Motion.TargetYaw    = SeinMath::Atan2(Dir.Y, Dir.X);
	Motion.bUpdateFacing = true;
	return Motion;
}

FSeinMotion USeinMovement::ComputeArrivalMotion_Implementation(USeinMoverHandle* /*Mover*/)
{
	// Default arrival policy: HARD STOP. The default-constructed motion is zero velocity with
	// facing untouched — bit-exact with the harness's historic `Velocity = 0` arrival. Modes
	// that roll or loiter through arrival override this to return residual velocity.
	return FSeinMotion();
}

void USeinMovement::DispatchArrivalMotion(const FSeinMovementContext& Ctx)
{
	if (!Ctx.MovementData) return;

	if (!CachedHandle) { CachedHandle = NewObject<USeinMoverHandle>(this); }
	CachedHandle->SetContext(&Ctx);
	const FSeinMotion Arrival = ComputeArrivalMotion(CachedHandle);
	CachedHandle->SetContext(nullptr);

	Ctx.MovementData->Velocity = FFixedVector(Arrival.Velocity.X, Arrival.Velocity.Y, FFixedPoint::Zero);

	// Optional final facing step — same TurnRate clamp + slope handling as the harness's normal
	// facing mechanism, applied once. Post-arrival facing work (settle-facing) continues in
	// TickIdle; this is only the arrival tick's contribution.
	if (Arrival.bUpdateFacing && Ctx.MovementData->TurnRate > FFixedPoint::Zero)
	{
		const FFixedPoint CurrentYaw = YawFromRotation(Ctx.Entity.Transform.Rotation);
		const FFixedPoint YawDelta   = ShortestAngleDelta(CurrentYaw, Arrival.TargetYaw);
		const FFixedPoint MaxTurn    = Ctx.MovementData->TurnRate * Ctx.DeltaTime;
		const FFixedPoint FinalYaw   = CurrentYaw + ClampFP(YawDelta, -MaxTurn, MaxTurn);
		Ctx.Entity.Transform.Rotation = bSnapsToGround
			? ApplySlopeTilt(Ctx.Entity.Transform.GetLocation(), FinalYaw, Ctx.MovementData, Ctx.Nav, Ctx.DeltaTime)
			: YawOnly(FinalYaw);
	}
}

void USeinMovement::AdvanceWaypointAlongPath(const FSeinMovementContext& Ctx)
{
	if (!Ctx.MovementData) return;
	const FFixedPoint OneStep = Ctx.MovementData->TopSpeed * Ctx.DeltaTime;
	const FFixedPoint CloseRadius = (OneStep * FFixedPoint::Two > FFixedPoint::FromInt(50))
		? OneStep * FFixedPoint::Two : FFixedPoint::FromInt(50);
	AdvanceWaypointAlongPath(Ctx.CurrentWaypointIndex, Ctx.Path, Ctx.Entity.Transform.GetLocation(), CloseRadius);
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

	while (CurrentWaypointIndex < N - 1)
	{
		const FFixedVector& Wp = Path.Waypoints[CurrentWaypointIndex];
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
			const FFixedVector OffsetDirection =
				FFixedVector::GetSafeNormalDifference(Wp, AgentPos);
			const FFixedVector IncomingDirection =
				FFixedVector::GetSafeNormalDifference(PrevWp, Wp);
			if (OffsetDirection.X * IncomingDirection.X
					+ OffsetDirection.Y * IncomingDirection.Y
				> FFixedPoint::Zero)
			{
				bAdvance = true;
			}
		}

		// Distance test — genuinely within CloseRadius of Wp. Primary trigger for
		// normal arrival (the mode steers straight at Wp, so the agent always passes
		// within CloseRadius of it) and the sole trigger for the first waypoint.
		if (!bAdvance && FFixedVector::IsPlanarDistanceWithin(
			AgentPos, Wp, CloseRadius))
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

		const FFixedVector PlanarPrev(
			PrevPos.X, PrevPos.Y, FFixedPoint::Zero);
		const FFixedVector PlanarCand(
			Cand.X, Cand.Y, FFixedPoint::Zero);
		const FFixedVector PlanarNext(
			NextRaw.X, NextRaw.Y, FFixedPoint::Zero);
		const FFixedPoint LenA = FFixedVector::DistanceSaturated(
			PlanarPrev, PlanarCand);
		const FFixedPoint LenB = FFixedVector::DistanceSaturated(
			PlanarCand, PlanarNext);

		if (LenA > FFixedPoint::Epsilon && LenA < CloseSegThreshold
			&& LenB > FFixedPoint::Epsilon)
		{
			const FFixedVector NormA =
				FFixedVector::GetSafeNormalDifference(PlanarPrev, PlanarCand);
			const FFixedVector NormB =
				FFixedVector::GetSafeNormalDifference(PlanarCand, PlanarNext);
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
		const FFixedVector PlanarStart(
			SegStart.X, SegStart.Y, FFixedPoint::Zero);
		const FFixedVector PlanarEnd(
			SegEnd.X, SegEnd.Y, FFixedPoint::Zero);
		const FFixedPoint ApproximateSegLen = FFixedVector::DistanceSaturated(
			PlanarStart, PlanarEnd);
		const bool bNeedsExactLength =
			ApproximateSegLen == FFixedPoint::MaxValue
			|| !IsRawDifferenceRepresentable(
				SegStart.Z.Value, SegEnd.Z.Value);
		int512 ExactSegLenRaw(int64(0));
		bool bExactLengthRepresentable = false;
		FFixedPoint SegLen = ApproximateSegLen;
		if (bNeedsExactLength)
		{
			ExactSegLenRaw = ExactPlanarLengthRawFloor(SegStart, SegEnd);
			bExactLengthRepresentable =
				ExactSegLenRaw <= int512(int64(INT64_MAX));
			if (bExactLengthRepresentable)
			{
				SegLen = FFixedPoint(ExactSegLenRaw.ToInt());
			}
		}

		if (Remaining <= SegLen)
		{
			// Exact floor equality is the endpoint. Returning it directly keeps
			// XY and Z from using an approximate direction or overshooting by the
			// sub-raw-unit remainder discarded by the integer square root.
			if (bNeedsExactLength && bExactLengthRepresentable
				&& int512(Remaining.Value) >= ExactSegLenRaw)
			{
				return SegEnd;
			}
			// Carrot lands within this segment.
			if (SegLen > FFixedPoint::Epsilon)
			{
				const FFixedVector Dir =
					FFixedVector::GetSafeNormalDifference(PlanarStart, PlanarEnd);
				FFixedVector Out = SegStart + Dir * Remaining;
				// Z interpolation along the segment by XY fraction so the
				// carrot's elevation tracks the path's slope continuously
				// between waypoints (steering-vector debug viz consumes this).
				if (bNeedsExactLength)
				{
					Out.Z = InterpolateZByPlanarDistanceExact(
						SegStart, SegEnd, Remaining, ExactSegLenRaw);
				}
				else
				{
					const FFixedPoint T = Remaining / SegLen;
					Out.Z = SegStart.Z + (SegEnd.Z - SegStart.Z) * T;
				}
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

	// speedRaw^2 = 2 * distanceRaw * decelerationRaw. Floor the exact
	// 128-bit radicand so the cap is conservative and cannot overstate the
	// speed that can brake inside the remaining distance.
	return FloorSqrtTwiceRawProduct(DistToFinal, Deceleration);
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
		// Seed the idle re-seek HOME (muster pose) from this settled spawn pose. An UN-BROKERED unit
		// (never ordered → no broker → no SettledSlotPositions) returns HERE when shoved, via the
		// broker system's loose-home-return. Seeded once; a later order gives the unit a broker whose
		// SettledSlotPositions supersedes this per-unit home.
		if (!MovementData.bHomeSeeded)
		{
			MovementData.HomePos     = Entity.Transform.GetLocation();
			MovementData.bHomeSeeded = true;
		}
		return;
	}

	const FFixedVector InitialPos = Entity.Transform.GetLocation();
	FFixedVector Pos = InitialPos;

	// IDLE-DODGE intent, resolved UP FRONT so coast-down can yield to it. "Actively dodging" = a
	// qualifying mover's approach left this idler a non-zero dodge steer, the step speed is on, AND
	// the unit is at rest or already moving only at the slow dodge pace. That last clause lets real
	// ORDER residual momentum (a cancelled mid-stride order — fast) coast to rest FIRST: the velocity
	// the dodge writes below is ~IdleStepSpeed, safely under this cap, so an active dodge re-qualifies
	// every tick while fast residual sits above the cap and keeps coasting until it decays into range.
	const USeinARTSCoreSettings* IdleSet = GetDefault<USeinARTSCoreSettings>();
	const FFixedVector& DodgeSteer  = MovementData.AvoidanceOutput.SteerDir;
	const FFixedPoint IdleStepSpeed = IdleSet ? IdleSet->AvoidanceIdleDodgeStepSpeed : FFixedPoint::Zero;
	const FFixedPoint DodgeVelCap   = IdleStepSpeed + IdleStepSpeed / FFixedPoint::Two; // 1.5x headroom
	const bool bDodgeActive = DodgeSteer.SizeSquared() > FFixedPoint::Epsilon
		&& IdleStepSpeed > FFixedPoint::Zero
		&& MovementData.Velocity.SizeSquared() <= DodgeVelCap * DodgeVelCap;

	// Coast-down: residual momentum (an order cancelled / preempted mid-stride
	// deliberately leaves Velocity set) decays to rest through the SAME decel
	// ramp orders use, instead of the unit freezing mid-stride. The footprint-
	// aware nav floor still applies — a coasting unit can't drift through a
	// wall. The footprint cache is rebuilt per coast tick: coasting is brief
	// and the cache may never have been primed for a never-ordered unit.
	// Skipped while a dodge owns motion this tick (the dodge writes velocity below);
	// once the dodge steer clears, the leftover dodge velocity coasts to rest here.
	FFixedPoint Speed = MovementData.Velocity.Size();
	if (!bDodgeActive && Speed > FFixedPoint::Epsilon)
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

	// IDLE-DODGE SHUFFLE (resolve-through, idler side). Consume this unit's OWN precomputed avoidance
	// steer — written one-sided at PreTick only when a qualifying mover is approaching — as a slow
	// lateral step aside. PURE-SELF: no neighbour read, so TickIdle stays deterministic and
	// neighbour-blind. Routed through the nav floor so a dodging idler can't clip a wall.
	//
	// DECOUPLE: unlike the earlier design, this now WRITES an honest displacement-derived velocity.
	// That makes the step VISIBLE to velocity-gated consumers — the anim BP plays the step-aside, and
	// re-seek's OWN settled-predicate (velocity ~zero) holds this member back from re-forming while it
	// dodges — so idle-dodge and re-seek stay two SEPARATE behaviours with no bespoke suppression hook.
	if (bDodgeActive)
	{
		CacheFootprintFromContext(Ctx);
		const FFixedVector DodgeDir = FFixedVector::GetSafeNormal(DodgeSteer);
		const FFixedVector PreDodge = Pos;
		Pos.X = Pos.X + DodgeDir.X * IdleStepSpeed * DeltaTime;
		Pos.Y = Pos.Y + DodgeDir.Y * IdleStepSpeed * DeltaTime;
		Pos = ResolveNavCollision(PreDodge, Pos, Nav);
		// Honest velocity = actual (nav-clamped) planar displacement / dt — the unit's own commanded
		// step, the same definition the ordered path uses.
		if (DeltaTime > FFixedPoint::Zero)
		{
			const FFixedPoint InvDt = FFixedPoint::One / DeltaTime;
			MovementData.Velocity = FFixedVector(
				(Pos.X - PreDodge.X) * InvDt, (Pos.Y - PreDodge.Y) * InvDt, FFixedPoint::Zero);
		}
	}

	// Per-tick settle: re-snap Z/altitude (rate-limited) and smooth pitch/roll
	// toward the slope under the CURRENT position. This is what makes a
	// collision-shoved unit settle where it lands (settle-in-place semantics: no
	// return-to-home) instead of floating / clipping at its pre-shove pose.
	// Stationary converged units: the samples still run (no transform-dirty
	// signal exists to skip on) but the write-guard below keeps the rotation
	// untouched — flagged in Roadmap_Multithreading.md territory if idle-unit
	// sampling ever shows up in profiles.
	ApplyGroundSnapAndAltitude(Pos, Ctx.MovementData, Nav, DeltaTime);
	Entity.Transform.SetLocation(Pos);

	FFixedPoint Yaw = YawFromRotation(Entity.Transform.Rotation);
	bool bYawStepped = false;

	// SETTLE FACING — a unit delivered to a formation slot turns (at its own TurnRate)
	// to the slot's facing while it settles, so an arrived formation faces formation-
	// forward instead of freezing at each unit's last travel heading. "Which slot is
	// mine": exact-match the unit's last ordered goal (TargetLocation — for ground moves
	// this IS the broker's persisted slot position, byte-identical fixed-point) against
	// the broker's SettledSlotPositions; resolved once per order via the instance cache.
	// No match (lone move, entity-targeted order, squad-authored dispatch) → keep the
	// travel heading. Broker reads here are order-independent const reads of data that
	// is stable during this phase (written at CommandProcessing / PostTick), so the
	// parallel idle batch stays race-free and deterministic.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (Settings && Settings->bSettleToFormationFacing && SettlesToSlotFacing()
		&& MovementData.TurnRate > FFixedPoint::Zero && Ctx.World)
	{
		const FFixedVector& Key = MovementData.TargetLocation;
		if (!bCachedSettleFacingResolved
			|| Key.X != CachedSettleFacingKey.X || Key.Y != CachedSettleFacingKey.Y
			|| Key.Z != CachedSettleFacingKey.Z)
		{
			bCachedSettleFacingResolved = true;
			bCachedSettleFacingFound = false;
			CachedSettleFacingKey = Key;
			if (const FSeinBrokerMembershipData* Membership =
					Ctx.World->GetComponent<FSeinBrokerMembershipData>(Ctx.SelfHandle))
			{
				if (Membership->CurrentBrokerHandle.IsValid())
				{
					if (const FSeinCommandBrokerData* Broker =
							Ctx.World->GetComponent<FSeinCommandBrokerData>(Membership->CurrentBrokerHandle))
					{
						for (int32 i = 0; i < Broker->SettledSlotPositions.Num(); ++i)
						{
							const FFixedVector& Slot = Broker->SettledSlotPositions[i];
							if (Slot.X == Key.X && Slot.Y == Key.Y)
							{
								if (Broker->SettledSlotFacings.IsValidIndex(i))
								{
									CachedSettleFacing = Broker->SettledSlotFacings[i];
									bCachedSettleFacingFound = true;
								}
								break;
							}
						}
					}
				}
			}
		}
		if (bCachedSettleFacingFound)
		{
			const FFixedPoint TargetYaw = YawFromRotation(CachedSettleFacing);
			const FFixedPoint Delta = ShortestAngleDelta(Yaw, TargetYaw);
			const FFixedPoint AbsDelta = (Delta < FFixedPoint::Zero) ? -Delta : Delta;
			// Convergence band (~0.25°): below it, stop stepping so fixed-point
			// atan2/quaternion round-trip noise can't jitter a parked unit forever.
			if (AbsDelta > FFixedPoint::Pi / FFixedPoint::FromInt(720))
			{
				const FFixedPoint MaxTurn = MovementData.TurnRate * DeltaTime;
				Yaw = Yaw + ClampFP(Delta, -MaxTurn, MaxTurn);
				bYawStepped = true;
			}
		}
	}

	const FFixedPoint TargetPitch = ComputeSlopePitch(Pos, Yaw, Nav);
	const FFixedPoint TargetRoll  = ComputeSlopeRoll(Pos, Yaw, Nav);
	const FFixedPoint OrientRate  = FFixedPoint::Pi / FFixedPoint::FromInt(3); // 60°/s — matches move ticks
	const FFixedPoint NewPitch = SmoothAngleToward(MovementData.SmoothedPitch, TargetPitch, OrientRate, DeltaTime);
	const FFixedPoint NewRoll  = SmoothAngleToward(MovementData.SmoothedRoll,  TargetRoll,  OrientRate, DeltaTime);
	if (bYawStepped || NewPitch != MovementData.SmoothedPitch || NewRoll != MovementData.SmoothedRoll)
	{
		MovementData.SmoothedPitch = NewPitch;
		MovementData.SmoothedRoll  = NewRoll;
		Entity.Transform.Rotation  = YawPitchRoll(Yaw, NewPitch, NewRoll);
	}
}

bool USeinMovement::IsFootprintPassable(const FFixedVector& Pos, USeinNavigation* Nav) const
{
	if (!Nav) return true;
	const FSeinNavAgentProfile Agent =
		BuildCachedNavAgentProfile();
	// Dynamic-AWARE floor: IsWorldPositionClear rejects the static bake AND runtime
	// dynamic nav blockers (bBlocksNav — non-baked cover walls / deployables). A
	// static-only check (the former IsPassable) let a body slide THROUGH a dynamic
	// wall even though A* routed around it, because the steering/arrival step isn't
	// perfectly on-path and nothing downstream stopped it. Mask = the agent's own
	// layer, so a blocker only stops layers it's authored to hit; by default units
	// don't stamp nav, so this blocks against structures, not other units.
	if (!Nav->IsWorldPositionClearForAgent(Pos, Agent)) return false;
	for (int32 i = 0; i < CachedNumFootprintSamples; ++i)
	{
		const FFixedVector SamplePos(
			Pos.X + CachedFootprintSamples[i].X,
			Pos.Y + CachedFootprintSamples[i].Y,
			Pos.Z);
		if (!Nav->IsWorldPositionClearForAgent(SamplePos, Agent)) return false;
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
	const FSeinNavAgentProfile Agent =
		BuildCachedNavAgentProfile();
	if (!Nav->IsWorldPositionClearForAgent(OldPos, Agent)) return NewPos;

	// Authoritative-destination overrule: when the candidate sits within reach of
	// an authoritative destination (a cover slot), let the unit move there even
	// though the cell is bake-blocked. The slot is a valid standing spot; the
	// blocked ("red") cell under it is a coarse-resolution false-negative, not a
	// wall (root CLAUDE.md #6). Scoped tightly to the slot's immediate vicinity so
	// it never lets the body clip walls anywhere else along the path.
	if (AuthoritativeDest)
	{
		const FFixedPoint ExemptRadius = SaturatingPositiveAdd(
			CachedCollisionRadius, FFixedPoint::FromInt(50));
		const FFixedVector PlanarNewPos(
			NewPos.X, NewPos.Y, FFixedPoint::Zero);
		const FFixedVector PlanarDestination(
			AuthoritativeDest->X, AuthoritativeDest->Y, FFixedPoint::Zero);
		if (FFixedVector::IsPlanarDistanceWithin(
				PlanarNewPos, PlanarDestination, ExemptRadius)
			&& Nav->IsAuthoritativeFootprintSafeForAgent(
				NewPos, Agent))
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

	// GOAL-RELATIVE BEND CAP (planar / XY). Clamp a unit-length bent heading so its angular
	// deviation from the (unit-length) DesiredDir — the mode's straight-line heading to its CURRENT
	// WAYPOINT — never exceeds acos(BendCapCos). This is what breaks dense-melee ORBITS: a summed
	// per-neighbour repulsor can otherwise rotate the bent heading past 90° (mostly lateral / partly
	// backward), so a unit circles a churning crowd. Forcing Bent·DesiredDir >= BendCapCos > 0 keeps
	// a strictly-positive component along the line to the waypoint EVERY tick, so waypoint distance
	// strictly decreases and no limit cycle can form. It only ever REDUCES the perpendicular
	// (off-goal) component while KEEPING its direction — the do-si-do / geometric SIDE choice is
	// never touched, so it cannot reintroduce lock-step or the packing regression. No trig: the cap
	// sine is sqrt(1 − cos²). BendCapCos = −1 (OFF) or a Bent already inside the cap is a bit-exact
	// passthrough.
	const FFixedPoint BendCapCos = GetDefault<USeinARTSCoreSettings>()->AvoidanceBendCapCos;
	const auto CapToGoal = [&](const FFixedVector& In) -> FFixedVector
	{
		if (BendCapCos <= -FFixedPoint::One) return In;                     // OFF sentinel → passthrough
		const FFixedPoint Along = In.X * DesiredDir.X + In.Y * DesiredDir.Y; // cos(theta), both unit-length
		if (Along >= BendCapCos) return In;                                 // already within the cap
		// Perpendicular component of In about DesiredDir — its DIRECTION is the side the steer chose.
		FFixedVector Perp(In.X - DesiredDir.X * Along, In.Y - DesiredDir.Y * Along, FFixedPoint::Zero);
		const FFixedPoint PerpLen = Perp.Size();
		if (PerpLen <= FFixedPoint::Epsilon) return DesiredDir;             // In ≈ ±Desired → no side info → goal
		FFixedPoint SinSq = FFixedPoint::One - BendCapCos * BendCapCos;
		if (SinSq < FFixedPoint::Zero) SinSq = FFixedPoint::Zero;           // guard Sqrt's X>=0 assert (fp rounding)
		const FFixedPoint CapSin = SeinMath::Sqrt(SinSq);
		const FFixedPoint InvPerp = FFixedPoint::One / PerpLen;
		const FFixedVector PerpHat(Perp.X * InvPerp, Perp.Y * InvPerp, FFixedPoint::Zero);
		// Rebuild at exactly the cap angle: cos·Desired + sin·PerpHat (same side, reduced deviation).
		return FFixedVector(
			DesiredDir.X * BendCapCos + PerpHat.X * CapSin,
			DesiredDir.Y * BendCapCos + PerpHat.Y * CapSin,
			DesiredDir.Z);
	};

	// WALL-TANGENT GUARD. Avoidance is wall-blind, so the bent heading can aim into static
	// geometry — the nav floor then pins the unit and it grinds. Do NOT just drop avoidance near
	// walls (that funnels every unit onto one path line → corner pile, the prior regression);
	// instead keep as much lateral steer as the wall allows: if the bent step is nav-blocked,
	// scale the steer down toward the path heading until it clears (a tangent-along-the-wall
	// approximation), and only fall back to the pure path heading if even a gentle steer is
	// blocked. Nav reads are the static bake (immutable this tick) → deterministic; a few probes,
	// first step only. This guard owns wall-tangency; the bend cap above owns melee-orbit
	// prevention. All exits funnel through the single CapToGoal below (a wall-scaled steer only
	// REDUCES deviation, so the cap never fights the guard; the DesiredDir fallback caps to itself).
	FFixedVector Result = Bent;
	if (Ctx.Nav)
	{
		const FFixedVector Pos = Ctx.Entity.Transform.GetLocation();
		const FFixedPoint Probe = SaturatingPositiveAdd(
			CachedCollisionRadius, FFixedPoint::FromInt(50));
		const FFixedVector BentCand(Pos.X + Bent.X * Probe, Pos.Y + Bent.Y * Probe, Pos.Z);
		if (!IsFootprintPassable(BentCand, Ctx.Nav))
		{
			const FFixedPoint S1 = FFixedPoint::FromInt(2) / FFixedPoint::FromInt(3); // 0.66
			const FFixedVector Cand1 = FFixedVector::GetSafeNormal(
				FFixedVector(DesiredDir.X + Steer.X * S1, DesiredDir.Y + Steer.Y * S1, DesiredDir.Z));
			if (IsFootprintPassable(FFixedVector(Pos.X + Cand1.X * Probe, Pos.Y + Cand1.Y * Probe, Pos.Z), Ctx.Nav))
				Result = Cand1;
			else
			{
				const FFixedPoint S2 = FFixedPoint::One / FFixedPoint::FromInt(3); // 0.33
				const FFixedVector Cand2 = FFixedVector::GetSafeNormal(
					FFixedVector(DesiredDir.X + Steer.X * S2, DesiredDir.Y + Steer.Y * S2, DesiredDir.Z));
				if (IsFootprintPassable(FFixedVector(Pos.X + Cand2.X * Probe, Pos.Y + Cand2.Y * Probe, Pos.Z), Ctx.Nav))
					Result = Cand2;
				else
					Result = DesiredDir; // even a gentle steer hits the wall — follow the path, let the floor resolve
			}
		}
	}

	return CapToGoal(Result);
}

FFixedPoint USeinMovement::GetAvoidanceSpeedScale(const FSeinMovementContext& Ctx) const
{
	// PURE READ of the PreTick-written speed-yield channel (same one-sided, order-
	// independent discipline as ApplyAvoidanceSteer — never read neighbour state here).
	// 1 = no change; the multiplier is non-negative and MAY exceed 1 (catch-up). The
	// shipped model produces it as brake-on-steer-saturation × formation cohesion
	// (hold-back / catch-up). The base RTS loop multiplies its cruise target by this;
	// a custom Tick reads it via the Mover Handle.
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
	const FFixedVector& AvoidanceSteer,
	const FFixedPoint& SpeedScale)
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

	const FColor OrangeColor(255, 220, 0, 255); // footprint ring + neutral velocity vector
	const FColor AvoidColor(255, 0, 0, 255);    // avoidance vector

	// Velocity-arrow tint by SpeedScale: neutral orange at 1; toward RED as the unit yields
	// speed (< 1: avoidance brake / cohesion hold-back); toward GREEN as it boosts (> 1:
	// cohesion catch-up). Full tint at ±0.5 from neutral. Render-only float math.
	FColor VelocityColor = OrangeColor;
	{
		const float Scale = SpeedScale.ToFloat();
		if (Scale < 1.0f)
		{
			const float T = FMath::Clamp((1.0f - Scale) / 0.5f, 0.0f, 1.0f);
			VelocityColor = FColor(255, static_cast<uint8>(220.0f * (1.0f - T)), 0, 255);
		}
		else if (Scale > 1.0f)
		{
			const float T = FMath::Clamp((Scale - 1.0f) / 0.5f, 0.0f, 1.0f);
			VelocityColor = FColor(static_cast<uint8>(255.0f * (1.0f - T)), 220, 0, 255);
		}
	}

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
			/*ArrowSize*/ 20.0f, VelocityColor,
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
	//           Whole-collider radius via GetColliderBoundingRadius:
	//             - Capsule → Shape.Radius
	//             - Box     → sqrt(HalfExtentX² + HalfExtentY²) (DIAGONAL)
	//           Diagonal — not max half-extent — is the smallest circle
	//           that fully contains the box (center-to-corner reach).
	//           Compound entities include each shape's planar LocalOffset, then
	//           take the maximum total reach from the entity origin.
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
		Radius =
			SeinExtentsHelpers::GetColliderBoundingRadius(*Extents);
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
	if (Ctx.World)
	{
		// Delegate to the canonical profile builder so this per-order snapshot
		// can never drift from the policy that path planning, containment, and
		// command validation resolve for the same entity. AgentTags are
		// deliberately NOT cached: no shipped navigation consumes them, and
		// caching them would add reflected state to the movement schema. A
		// future tags consumer must revisit BuildCachedNavAgentProfile.
		const FSeinNavAgentProfile Profile =
			Ctx.World->BuildNavAgentProfile(Ctx.SelfHandle, Radius);
		CachedNavLayerMask = Profile.AgentNavLayerMask;
		CachedNavWallPaddingCells = Profile.AgentWallPaddingCells;
		CachedBlockedTerrainTags = Profile.BlockedTerrainTags;
	}
	else
	{
		// No sim subsystem (isolated test worlds): mirror the profile defaults
		// exactly so both branches stay field-for-field aligned.
		CachedNavLayerMask = Ctx.NavData
			? Ctx.NavData->NavLayerMask
			: FSeinNavAgentProfile().AgentNavLayerMask;
		CachedNavWallPaddingCells = Ctx.NavData
			? Ctx.NavData->WallPadding
			: 0;
		CachedBlockedTerrainTags = Ctx.NavData
			? Ctx.NavData->BlockedTerrainTags
			: FGameplayTagContainer();
	}
	CachedNavRequester = Ctx.SelfHandle;

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

FSeinNavAgentProfile USeinMovement::BuildCachedNavAgentProfile() const
{
	// AgentTags deliberately absent — see CacheFootprintFromContext.
	FSeinNavAgentProfile Agent;
	Agent.Requester = CachedNavRequester;
	Agent.BlockedTerrainTags = CachedBlockedTerrainTags;
	Agent.AgentNavLayerMask = CachedNavLayerMask;
	Agent.AgentFootprintRadius = CachedCollisionRadius;
	Agent.AgentWallPaddingCells = CachedNavWallPaddingCells;
	return Agent;
}

bool USeinMovement::IsOvershootArrival(
	const FFixedVector& AgentPos,
	const FFixedVector& FinalWp,
	const FFixedQuaternion& Rotation,
	FFixedPoint CurrentSpeed,
	FFixedPoint VicinityRadiusSq,
	FFixedPoint MaxSpeedForOvershoot)
{
	const FFixedVector PlanarAgent(
		AgentPos.X, AgentPos.Y, FFixedPoint::Zero);
	const FFixedVector PlanarFinal(
		FinalWp.X, FinalWp.Y, FFixedPoint::Zero);
	if (!FFixedVector::IsPlanarDistSquaredWithin(
		PlanarAgent, PlanarFinal, VicinityRadiusSq))
	{
		return false;
	}

	const FFixedPoint AbsSpeed = CurrentSpeed < FFixedPoint::Zero
		? -CurrentSpeed : CurrentSpeed;
	if (AbsSpeed > MaxSpeedForOvershoot)
	{
		return false;
	}
	const FFixedVector ToFinal =
		FFixedVector::GetSafeNormalDifference(PlanarAgent, PlanarFinal);
	const FFixedVector Forward = Rotation.RotateVector(FFixedVector::ForwardVector);
	return Forward.X * ToFinal.X + Forward.Y * ToFinal.Y
		< FFixedPoint::Zero;
}

bool USeinMovement::IsOvershootArrivalRadius(
	const FFixedVector& AgentPos,
	const FFixedVector& FinalWp,
	const FFixedQuaternion& Rotation,
	FFixedPoint CurrentSpeed,
	FFixedPoint VicinityRadius,
	FFixedPoint MaxSpeedForOvershoot)
{
	const FFixedVector PlanarAgent(
		AgentPos.X, AgentPos.Y, FFixedPoint::Zero);
	const FFixedVector PlanarFinal(
		FinalWp.X, FinalWp.Y, FFixedPoint::Zero);
	if (!FFixedVector::IsPlanarDistanceWithin(
		PlanarAgent, PlanarFinal, VicinityRadius))
	{
		return false;
	}

	const FFixedPoint AbsSpeed = (CurrentSpeed < FFixedPoint::Zero) ? -CurrentSpeed : CurrentSpeed;
	if (AbsSpeed > MaxSpeedForOvershoot) return false;

	// "Heading away" — forward · toFinal < 0. ToFinal is non-zero here only
	// if the unit is offset from FinalWp; degenerate-zero falls through to
	// the dot returning 0 and not triggering.
	const FFixedVector ToFinal =
		FFixedVector::GetSafeNormalDifference(PlanarAgent, PlanarFinal);
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

	const FFixedVector PlanarAgent(
		AgentPos.X, AgentPos.Y, FFixedPoint::Zero);
	const FFixedVector PlanarGoal(
		FinalGoal.X, FinalGoal.Y, FFixedPoint::Zero);
	if (!FFixedVector::IsPlanarDistanceWithin(
		PlanarAgent, PlanarGoal,
		MovementData.ReverseEngageDistanceThreshold))
	{
		return false;
	}
	if (FFixedVector::IsPlanarDistanceWithin(
		PlanarAgent, PlanarGoal, FFixedPoint::Epsilon))
	{
		return false;
	}

	// Compare normalized dot against threshold. Threshold is typically
	// negative (target is behind) — using <= so the boundary case engages.
	const FFixedVector ToGoalN =
		FFixedVector::GetSafeNormalDifference(PlanarAgent, PlanarGoal);
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

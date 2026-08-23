/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementBPFL.cpp
 */

#include "Lib/SeinMovementBPFL.h"
#include "Actor/SeinActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinMovementComponent.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Actions/SeinMoveToAction.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Quat.h"
#include "Types/Vector.h"

#include "Math/RotationMatrix.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinMovementBPFL, Log, All);

void USeinMovementBPFL::SeinStopMovement(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	// Movement ONLY — cancels just the entity's USeinMoveToAction, never any other latent
	// action. The move's OnCancel runs (clears arrival state, fires OnCancelled on its
	// proxy); the unit then coasts to rest via the idle driver.
	USeinWorldSubsystem* World = GetWorldSubsystem(WorldContextObject);
	if (World && World->LatentActionManager
		&& World->RequireStateMutationAuthorization(TEXT("StopMovement")))
	{
		World->LatentActionManager->CancelActionsForEntityOfClass(EntityHandle, USeinMoveToAction::StaticClass());
	}
}

namespace
{
	/** Signed angle from `BaseRotation`'s forward axis to `Vel`, in degrees,
	 *  in [-180, 180]. Mirrors `UKismetAnimationLibrary::CalculateDirection`
	 *  so AnimBPs see the same value they'd get from the mannequin template's
	 *  `Calculate Direction` node — without this BPFL having to depend on
	 *  AnimGraphRuntime just for one helper. */
	float CalculateDirectionDegrees(const FVector& Vel, const FRotator& BaseRotation)
	{
		if (Vel.IsNearlyZero()) return 0.0f;

		const FMatrix RotMat = FRotationMatrix(BaseRotation);
		const FVector Forward = RotMat.GetScaledAxis(EAxis::X);
		const FVector Right   = RotMat.GetScaledAxis(EAxis::Y);
		const FVector NormVel = Vel.GetSafeNormal2D();

		const float ForwardDot = static_cast<float>(FVector::DotProduct(Forward, NormVel));
		const float ClampedDot = FMath::Clamp(ForwardDot, -1.0f, 1.0f);
		float Degrees = FMath::RadiansToDegrees(FMath::Acos(ClampedDot));

		// Sign by which side of forward the velocity falls on.
		const float RightDot = static_cast<float>(FVector::DotProduct(Right, NormVel));
		if (RightDot < 0.0f) Degrees = -Degrees;

		return Degrees;
	}

	/** Pull a runtime Altitude value out of the movement component's
	 *  polymorphic sub-data (FInstancedStruct), if present. Sub-data structs
	 *  declared by hover / flight movements carry an `Altitude` FFixedPoint
	 *  field; ground movements have no sub-data Altitude. Uses property
	 *  reflection rather than a hard dependency on the specific sub-data
	 *  types so future altitude-bearing movement classes Just Work without
	 *  this BPFL knowing about them. Returns 0 when no Altitude field is
	 *  present. */
	float QueryAltitudeFromSubData(const FSeinMovementComponent& MoveComp)
	{
		const UScriptStruct* SubStruct = MoveComp.MovementClassData.GetScriptStruct();
		const uint8* SubMemory = MoveComp.MovementClassData.GetMemory();
		if (!SubStruct || !SubMemory) return 0.0f;

		static const FName NAME_Altitude(TEXT("Altitude"));
		FProperty* Prop = SubStruct->FindPropertyByName(NAME_Altitude);
		if (!Prop) return 0.0f;

		const FStructProperty* StructProp = CastField<FStructProperty>(Prop);
		if (!StructProp) return 0.0f;
		if (StructProp->Struct != FFixedPoint::StaticStruct()) return 0.0f;

		const FFixedPoint* AltPtr = reinterpret_cast<const FFixedPoint*>(
			StructProp->ContainerPtrToValuePtr<void>(SubMemory));
		return AltPtr ? AltPtr->ToFloat() : 0.0f;
	}
}

USeinWorldSubsystem* USeinMovementBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

bool USeinMovementBPFL::SeinGetAnimationMovementState(const UObject* WorldContextObject, AActor* Actor, FSeinMovementStateData& OutState)
{
	OutState = FSeinMovementStateData{};

	ASeinActor* SeinActor = Cast<ASeinActor>(Actor);
	if (!SeinActor)
	{
		UE_LOG(LogSeinMovementBPFL, Error,
			TEXT("GetAnimationMovementState: Actor '%s' is not an ASeinActor."),
			Actor ? *Actor->GetName() : TEXT("null"));
		return false;
	}

	return SeinGetMovementState(WorldContextObject, SeinActor->GetEntityHandle(), OutState);
}

bool USeinMovementBPFL::SeinGetMovementState(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinMovementStateData& OutState)
{
	// Reset to defaults on every call so partial fills on early returns
	// don't surprise callers reading individual fields.
	OutState = FSeinMovementStateData{};

	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const FSeinEntity* Entity = Subsystem->GetEntity(EntityHandle);
	if (!Entity) return false;

	const FSeinMovementComponent* MoveData = Subsystem->GetComponent<FSeinMovementComponent>(EntityHandle);
	if (!MoveData) return false;

	// Compose derived state from sim transform + movement payload. Both reads
	// are tick-coherent: the bridge interpolates the actor each render frame
	// but we read the entity's authoritative sim-tick snapshot so all fields
	// agree with each other (no wobble between Velocity and Direction during
	// the interpolation gap between sim ticks).
	const FFixedQuaternion SimRot = Entity->Transform.Rotation;
	const FFixedVector SimForward = SimRot.RotateVector(FFixedVector::ForwardVector);
	const FFixedVector SimVel = MoveData->Velocity;

	// Velocity is stored directly. For non-strafing subclasses (every one
	// shipped today) the value is parallel to Forward by construction. For
	// strafe-capable subclasses the vector carries lateral components and
	// Direction becomes a real, animation-driving signal.
	OutState.Velocity = FVector(SimVel.X.ToFloat(), SimVel.Y.ToFloat(), 0.0f);

	// Ground speed = magnitude of planar velocity. Non-negative regardless
	// of whether the unit is moving forward, backward, or strafing.
	const FFixedPoint VelMagFP = SimVel.Size();
	OutState.GroundSpeed = VelMagFP.ToFloat();

	// Signed forward speed = velocity projected onto the entity's forward
	// axis. Positive when moving forward, negative when reversing, ~0 for
	// pure-strafe motion. Distinct from GroundSpeed: |Speed| <= GroundSpeed,
	// equality only when motion is purely along facing.
	const FFixedPoint SignedFwdFP = SimVel.X * SimForward.X + SimVel.Y * SimForward.Y;
	OutState.Speed = SignedFwdFP.ToFloat();

	// Direction relative to entity facing. For strafe-capable units this is
	// the angle the AnimBP's strafe blendspace consumes; for non-strafing
	// units it's 0 (forward motion) or ±180 (reverse). Stationary entities
	// return 0 — matches the mannequin template's "no defined direction"
	// behavior so blendspaces don't latch on the previous frame's value.
	if (OutState.GroundSpeed > UE_KINDA_SMALL_NUMBER)
	{
		const FRotator EntityRot = SimRot.ToQuat().Rotator();
		OutState.Direction = CalculateDirectionDegrees(OutState.Velocity, EntityRot);
	}

	// MoveEpsilon 1cm/s: well below the at-rest threshold elsewhere in the
	// framework (10cm/s in avoidance). Lower here so AnimBPs catch the
	// start of motion before it's macroscopically visible, smoothing the
	// idle → locomotion transition. Gate on planar magnitude (GroundSpeed)
	// so a strafing unit registers as moving even when its forward
	// component is near zero.
	constexpr float MoveEpsilon = 1.0f;
	OutState.bIsMoving = OutState.GroundSpeed > MoveEpsilon;
	// bIsReversing keys off signed forward speed — a unit driving backward
	// has Speed < 0; a unit pure-strafing has Speed ≈ 0 and shouldn't
	// register as reversing.
	OutState.bIsReversing = OutState.Speed < -MoveEpsilon;

	// Airborne when sub-data Altitude lifts the entity above ground. 1cm
	// threshold filters numerical jitter at rest; hover/flight push Altitude
	// well above this when active. Ground movements have no Altitude sub-
	// data field → QueryAltitudeFromSubData returns 0 → never airborne.
	OutState.bIsAirborne = QueryAltitudeFromSubData(*MoveData) > 1.0f;

	OutState.bArrivalImminent = MoveData->bArrivalImminent;
	OutState.bHasMovementInput = MoveData->bHasTarget;

	return true;
}

// ===== Single-field getters =====
// All delegate to SeinGetMovementState. Cost is one composite computation
// per call. If a caller hits all 9 getters per frame on the same entity,
// switch to Get Movement State once and break the struct.

FVector USeinMovementBPFL::SeinGetVelocity(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	FSeinMovementStateData State;
	SeinGetMovementState(WorldContextObject, EntityHandle, State);
	return State.Velocity;
}

float USeinMovementBPFL::SeinGetSpeed(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	FSeinMovementStateData State;
	SeinGetMovementState(WorldContextObject, EntityHandle, State);
	return State.Speed;
}

float USeinMovementBPFL::SeinGetGroundSpeed(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	FSeinMovementStateData State;
	SeinGetMovementState(WorldContextObject, EntityHandle, State);
	return State.GroundSpeed;
}

float USeinMovementBPFL::SeinGetDirection(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	FSeinMovementStateData State;
	SeinGetMovementState(WorldContextObject, EntityHandle, State);
	return State.Direction;
}

bool USeinMovementBPFL::SeinIsMoving(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	FSeinMovementStateData State;
	SeinGetMovementState(WorldContextObject, EntityHandle, State);
	return State.bIsMoving;
}

bool USeinMovementBPFL::SeinIsReversing(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	FSeinMovementStateData State;
	SeinGetMovementState(WorldContextObject, EntityHandle, State);
	return State.bIsReversing;
}

bool USeinMovementBPFL::SeinIsAirborne(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	FSeinMovementStateData State;
	SeinGetMovementState(WorldContextObject, EntityHandle, State);
	return State.bIsAirborne;
}

bool USeinMovementBPFL::SeinIsArrivalImminent(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	FSeinMovementStateData State;
	SeinGetMovementState(WorldContextObject, EntityHandle, State);
	return State.bArrivalImminent;
}

bool USeinMovementBPFL::SeinHasMovementInput(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	FSeinMovementStateData State;
	SeinGetMovementState(WorldContextObject, EntityHandle, State);
	return State.bHasMovementInput;
}

float USeinMovementBPFL::SeinGetMovementRenderValue(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, int32 Slot)
{
	FFixedPoint Value = FFixedPoint::Zero;
	return GetMovementRenderValueFixed(
		WorldContextObject, EntityHandle, Slot, Value)
		? Value.ToFloat()
		: 0.0f;
}

bool USeinMovementBPFL::GetMovementRenderValueFixed(
	const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle,
	int32 Slot,
	FFixedPoint& OutValue)
{
	OutValue = FFixedPoint::Zero;
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem)
	{
		return false;
	}
	const FSeinMovementComponent* MoveData =
		Subsystem->GetComponent<FSeinMovementComponent>(EntityHandle);
	if (!MoveData || !MoveData->RenderState.IsValidIndex(Slot))
	{
		return false;
	}
	OutValue = MoveData->RenderState[Slot];
	return true;
}

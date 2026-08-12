/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementBPFL.h
 * @brief   Blueprint Function Library for derived movement state. Composes
 *          an entity's transform + FSeinMovementComponent (+ optional
 *          polymorphic per-class sub-data for altitude) into a
 *          CharacterMovement-style snapshot for AnimBPs / UI / gameplay.
 *
 *          Raw component reads are intentionally NOT exposed here — designers
 *          use the generic typed `Get Component` K2 node
 *          (`K2Node_SeinGetComponent`) for `FSeinMovementComponent` +
 *          `FSeinNavigationComponent`. This BPFL only exists to centralize
 *          the field composition (Velocity → Speed/Direction/etc) that would
 *          otherwise be duplicated across consumers.
 *
 *          Writes to `FSeinMovementComponent` (during simulation only) go
 *          through generic `K2Node_SeinSetComponent` or the entity bridge's
 *          ComponentData authoring at design time.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "SeinMovementBPFL.generated.h"

class USeinWorldSubsystem;

/**
 * Derived movement state — read-only runtime snapshot composed from an
 * entity's transform and FSeinMovementComponent (+ optional polymorphic
 * sub-data for altitude). Mirrors the field shape of
 * UCharacterMovementComponent so AnimBPs / UI / gameplay code authored
 * against the mannequin template port over with minimal rewiring.
 *
 * Stateless by design: no per-frame deltas (acceleration, yaw rate). If
 * you need those, compute them in the calling graph from per-tick
 * Velocity / rotation deltas with whatever cache strategy fits the
 * caller (BP variable, ViewModel, etc.). Keeping this struct stateless
 * means a single sim entity can be queried by N consumers per frame
 * without coordination.
 */
USTRUCT(BlueprintType)
struct SEINARTSMOVEMENT_API FSeinMovementStateData
{
	GENERATED_BODY()

	/** Linear velocity (world-space, units/s). For non-strafing movement
	 *  this is `Forward × Speed` — sign carries reverse direction. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|State")
	FVector Velocity = FVector::ZeroVector;

	/** Signed forward speed (units/s). Negative when reversing. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|State")
	float Speed = 0.0f;

	/** Planar (XY) speed magnitude. Non-negative; equivalent to
	 *  `Vector Length XY(Velocity)` from the mannequin AnimBP template. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|State")
	float GroundSpeed = 0.0f;

	/** Signed angle from the entity's facing to its velocity, in degrees,
	 *  in [-180, 180]. Drives the strafe blendspace. For non-strafing
	 *  movement: 0 forward, ±180 reverse. Returns 0 when stationary. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|State")
	float Direction = 0.0f;

	/** True when |Speed| exceeds a small move epsilon (1cm/s). AnimBP
	 *  "Should Move" equivalent — drives idle ↔ locomotion transitions. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|State")
	bool bIsMoving = false;

	/** True when Speed is negative — the unit is driving backwards. Distinct
	 *  from `bIsMoving`: a stopped unit isn't reversing even if it could.
	 *  AnimBPs use this to swap forward/backward locomotion blendspace inputs. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|State")
	bool bIsReversing = false;

	/** True when the entity is off the ground (Altitude > 1cm). Sourced
	 *  from the polymorphic sub-data on `FSeinMovementComponent::
	 *  MovementClassData` (hover / flight sub-data carries Altitude); false
	 *  for ground movements that have no sub-data Altitude field. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|State")
	bool bIsAirborne = false;

	/** True while an active move action has entered the kinematic brake
	 *  zone — the unit is decelerating toward its final waypoint. Direct
	 *  mirror of `FSeinMovementComponent::bArrivalImminent`. AnimBPs use
	 *  this to blend into "approaching destination" anims. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|State")
	bool bArrivalImminent = false;

	/** True while a move action is actively driving the entity toward a
	 *  destination. Direct mirror of `FSeinMovementComponent::bHasTarget`.
	 *  Goes false the moment the action ends, even while Velocity coasts
	 *  toward zero through the deceleration curve — combine with
	 *  `bIsMoving` for AnimBP "Should Move" gating that releases at input
	 *  release rather than at full kinematic stop. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|State")
	bool bHasMovementInput = false;
};

UCLASS(meta = (DisplayName = "SeinARTS Movement Library"))
class SEINARTSMOVEMENT_API USeinMovementBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Composite read of derived movement state for an entity. Composes
	 *  entity transform + FSeinMovementComponent (+ optional sub-data
	 *  Altitude) into AnimBP-shaped derived values (Velocity, Speed,
	 *  Direction, etc.). Returns false on invalid handle / missing movement
	 *  component (OutState reset to defaults). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Movement State"))
	static bool SeinGetMovementState(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinMovementStateData& OutState);

	// ===== Single-field getters =====
	// Convenience wrappers for call sites that only need one field and
	// don't want to break a struct. Each delegates to Get Movement State.
	// If you read 4+ fields per frame on the same entity, prefer Get
	// Movement State once and break the struct.

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Velocity"))
	static FVector SeinGetVelocity(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Speed"))
	static float SeinGetSpeed(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Ground Speed"))
	static float SeinGetGroundSpeed(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Direction"))
	static float SeinGetDirection(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (WorldContext = "WorldContextObject", DisplayName = "Is Moving"))
	static bool SeinIsMoving(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (WorldContext = "WorldContextObject", DisplayName = "Is Reversing"))
	static bool SeinIsReversing(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (WorldContext = "WorldContextObject", DisplayName = "Is Airborne"))
	static bool SeinIsAirborne(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (WorldContext = "WorldContextObject", DisplayName = "Is Arrival Imminent"))
	static bool SeinIsArrivalImminent(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement", meta = (WorldContext = "WorldContextObject", DisplayName = "Has Movement Input"))
	static bool SeinHasMovementInput(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	/** Reads a custom render/anim value a movement mode wrote at a slot (see Set Render Value on the Sein
	 *  Mover Handle). Returns 0 if the unit has no value at that slot. Drives visuals only. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement",
		meta = (WorldContext = "WorldContextObject",
			DisplayName = "Get Movement Render Value",
			SeinPresentationOnly))
	static float SeinGetMovementRenderValue(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, int32 Slot);

	/** Native presentation read that preserves fixed-point precision until the
	 * caller performs its final render-boundary conversion. */
	static bool GetMovementRenderValueFixed(
		const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle,
		int32 Slot,
		FFixedPoint& OutValue);

	// ===== Movement control =====

	/** Safely terminate the entity's movement — and ONLY its movement. Cancels its active
	 *  USeinMoveToAction (its OnCancelled fires on the move proxy; the unit then coasts to
	 *  rest via the idle driver). Any OTHER latent action on the entity (a channel, a wait,
	 *  a custom primitive) is left running. The RTS "Stop"/"Halt" command; no-op if not moving.
	 *  To cancel more, use Cancel All Actions / Cancel Actions Of Class (SeinARTS Latent Action Library).
	 *  Sim-side: call from an ability / sim context, like Move To. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Stop Movement"))
	static void SeinStopMovement(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};

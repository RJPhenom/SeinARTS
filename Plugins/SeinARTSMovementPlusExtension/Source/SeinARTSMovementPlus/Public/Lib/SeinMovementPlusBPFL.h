/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinMovementPlusBPFL.h
 * @author       RJ Macklem
 * @created      12 Aug 2026
 * @latest       14 Aug 2026
 * @brief        Exposes typed Movement+ vehicle telemetry to presentation
 *               Blueprints without exposing transient render-state slots.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "SeinMovementPlusBPFL.generated.h"

struct FSeinMovementComponent;

/** Selects one field from the typed Movement+ presentation state. */
UENUM(BlueprintType)
enum class ESeinMovementPlusTelemetryChannel : uint8
{
	/** Wheeled steering angle in radians. Positive turns local +X toward +Y. */
	SteeringAngle,

	/** Settled chassis yaw rate in radians per second. Positive rotates +X toward +Y. */
	YawRate,

	/** Driver-output acceleration normalized to the authored acceleration limit, from 0 to 1. */
	NormalizedThrottle,

	/** Driver-output deceleration normalized to the authored deceleration limit, from 0 to 1. */
	NormalizedBrake,

	/** Wrapped wheel phase in radians, derived from settled signed travel. */
	WheelRotation,

	/** Derived signed left-track velocity in centimeters per second. Positive is forward. */
	LeftTrackVelocity,

	/** Derived signed right-track velocity in centimeters per second. Positive is forward. */
	RightTrackVelocity
};

/** Visual vehicle dimensions used to derive wheel and track animation values.
 *  These values affect presentation only and never feed simulation. */
USTRUCT(BlueprintType)
struct SEINARTSMOVEMENTPLUS_API FSeinMovementPlusPresentationDimensions
{
	GENERATED_BODY()

	/** Visual wheel radius in centimeters. Converts settled signed travel to
	 *  wheel phase; a non-positive, non-finite, or effectively zero value
	 *  produces zero wheel rotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "SeinARTS|Movement|Presentation",
		meta = (ClampMin = "0.0", Units = "cm"))
	float WheelRadiusCm = 25.0f;

	/** Distance in centimeters from the vehicle centerline to either track.
	 *  Derives left/right track velocity; a non-positive, non-finite, or
	 *  effectively zero value produces zero for both track velocities. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "SeinARTS|Movement|Presentation",
		meta = (ClampMin = "0.0", Units = "cm"))
	float TrackHalfWidthCm = 50.0f;
};

/** Typed render-only telemetry from the latest settled Movement+ sample.
 *  Unavailable or reset values are zero and never affect canonical state. */
USTRUCT(BlueprintType)
struct SEINARTSMOVEMENTPLUS_API FSeinMovementPlusPresentationState
{
	GENERATED_BODY()

	/** Wheeled steering angle in radians. Positive turns local +X toward +Y;
	 *  tracked movement reports zero. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|Presentation")
	float SteeringAngleRadians = 0.0f;

	/** Settled chassis yaw rate in radians per second. Positive rotates local
	 *  +X toward +Y; zero means no measured yaw or no settled sample. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|Presentation")
	float YawRateRadiansPerSecond = 0.0f;

	/** Driver-output acceleration normalized by the authored acceleration
	 *  limit. The range is 0 to 1; zero means no positive driver-speed change. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|Presentation")
	float NormalizedThrottle = 0.0f;

	/** Driver-output deceleration normalized by the authored deceleration
	 *  limit. The range is 0 to 1; zero means no negative driver-speed change. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|Presentation")
	float NormalizedBrake = 0.0f;

	/** Wheel phase in radians, wrapped to [0, 2*pi) and derived from settled
	 *  accumulated signed travel. Forward travel advances the phase and reverse
	 *  travel reduces it through the wrap. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|Presentation")
	float WheelRotationRadians = 0.0f;

	/** Derived left-track velocity in centimeters per second. Positive is
	 *  forward, negative is reverse, and positive yaw raises this value. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|Presentation")
	float LeftTrackVelocityCmPerSecond = 0.0f;

	/** Derived right-track velocity in centimeters per second. Positive is
	 *  forward, negative is reverse, and positive yaw lowers this value. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|Presentation")
	float RightTrackVelocityCmPerSecond = 0.0f;
};

namespace UE::SeinARTSMovementPlus::Telemetry
{
	inline constexpr int32 SteeringAngleSlot = 0;
	inline constexpr int32 YawRateSlot = 1;
	inline constexpr int32 NormalizedThrottleSlot = 2;
	inline constexpr int32 NormalizedBrakeSlot = 3;
	inline constexpr int32 WheelTravelDistanceSlot = 4;
	inline constexpr int32 SettledForwardSpeedSlot = 5;

	SEINARTSMOVEMENTPLUS_API int32 ChannelToSlot(ESeinMovementPlusTelemetryChannel Channel);
	SEINARTSMOVEMENTPLUS_API void SetRenderValue(FSeinMovementComponent& MovementData, int32 Slot, FFixedPoint Value);
	SEINARTSMOVEMENTPLUS_API void ResetMovementPlusRenderValues(FSeinMovementComponent& MovementData);
	SEINARTSMOVEMENTPLUS_API FFixedPoint Clamp01(FFixedPoint Value);
	SEINARTSMOVEMENTPLUS_API FFixedPoint AccumulateWheelTravel(
		FFixedPoint PreviousTravel,
		FFixedPoint SettledForwardDistance);
}

UCLASS(meta = (DisplayName = "SeinARTS Movement+ Library"))
class SEINARTSMOVEMENTPLUS_API USeinMovementPlusBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Reads every typed Movement+ vehicle animation value for an entity.
	 *  Supply the visual wheel radius and track half-width for the mesh.
	 *  Unavailable fields are zero; Wheeled steering may be available before
	 *  motion-derived fields have two settled samples. This presentation-only
	 *  node cannot be used by deterministic Movement or Ability Blueprints. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Presentation",
		meta = (WorldContext = "WorldContextObject",
			DisplayName = "Get Movement+ Presentation State",
			SeinPresentationOnly))
	static FSeinMovementPlusPresentationState SeinGetMovementPlusPresentationState(
		const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle,
		FSeinMovementPlusPresentationDimensions Dimensions);

	/** Reads one field from Get Movement+ Presentation State. Returns zero when
	 *  the requested telemetry is unavailable. Prefer the full state node when
	 *  an animation update needs more than one channel. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Presentation",
		meta = (WorldContext = "WorldContextObject",
			DisplayName = "Get Movement+ Telemetry Value",
			SeinPresentationOnly))
	static float SeinGetMovementPlusTelemetryValue(
		const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle,
		ESeinMovementPlusTelemetryChannel Channel,
		FSeinMovementPlusPresentationDimensions Dimensions);
};

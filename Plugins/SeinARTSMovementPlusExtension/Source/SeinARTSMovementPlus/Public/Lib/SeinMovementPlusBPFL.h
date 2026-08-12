/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinMovementPlusBPFL.h
 * @brief:   Typed Movement+ presentation telemetry for AnimBPs.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "SeinMovementPlusBPFL.generated.h"

struct FSeinMovementComponent;

UENUM(BlueprintType)
enum class ESeinMovementPlusTelemetryChannel : uint8
{
	SteeringAngle,
	YawRate,
	NormalizedThrottle,
	NormalizedBrake,
	WheelRotation,
	LeftTrackVelocity,
	RightTrackVelocity
};

USTRUCT(BlueprintType)
struct SEINARTSMOVEMENTPLUS_API FSeinMovementPlusPresentationDimensions
{
	GENERATED_BODY()

	/** Visual wheel radius used only to convert settled travel to radians. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "SeinARTS|Movement|Presentation",
		meta = (ClampMin = "0.0", Units = "cm"))
	float WheelRadiusCm = 25.0f;

	/** Visual center-to-track distance used only for left/right velocity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "SeinARTS|Movement|Presentation",
		meta = (ClampMin = "0.0", Units = "cm"))
	float TrackHalfWidthCm = 50.0f;
};

USTRUCT(BlueprintType)
struct SEINARTSMOVEMENTPLUS_API FSeinMovementPlusPresentationState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|Presentation")
	float SteeringAngleRadians = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|Presentation")
	float YawRateRadiansPerSecond = 0.0f;

	/** Driver-output acceleration normalized by the authored acceleration limit. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|Presentation")
	float NormalizedThrottle = 0.0f;

	/** Driver-output deceleration normalized by the authored deceleration limit. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|Presentation")
	float NormalizedBrake = 0.0f;

	/** Settled wheel phase in the range [0, 2*pi). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|Presentation")
	float WheelRotationRadians = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Movement|Presentation")
	float LeftTrackVelocityCmPerSecond = 0.0f;

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
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Movement|Presentation",
		meta = (WorldContext = "WorldContextObject",
			DisplayName = "Get Movement+ Presentation State",
			SeinPresentationOnly))
	static FSeinMovementPlusPresentationState SeinGetMovementPlusPresentationState(
		const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle,
		FSeinMovementPlusPresentationDimensions Dimensions);

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

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinMovementPlusBPFL.cpp
 */

#include "Lib/SeinMovementPlusBPFL.h"

#include "Components/SeinMovementComponent.h"
#include "Lib/SeinMovementBPFL.h"

namespace UE::SeinARTSMovementPlus::Telemetry
{
	int32 ChannelToSlot(ESeinMovementPlusTelemetryChannel Channel)
	{
		switch (Channel)
		{
		case ESeinMovementPlusTelemetryChannel::SteeringAngle: return SteeringAngleSlot;
		case ESeinMovementPlusTelemetryChannel::YawRate: return YawRateSlot;
		case ESeinMovementPlusTelemetryChannel::NormalizedThrottle: return NormalizedThrottleSlot;
		case ESeinMovementPlusTelemetryChannel::NormalizedBrake: return NormalizedBrakeSlot;
		case ESeinMovementPlusTelemetryChannel::WheelRotation:
		case ESeinMovementPlusTelemetryChannel::LeftTrackVelocity:
		case ESeinMovementPlusTelemetryChannel::RightTrackVelocity:
		default: return -1;
		}
	}

	void SetRenderValue(FSeinMovementComponent& MovementData, int32 Slot, FFixedPoint Value)
	{
		if (Slot < 0 || Slot >= 64)
		{
			return;
		}
		if (Slot >= MovementData.RenderState.Num())
		{
			MovementData.RenderState.SetNumZeroed(Slot + 1);
		}
		MovementData.RenderState[Slot] = Value;
	}

	void ResetMovementPlusRenderValues(FSeinMovementComponent& MovementData)
	{
		SetRenderValue(MovementData, SteeringAngleSlot, FFixedPoint::Zero);
		SetRenderValue(MovementData, YawRateSlot, FFixedPoint::Zero);
		SetRenderValue(MovementData, NormalizedThrottleSlot, FFixedPoint::Zero);
		SetRenderValue(MovementData, NormalizedBrakeSlot, FFixedPoint::Zero);
		SetRenderValue(MovementData, WheelTravelDistanceSlot, FFixedPoint::Zero);
		SetRenderValue(MovementData, SettledForwardSpeedSlot, FFixedPoint::Zero);
	}

	FFixedPoint Clamp01(FFixedPoint Value)
	{
		if (Value < FFixedPoint::Zero)
		{
			return FFixedPoint::Zero;
		}
		if (Value > FFixedPoint::One)
		{
			return FFixedPoint::One;
		}
		return Value;
	}

	FFixedPoint AccumulateWheelTravel(
		FFixedPoint PreviousTravel,
		FFixedPoint SettledForwardDistance)
	{
		const FFixedPoint MaxPresentationTravel =
			FFixedPoint::FromInt(1000000000);
		const FFixedPoint MinPresentationTravel =
			-MaxPresentationTravel;
		if (PreviousTravel < MinPresentationTravel
			|| PreviousTravel > MaxPresentationTravel
			|| SettledForwardDistance < MinPresentationTravel
			|| SettledForwardDistance > MaxPresentationTravel)
		{
			return FFixedPoint::Zero;
		}
		if ((SettledForwardDistance > FFixedPoint::Zero
				&& PreviousTravel
					> MaxPresentationTravel - SettledForwardDistance)
			|| (SettledForwardDistance < FFixedPoint::Zero
				&& PreviousTravel
					< MinPresentationTravel - SettledForwardDistance))
		{
			return SettledForwardDistance;
		}
		return PreviousTravel + SettledForwardDistance;
	}
}

FSeinMovementPlusPresentationState USeinMovementPlusBPFL::SeinGetMovementPlusPresentationState(
	const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle,
	FSeinMovementPlusPresentationDimensions Dimensions)
{
	FSeinMovementPlusPresentationState State;
	State.SteeringAngleRadians = USeinMovementBPFL::SeinGetMovementRenderValue(
		WorldContextObject, EntityHandle, UE::SeinARTSMovementPlus::Telemetry::SteeringAngleSlot);
	State.YawRateRadiansPerSecond = USeinMovementBPFL::SeinGetMovementRenderValue(
		WorldContextObject, EntityHandle, UE::SeinARTSMovementPlus::Telemetry::YawRateSlot);
	State.NormalizedThrottle = USeinMovementBPFL::SeinGetMovementRenderValue(
		WorldContextObject, EntityHandle, UE::SeinARTSMovementPlus::Telemetry::NormalizedThrottleSlot);
	State.NormalizedBrake = USeinMovementBPFL::SeinGetMovementRenderValue(
		WorldContextObject, EntityHandle, UE::SeinARTSMovementPlus::Telemetry::NormalizedBrakeSlot);
	FFixedPoint WheelTravelDistance = FFixedPoint::Zero;
	USeinMovementBPFL::GetMovementRenderValueFixed(
		WorldContextObject,
		EntityHandle,
		UE::SeinARTSMovementPlus::Telemetry::WheelTravelDistanceSlot,
		WheelTravelDistance);
	const float SettledForwardSpeed =
		USeinMovementBPFL::SeinGetMovementRenderValue(
			WorldContextObject,
			EntityHandle,
			UE::SeinARTSMovementPlus::Telemetry::SettledForwardSpeedSlot);
	if (FMath::IsFinite(Dimensions.WheelRadiusCm)
		&& Dimensions.WheelRadiusCm > UE_SMALL_NUMBER)
	{
		constexpr double TwoPi = 6.28318530717958647692;
		const double TravelCm =
			static_cast<double>(WheelTravelDistance.Value)
			/ 4294967296.0;
		double Phase = FMath::Fmod(
			TravelCm / static_cast<double>(Dimensions.WheelRadiusCm),
			TwoPi);
		if (Phase < 0.0)
		{
			Phase += TwoPi;
		}
		State.WheelRotationRadians = static_cast<float>(Phase);
	}
	if (FMath::IsFinite(Dimensions.TrackHalfWidthCm)
		&& Dimensions.TrackHalfWidthCm > UE_SMALL_NUMBER)
	{
		const float Differential = State.YawRateRadiansPerSecond
			* Dimensions.TrackHalfWidthCm;
		State.LeftTrackVelocityCmPerSecond =
			SettledForwardSpeed + Differential;
		State.RightTrackVelocityCmPerSecond =
			SettledForwardSpeed - Differential;
	}
	return State;
}

float USeinMovementPlusBPFL::SeinGetMovementPlusTelemetryValue(
	const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle,
	ESeinMovementPlusTelemetryChannel Channel,
	FSeinMovementPlusPresentationDimensions Dimensions)
{
	const FSeinMovementPlusPresentationState State =
		SeinGetMovementPlusPresentationState(
			WorldContextObject, EntityHandle, Dimensions);
	switch (Channel)
	{
	case ESeinMovementPlusTelemetryChannel::SteeringAngle:
		return State.SteeringAngleRadians;
	case ESeinMovementPlusTelemetryChannel::YawRate:
		return State.YawRateRadiansPerSecond;
	case ESeinMovementPlusTelemetryChannel::NormalizedThrottle:
		return State.NormalizedThrottle;
	case ESeinMovementPlusTelemetryChannel::NormalizedBrake:
		return State.NormalizedBrake;
	case ESeinMovementPlusTelemetryChannel::WheelRotation:
		return State.WheelRotationRadians;
	case ESeinMovementPlusTelemetryChannel::LeftTrackVelocity:
		return State.LeftTrackVelocityCmPerSecond;
	case ESeinMovementPlusTelemetryChannel::RightTrackVelocity:
		return State.RightTrackVelocityCmPerSecond;
	default:
		return 0.0f;
	}
}

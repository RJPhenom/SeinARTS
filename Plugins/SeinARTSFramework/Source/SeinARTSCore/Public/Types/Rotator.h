/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		Rotator.h
 * @date:		1/16/2026
 * @author:		RJ Macklem
 * @brief:		Deterministic rotation using fixed-point angles for 
 * 				lockstep simulation.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "FixedPoint.h"
#include "Quat.h"
#include "Math/MathLib.h"
#include "Rotator.generated.h"

/** Deterministic rotation using fixed-point angles */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCORE_API FFixedRotator
{
	GENERATED_BODY()

	/** Pitch in fixed-point degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedPoint Pitch;

	/** Yaw in fixed-point degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedPoint Yaw;

	/** Roll in fixed-point degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedPoint Roll;

	// Constructors
	FFixedRotator() : Pitch(0), Yaw(0), Roll(0) {}
	FFixedRotator(FFixedPoint InPitch, FFixedPoint InYaw, FFixedPoint InRoll) : Pitch(InPitch), Yaw(InYaw), Roll(InRoll) {}

	// ZeroRotator Constant
	static const FFixedRotator ZeroRotator;

	// Operators
	// ================================================================================================================================

	// Arithmetic operators
	FORCEINLINE FFixedRotator operator+(const FFixedRotator& Other) const { return FFixedRotator(Pitch + Other.Pitch, Yaw + Other.Yaw, Roll + Other.Roll); }
	FORCEINLINE FFixedRotator operator-(const FFixedRotator& Other) const { return FFixedRotator(Pitch - Other.Pitch, Yaw - Other.Yaw, Roll - Other.Roll); }
	FORCEINLINE FFixedRotator operator*(FFixedPoint Scale) const { return FFixedRotator(Pitch * Scale, Yaw * Scale, Roll * Scale); }

	// Comparison operators
	FORCEINLINE bool operator==(const FFixedRotator& Other) const { return Pitch == Other.Pitch && Yaw == Other.Yaw && Roll == Other.Roll; }
	FORCEINLINE bool operator!=(const FFixedRotator& Other) const { return Pitch != Other.Pitch || Yaw != Other.Yaw || Roll != Other.Roll; }

	// Math
	// ===========================================================================================

	FORCEINLINE bool IsZero() const { return Pitch == 0 && Yaw == 0 && Roll == 0; }
	FORCEINLINE FFixedRotator GetInverse() const { return FFixedRotator(-Pitch, -Yaw, -Roll); }

	// Normalize angles to [-180, 180] using fixed-point
	FORCEINLINE FFixedRotator GetNormalized() const
	{
		auto NormalizeAxis = [](FFixedPoint Angle) -> FFixedPoint
		{
			const int64 Deg180 = 180 * FFixedPoint::One;
			const int64 Deg360 = 360 * FFixedPoint::One;
			
			int64 Val = Angle;
			while (Val > Deg180) Val -= Deg360;
			while (Val < -Deg180) Val += Deg360;
			return FFixedPoint(Val);
		};
		
		return FFixedRotator(
			NormalizeAxis(Pitch),
			NormalizeAxis(Yaw),
			NormalizeAxis(Roll)
		);
	}

	// Clamp each axis to reasonable ranges
	FORCEINLINE FFixedRotator Clamp() const
	{
		// Clamp to +/- 10 million degrees (plenty of range for any rotation)
		const int64 MaxDegrees = 10000000LL * (FFixedPoint::One.Value);
		return FFixedRotator(
			FFixedPoint(SeinMath::Clamp(static_cast<int64>(Pitch), -MaxDegrees, MaxDegrees)),
			FFixedPoint(SeinMath::Clamp(static_cast<int64>(Yaw), -MaxDegrees, MaxDegrees)),
			FFixedPoint(SeinMath::Clamp(static_cast<int64>(Roll), -MaxDegrees, MaxDegrees))
		);
	}
	
	// Static Math utilities
	// ===========================================================================================================

	// Factory methods
	FORCEINLINE static FFixedRotator FromRotator(const FRotator& R)
	{
		return FFixedRotator(
			FFixedPoint::FromFloat(R.Pitch),
			FFixedPoint::FromFloat(R.Yaw),
			FFixedPoint::FromFloat(R.Roll)
		);
	}
	
	// Convert to quaternion using fixed-point trig
	FORCEINLINE FFixedQuaternion Quaternion() const
	{
		// Convert degrees to radians
		const FFixedPoint RollRad = Roll * FFixedPoint::DegToRad;
		const FFixedPoint PitchRad = Pitch * FFixedPoint::DegToRad;
		const FFixedPoint YawRad = Yaw * FFixedPoint::DegToRad;
		
		return FFixedQuaternion::MakeFromEulers(FFixedVector(RollRad, PitchRad, YawRad));
	}

	// Get direction vectors using quaternion rotation
	FORCEINLINE FFixedVector GetForwardVector() const
	{
		return Quaternion().GetForwardVector();
	}

	FORCEINLINE FFixedVector GetRightVector() const
	{
		return Quaternion().GetRightVector();
	}

	FORCEINLINE FFixedVector GetUpVector() const
	{
		return Quaternion().GetUpVector();
	}

	FORCEINLINE FFixedVector GetBackwardVector() const
	{
		return FFixedVector::ZeroVector - GetForwardVector();
	}

	FORCEINLINE FFixedVector GetLeftVector() const
	{
		return FFixedVector::ZeroVector - GetRightVector();
	}

	FORCEINLINE FFixedVector GetDownVector() const
	{
		return FFixedVector::ZeroVector - GetUpVector();
	}

	// Rotate a vector by this rotator
	FORCEINLINE FFixedVector RotateVector(const FFixedVector& V) const
	{
		return Quaternion().RotateVector(V);
	}

	// Unrotate a vector (apply inverse rotation)
	FORCEINLINE FFixedVector UnrotateVector(const FFixedVector& V) const
	{
		return Quaternion().Inverse().RotateVector(V);
	}

	// Linear interpolation
	FORCEINLINE static FFixedRotator Lerp(const FFixedRotator& A, const FFixedRotator& B, FFixedPoint Alpha)
	{
		return FFixedRotator(
			A.Pitch + (B.Pitch - A.Pitch) * Alpha,
			A.Yaw + (B.Yaw - A.Yaw) * Alpha,
			A.Roll + (B.Roll - A.Roll) * Alpha
		);
	}

	// Conversion
	FORCEINLINE FRotator ToRotator() const	{ return FRotator(Pitch.ToFloat(), Yaw.ToFloat(), Roll.ToFloat()); }
	FORCEINLINE FString ToString() const { return FString::Printf(TEXT("P=%.2f Y=%.2f R=%.2f"), Pitch.ToFloat(), Yaw.ToFloat(), Roll.ToFloat()); }
};

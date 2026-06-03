/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		Quat.h
 * @date:		1/16/2026
 * @author:		RJ Macklem
 * @brief:		Deterministic quaternion using fixed-point arithmetic for 
 * 				lockstep simulation.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "FixedPoint.h"
#include "Vector.h"
#include "Math/MathLib.h"
#include "Quat.generated.h"

struct FFixedRotator;

/** Deterministic quaternion using fixed-point arithmetic */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCORE_API FFixedQuaternion
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedPoint X;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedPoint Y;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedPoint Z;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedPoint W;

	// Constructors
	FFixedQuaternion() : X(0), Y(0), Z(0), W(FFixedPoint::One) {}  // Identity (W = 1.0 in fixed-point)
	FFixedQuaternion(FFixedPoint InX, FFixedPoint InY, FFixedPoint InZ, FFixedPoint InW) : X(InX), Y(InY), Z(InZ), W(InW) {}

	// Identity quaternion constant
	static const FFixedQuaternion Identity;

	// Operators
	// ================================================================================================================================

	// Quaternion multiplication
	FORCEINLINE FFixedQuaternion operator*(const FFixedQuaternion& Other) const
	{
		return FFixedQuaternion(
			W * Other.X + X * Other.W + Y * Other.Z - Z * Other.Y,
			W * Other.Y - X * Other.Z + Y * Other.W + Z * Other.X,
			W * Other.Z + X * Other.Y - Y * Other.X + Z * Other.W,
			W * Other.W - X * Other.X - Y * Other.Y - Z * Other.Z
		);
	}

	// Comparison operators
	FORCEINLINE bool operator==(const FFixedQuaternion& Other) const { return X == Other.X && Y == Other.Y && Z == Other.Z && W == Other.W; }
	FORCEINLINE bool operator!=(const FFixedQuaternion& Other) const { return X != Other.X || Y != Other.Y || Z != Other.Z || W != Other.W; }

	// Math
	// ===========================================================================================

	// Quaternion operations
	FORCEINLINE FFixedPoint SizeSquared() const { return X * X + Y * Y + Z * Z + W * W; }
	FORCEINLINE FFixedVector RotateVector(const FFixedVector& V) const
	{
		// qvq* using quaternion sandwich product
		const FFixedVector Q(X, Y, Z);
		const FFixedVector T = (Q ^ V) * FFixedPoint::Two;
		return V + (T * W) + (Q ^ T);
	}

	// Get direction vectors from quaternion
	FORCEINLINE FFixedVector GetForwardVector() const
	{
		return RotateVector(FFixedVector::ForwardVector);
	}

	FORCEINLINE FFixedVector GetRightVector() const
	{
		return RotateVector(FFixedVector::RightVector);
	}

	FORCEINLINE FFixedVector GetUpVector() const
	{
		return RotateVector(FFixedVector::UpVector);
	}

	// To Rotator conversion
	FFixedRotator Rotator() const;

	// Factory methods
	FORCEINLINE static FFixedQuaternion FromQuat(const FQuat& Q)
	{
		return FFixedQuaternion(
			FFixedPoint::FromFloat(Q.X),
			FFixedPoint::FromFloat(Q.Y),
			FFixedPoint::FromFloat(Q.Z),
			FFixedPoint::FromFloat(Q.W)
		);
	}

	FORCEINLINE static FQuat ToQuat(const FFixedQuaternion& Q)
	{
		return FQuat(
			Q.X.ToFloat(),
			Q.Y.ToFloat(),
			Q.Z.ToFloat(),
			Q.W.ToFloat()
		);
	}

	// Create from axis and angle (Axis should be normalized, Angle in radians)
	FORCEINLINE static FFixedQuaternion FromAxisAndAngle(const FFixedVector& Axis, FFixedPoint AngleRad)
	{
		const FFixedPoint HalfAngle = AngleRad * FFixedPoint::Half;
		const FFixedPoint SinHalf = SeinMath::Sin(HalfAngle);
		const FFixedPoint CosHalf = SeinMath::Cos(HalfAngle);
		
		return FFixedQuaternion(
			Axis.X * SinHalf,
			Axis.Y * SinHalf,
			Axis.Z * SinHalf,
			CosHalf
		);
	}

	// Get rotation axis and angle
	FORCEINLINE void ToAxisAndAngle(FFixedVector& OutAxis, FFixedPoint& OutAngle) const
	{
		const FFixedPoint SinSq = X * X + Y * Y + Z * Z;
		if (SinSq > FFixedPoint::Epsilon)
		{
			const FFixedPoint Scale = FFixedPoint::One / SeinMath::Sqrt(SinSq);
			OutAxis = FFixedVector(X * Scale, Y * Scale, Z * Scale);
			OutAngle = FFixedPoint::Two * SeinMath::Atan2(SeinMath::Sqrt(SinSq), W);
		}
		else
		{
			// Near identity quaternion
			OutAxis = FFixedVector(FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::Zero);
			OutAngle = FFixedPoint::Zero;
		}
	}

	// Create from Euler angles (Roll, Pitch, Yaw in radians)
	FORCEINLINE static FFixedQuaternion MakeFromEulers(const FFixedVector& Euler)
	{
		const FFixedPoint Roll = Euler.X;
		const FFixedPoint Pitch = Euler.Y;
		const FFixedPoint Yaw = Euler.Z;
		
		const FFixedPoint HalfRoll = Roll * FFixedPoint::Half;
		const FFixedPoint HalfPitch = Pitch * FFixedPoint::Half;
		const FFixedPoint HalfYaw = Yaw * FFixedPoint::Half;
		
		const FFixedPoint SR = SeinMath::Sin(HalfRoll);
		const FFixedPoint CR = SeinMath::Cos(HalfRoll);
		const FFixedPoint SP = SeinMath::Sin(HalfPitch);
		const FFixedPoint CP = SeinMath::Cos(HalfPitch);
		const FFixedPoint SY = SeinMath::Sin(HalfYaw);
		const FFixedPoint CY = SeinMath::Cos(HalfYaw);
		
		return FFixedQuaternion(
			CR * SP * SY - SR * CP * CY,  // X
			-CR * SP * CY - SR * CP * SY,  // Y (negated for Unreal's left-handed coordinate system)
			CR * CP * SY - SR * SP * CY,  // Z
			CR * CP * CY + SR * SP * SY   // W
		);
	}

	// Get Euler angles from quaternion (returns Roll, Pitch, Yaw in radians)
	FORCEINLINE FFixedVector Eulers() const
	{
		// Singularity at north and south pole
		const FFixedPoint Singularity = X * Z - W * Y;
		const FFixedPoint SingularityThreshold = FFixedPoint(FFixedPoint::Half - 100);
		
		FFixedPoint Roll, Pitch, Yaw;
		
		if (Singularity > SingularityThreshold)
		{
			// North pole singularity
			Yaw = FFixedPoint::Two * SeinMath::Atan2(X, W);
			Pitch = FFixedPoint::HalfPi;
			Roll = FFixedPoint::Zero;
		}
		else if (Singularity < -SingularityThreshold)
		{
			// South pole singularity
			Yaw = -FFixedPoint::Two * SeinMath::Atan2(X, W);
			Pitch = -FFixedPoint::HalfPi;
			Roll = FFixedPoint::Zero;
		}
		else
		{
			Yaw = SeinMath::Atan2(FFixedPoint::Two * (W * Z + X * Y), FFixedPoint::One - FFixedPoint::Two * (Y * Y + Z * Z));
			Pitch = SeinMath::Asin(FFixedPoint::Two * Singularity);
			Roll = SeinMath::Atan2(FFixedPoint::Two * (W * X + Y * Z), FFixedPoint::One - FFixedPoint::Two * (X * X + Y * Y));
		}
		
		return FFixedVector(Roll, Pitch, Yaw);
	}

	// Spherical linear interpolation
	FORCEINLINE static FFixedQuaternion Slerp(const FFixedQuaternion& Q1, const FFixedQuaternion& Q2, FFixedPoint Alpha)
	{
		// Calculate cosine of angle between quaternions
		FFixedPoint CosOmega = Q1.X * Q2.X + Q1.Y * Q2.Y + Q1.Z * Q2.Z + Q1.W * Q2.W;
		
		// Use the shorter path
		FFixedQuaternion Q2Adjusted = Q2;
		if (CosOmega < 0)
		{
			CosOmega = FFixedPoint(-CosOmega);
			Q2Adjusted = FFixedQuaternion(FFixedPoint(-Q2.X), FFixedPoint(-Q2.Y), FFixedPoint(-Q2.Z), FFixedPoint(-Q2.W));
		}
		
		// If quaternions are very close, use linear interpolation
		if (CosOmega > (FFixedPoint::One - FFixedPoint::Epsilon))
		{
			return FastLerp(Q1, Q2Adjusted, Alpha).GetNormalized();
		}
		
		// Calculate slerp values
		const FFixedPoint Omega = SeinMath::Acos(CosOmega);
		const FFixedPoint SinOmega = SeinMath::Sin(Omega);
		
		if (SinOmega == 0)
		{
			return Q1; // Avoid division by zero
		}
		
		const FFixedPoint A = SeinMath::Sin((FFixedPoint::One - Alpha) * Omega) / SinOmega;
		const FFixedPoint B = SeinMath::Sin(Alpha * Omega) / SinOmega;
		
		return FFixedQuaternion(
			Q1.X * A + Q2Adjusted.X * B,
			Q1.Y * A + Q2Adjusted.Y * B,
			Q1.Z * A + Q2Adjusted.Z * B,
			Q1.W * A + Q2Adjusted.W * B
		);
	}

	// Fast linear interpolation (not normalized)
	FORCEINLINE static FFixedQuaternion FastLerp(const FFixedQuaternion& A, const FFixedQuaternion& B, FFixedPoint Alpha)
	{
		const FFixedPoint OneMinusAlpha = FFixedPoint::One - Alpha;
		return FFixedQuaternion(
			A.X * OneMinusAlpha + B.X * Alpha,
			A.Y * OneMinusAlpha + B.Y * Alpha,
			A.Z * OneMinusAlpha + B.Z * Alpha,
			A.W * OneMinusAlpha + B.W * Alpha
		);
	}

	// Get inverse quaternion (conjugate - only correct for unit quaternions)
	FORCEINLINE FFixedQuaternion Inverse() const
	{
		return FFixedQuaternion(-X, -Y, -Z, W);
	}

	// Get proper inverse (q^-1 = q* / |q|^2)
	FORCEINLINE FFixedQuaternion GetInverse() const
	{
		const FFixedPoint SizeSq = SizeSquared();
		if (SizeSq > FFixedPoint::Epsilon)
		{
			const FFixedPoint InvSizeSq = FFixedPoint::One / SizeSq;
			return FFixedQuaternion(-X * InvSizeSq, -Y * InvSizeSq, -Z * InvSizeSq, W * InvSizeSq);
		}
		return Identity;
	}

	// Normalize in place
	FORCEINLINE void Normalize()
	{
		*this = GetNormalized();
	}

	// Get normalized quaternion
	FORCEINLINE FFixedQuaternion GetNormalized() const
	{
		const int64 XSq = static_cast<int64>(X) * X;
		const int64 YSq = static_cast<int64>(Y) * Y;
		const int64 ZSq = static_cast<int64>(Z) * Z;
		const int64 WSq = static_cast<int64>(W) * W;
		int64 SizeSq = XSq + YSq + ZSq + WSq;
		
		if (SizeSq <= FFixedPoint::Epsilon)
		{
			return Identity;
		}
		
		// Integer square root
		int64 Bit = 1LL << 62;
		while (Bit > SizeSq) Bit >>= 2;
		int64 Root = 0;
		while (Bit != 0)
		{
			if (SizeSq >= Root + Bit)
			{
				SizeSq -= Root + Bit;
				Root = (Root >> 1) + Bit;
			}
			else
			{
				Root >>= 1;
			}
			Bit >>= 2;
		}
		
		const FFixedPoint Length(static_cast<int64>(Root << 16));
		if (Length != 0)
		{
			return FFixedQuaternion(X / Length, Y / Length, Z / Length, W / Length);
		}
		return Identity;
	}

	// Get axis vectors
	FORCEINLINE FFixedVector GetAxisX() const
	{
		return RotateVector(FFixedVector::ForwardVector);
	}

	FORCEINLINE FFixedVector GetAxisY() const
	{
		return RotateVector(FFixedVector::RightVector);
	}

	FORCEINLINE FFixedVector GetAxisZ() const
	{
		return RotateVector(FFixedVector::UpVector);
	}

	// Conversion
	FORCEINLINE FQuat ToQuat() const { return FQuat(X.ToFloat(), Y.ToFloat(), Z.ToFloat(), W.ToFloat()); }
	FORCEINLINE FString ToString() const { return FString::Printf(TEXT("X=%.3f Y=%.3f Z=%.3f W=%.3f"), X.ToFloat(), Y.ToFloat(), Z.ToFloat(), W.ToFloat()); }
};

/** Hash function for FFixedQuaternion */
FORCEINLINE uint32 GetTypeHash(const FFixedQuaternion& Q)
{
	uint32 Hash = GetTypeHash(Q.X);
	Hash = HashCombine(Hash, GetTypeHash(Q.Y));
	Hash = HashCombine(Hash, GetTypeHash(Q.Z));
	Hash = HashCombine(Hash, GetTypeHash(Q.W));
	return Hash;
}


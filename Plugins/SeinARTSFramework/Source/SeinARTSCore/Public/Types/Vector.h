/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		Vector.h
 * @date:		1/16/2026
 * @author:		RJ Macklem
 * @brief:		Deterministic 3D vector using fixed-point arithmetic for 
 * 				lockstep simulation.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "FixedPoint.h"
#include "Math/MathLib.h"
#include "Vector.generated.h"

/** Deterministic 3D vector using fixed-point arithmetic */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCORE_API FFixedVector
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedPoint X;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedPoint Y;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedPoint Z;

	FFixedVector() : X(0), Y(0), Z(0) {}
	FFixedVector(FFixedPoint InX, FFixedPoint InY, FFixedPoint InZ) : X(InX), Y(InY), Z(InZ) {}

	// Operators
	// ================================================================================================================================

	// Arithmetic operators
	FORCEINLINE FFixedVector operator+(const FFixedVector& Other) const { return FFixedVector(X + Other.X, Y + Other.Y, Z + Other.Z); }
	FORCEINLINE FFixedVector operator-(const FFixedVector& Other) const { return FFixedVector(X - Other.X, Y - Other.Y, Z - Other.Z); }
	FORCEINLINE FFixedVector operator-() const { return FFixedVector(-X, -Y, -Z); }
	FORCEINLINE FFixedVector operator*(FFixedPoint Scale) const { return FFixedVector(X * Scale, Y * Scale, Z * Scale); }
	FORCEINLINE FFixedVector operator*(const FFixedVector& Other) const { return FFixedVector(X * Other.X, Y * Other.Y, Z * Other.Z); }
	FORCEINLINE FFixedVector operator/(FFixedPoint Divisor) const { return FFixedVector(X / Divisor, Y / Divisor, Z / Divisor); }
	FORCEINLINE FFixedVector operator/(const FFixedVector& Other) const { return FFixedVector(X / Other.X, Y / Other.Y, Z / Other.Z); }

	// Comparison operators
	FORCEINLINE bool operator==(const FFixedVector& Other) const { return X == Other.X && Y == Other.Y && Z == Other.Z; }
	FORCEINLINE bool operator!=(const FFixedVector& Other) const { return X != Other.X || Y != Other.Y || Z != Other.Z; }

	// Dot product
	FORCEINLINE FFixedPoint operator|(const FFixedVector& Other) const { return X * Other.X + Y * Other.Y + Z * Other.Z; }

	// Cross product
	FORCEINLINE FFixedVector operator^(const FFixedVector& Other) const
	{
		return FFixedVector(
			Y * Other.Z - Z * Other.Y,
			Z * Other.X - X * Other.Z,
			X * Other.Y - Y * Other.X
		);
	}

	// Assignment operators
	FORCEINLINE FFixedVector& operator+=(const FFixedVector& Other) { X += Other.X; Y += Other.Y; Z += Other.Z; return *this; }
	FORCEINLINE FFixedVector& operator-=(const FFixedVector& Other) { X -= Other.X; Y -= Other.Y; Z -= Other.Z; return *this; }
	FORCEINLINE FFixedVector& operator*=(FFixedPoint Scale) { X *= Scale; Y *= Scale; Z *= Scale; return *this; }
	FORCEINLINE FFixedVector& operator/=(FFixedPoint Divisor) { X /= Divisor; Y /= Divisor; Z /= Divisor; return *this; }

	// Math
	// ===========================================================================================

	// Vector operations
	FORCEINLINE FFixedPoint SizeSquared() const { return X * X + Y * Y + Z * Z; }
	FORCEINLINE FFixedPoint Size() const 
	{ 
		// Calculate magnitude using integer square root
		// X, Y, Z are in 32.32 format, so X*X is 64.64, but stored in int64 (overflow risk)
		// Use MathLib Sqrt for consistency
		return SeinMath::Sqrt(SizeSquared());
	}

	FORCEINLINE bool IsZero() const { return X == 0 && Y == 0 && Z == 0; }
	FORCEINLINE bool IsNearlyZero(FFixedPoint Tolerance = FFixedPoint(100)) const 
	{ 
		return (X < 0 ? -X : X) <= Tolerance && 
		       (Y < 0 ? -Y : Y) <= Tolerance &&
		       (Z < 0 ? -Z : Z) <= Tolerance; 
	}

	// Normalization (instance methods)
	FORCEINLINE FFixedVector GetNormalized(FFixedPoint Tolerance = FFixedPoint::Epsilon) const
	{
		return GetSafeNormal(*this, Tolerance);
	}

	FORCEINLINE void Normalize(FFixedPoint Tolerance = FFixedPoint::Epsilon)
	{
		*this = GetNormalized(Tolerance);
	}

	FORCEINLINE bool IsNormalized(FFixedPoint Tolerance = FFixedPoint::Epsilon) const
	{
		const FFixedPoint SizeSq = SizeSquared();
		return (SizeSq - FFixedPoint::One).IsNearlyEqual(FFixedPoint::Zero, Tolerance);
	}

	// Size clamping (instance methods)
	FORCEINLINE FFixedVector GetClampedToSize(FFixedPoint Min, FFixedPoint Max) const
	{
		return ClampSize(*this, Min, Max);
	}

	FORCEINLINE FFixedVector GetClampedToMaxSize(FFixedPoint MaxSize) const
	{
		return ClampSize(*this, FFixedPoint::Zero, MaxSize);
	}

	// Factory methods
	FORCEINLINE static FFixedVector FromVector(const FVector& V) 
	{ 
		return FFixedVector(FFixedPoint::FromFloat(V.X), FFixedPoint::FromFloat(V.Y), FFixedPoint::FromFloat(V.Z)); 
	}

	// Static Math utilities
	// ===========================================================================================================
	
	FORCEINLINE static FFixedPoint Distance(const FFixedVector& A, const FFixedVector& B) { return (B - A).Size(); }
	FORCEINLINE static FFixedPoint DistSquared(const FFixedVector& A, const FFixedVector& B) { return (B - A).SizeSquared(); }
	FORCEINLINE static FFixedPoint DotProduct(const FFixedVector& A, const FFixedVector& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
	FORCEINLINE static FFixedVector CrossProduct(const FFixedVector& A, const FFixedVector& B)
	{
		return FFixedVector(
			A.Y * B.Z - A.Z * B.Y,
			A.Z * B.X - A.X * B.Z,
			A.X * B.Y - A.Y * B.X
		);
	}

	// Normalization
	FORCEINLINE static FFixedVector GetSafeNormal(const FFixedVector& V, FFixedPoint Tolerance = FFixedPoint::Epsilon)
	{
		const FFixedPoint SizeSq = V.SizeSquared();
		if (SizeSq > Tolerance)
		{
			const FFixedPoint VSize = V.Size();
			if (VSize != 0)
			{
				return FFixedVector(V.X / VSize, V.Y / VSize, V.Z / VSize);
			}
		}
		return FFixedVector::ZeroVector;
	}

	// Linear interpolation
	FORCEINLINE static FFixedVector Lerp(const FFixedVector& A, const FFixedVector& B, FFixedPoint Alpha)
	{
		return A + (B - A) * Alpha;
	}

	// Component-wise operations
	FORCEINLINE static FFixedVector ComponentMin(const FFixedVector& A, const FFixedVector& B)
	{
		return FFixedVector(
			(A.X < B.X) ? A.X : B.X,
			(A.Y < B.Y) ? A.Y : B.Y,
			(A.Z < B.Z) ? A.Z : B.Z
		);
	}

	FORCEINLINE static FFixedVector ComponentMax(const FFixedVector& A, const FFixedVector& B)
	{
		return FFixedVector(
			(A.X > B.X) ? A.X : B.X,
			(A.Y > B.Y) ? A.Y : B.Y,
			(A.Z > B.Z) ? A.Z : B.Z
		);
	}

	FORCEINLINE static FFixedVector Abs(const FFixedVector& V)
	{
		return FFixedVector(
			FFixedPoint(V.X < 0 ? -V.X : V.X),
			FFixedPoint(V.Y < 0 ? -V.Y : V.Y),
			FFixedPoint(V.Z < 0 ? -V.Z : V.Z)
		);
	}

	// Projection functions
	FORCEINLINE static FFixedVector ProjectOnTo(const FFixedVector& V, const FFixedVector& Target)
	{
		const FFixedPoint TargetSizeSq = Target.SizeSquared();
		if (TargetSizeSq > 0)
		{
			const FFixedPoint Projection = DotProduct(V, Target);
			return Target * (Projection / TargetSizeSq);
		}
		return FFixedVector::ZeroVector;
	}

	FORCEINLINE static FFixedVector ProjectOnToNormal(const FFixedVector& V, const FFixedVector& Normal)
	{
		return Normal * DotProduct(V, Normal);
	}

	// Clamp vector magnitude
	FORCEINLINE static FFixedVector ClampSize(const FFixedVector& V, FFixedPoint Min, FFixedPoint Max)
	{
		const FFixedPoint VSize = V.Size();
		if (VSize > Max)
		{
			return (V / VSize) * Max;
		}
		else if (VSize < Min && VSize > 0)
		{
			return (V / VSize) * Min;
		}
		return V;
	}

	// Get maximum component
	FORCEINLINE static FFixedPoint GetMax(const FFixedVector& V)
	{
		FFixedPoint MaxVal = V.X;
		if (V.Y > MaxVal) MaxVal = V.Y;
		if (V.Z > MaxVal) MaxVal = V.Z;
		return MaxVal;
	}

	// Get minimum component
	FORCEINLINE static FFixedPoint GetMin(const FFixedVector& V)
	{
		FFixedPoint MinVal = V.X;
		if (V.Y < MinVal) MinVal = V.Y;
		if (V.Z < MinVal) MinVal = V.Z;
		return MinVal;
	}

	// Get absolute maximum component
	FORCEINLINE static FFixedPoint GetAbsMax(const FFixedVector& V)
	{
		const int64 AbsX = V.X < 0 ? -V.X : V.X;
		const int64 AbsY = V.Y < 0 ? -V.Y : V.Y;
		const int64 AbsZ = V.Z < 0 ? -V.Z : V.Z;
		
		int64 MaxVal = AbsX;
		if (AbsY > MaxVal) MaxVal = AbsY;
		if (AbsZ > MaxVal) MaxVal = AbsZ;
		return FFixedPoint(MaxVal);
	}

	// Get absolute minimum component
	FORCEINLINE static FFixedPoint GetAbsMin(const FFixedVector& V)
	{
		const int64 AbsX = V.X < 0 ? -V.X : V.X;
		const int64 AbsY = V.Y < 0 ? -V.Y : V.Y;
		const int64 AbsZ = V.Z < 0 ? -V.Z : V.Z;
		
		int64 MinVal = AbsX;
		if (AbsY < MinVal) MinVal = AbsY;
		if (AbsZ < MinVal) MinVal = AbsZ;
		return FFixedPoint(MinVal);
	}

	// Mirror vector by plane
	FORCEINLINE static FFixedVector MirrorByPlane(const FFixedVector& V, const FFixedVector& PlaneNormal)
	{
		return V - PlaneNormal * (DotProduct(V, PlaneNormal) * FFixedPoint::Two);
	}

	// Reflect vector
	FORCEINLINE static FFixedVector Reflect(const FFixedVector& Direction, const FFixedVector& Normal)
	{
		return Direction - Normal * (DotProduct(Direction, Normal) * FFixedPoint::Two);
	}

	// Reciprocal of vector (component-wise)
	FORCEINLINE static FFixedVector Reciprocal(const FFixedVector& V)
	{
		return FFixedVector(
			V.X != 0 ? FFixedPoint::One / V.X : FFixedPoint::Zero,
			V.Y != 0 ? FFixedPoint::One / V.Y : FFixedPoint::Zero,
			V.Z != 0 ? FFixedPoint::One / V.Z : FFixedPoint::Zero
		);
	}

	// Conversion
	// ===========================================================================================

	FORCEINLINE FVector ToVector() const { return FVector(X.ToFloat(), Y.ToFloat(), Z.ToFloat()); }
	FORCEINLINE FString ToString() const { return FString::Printf(TEXT("X=%.3f Y=%.3f Z=%.3f"), X.ToFloat(), Y.ToFloat(), Z.ToFloat()); }
	
	// Vector Constants (declarations)
	// ===========================================================================================

	static const FFixedVector ZeroVector;
	static const FFixedVector Identity;
	static const FFixedVector UpVector;
	static const FFixedVector DownVector;
	static const FFixedVector ForwardVector;
	static const FFixedVector BackwardVector;
	static const FFixedVector RightVector;
	static const FFixedVector LeftVector;

};

/** Hash function for FFixedVector */
FORCEINLINE uint32 GetTypeHash(const FFixedVector& V)
{
	uint32 Hash = GetTypeHash(V.X);
	Hash = HashCombine(Hash, GetTypeHash(V.Y));
	Hash = HashCombine(Hash, GetTypeHash(V.Z));
	return Hash;
}

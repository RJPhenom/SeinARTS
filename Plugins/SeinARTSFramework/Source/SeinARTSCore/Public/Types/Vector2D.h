/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		Vector2D.h
 * @date:		1/16/2026
 * @author:		RJ Macklem
 * @brief:		Deterministic 2D vector using fixed-point arithmetic for 
 * 				lockstep simulation.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "FixedPoint.h"
#include "Math/MathLib.h"
#include "Vector2D.generated.h"

/** Deterministic 2D vector using fixed-point arithmetic */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCORE_API FFixedVector2D
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedPoint X;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedPoint Y;

	// Constructors
	FFixedVector2D() : X(0), Y(0) {}
	FFixedVector2D(FFixedPoint InX, FFixedPoint InY) : X(InX), Y(InY) {}

	// Operators
	// ================================================================================================================================

	// Arithmetic operators
	FORCEINLINE FFixedVector2D operator+(const FFixedVector2D& Other) const { return FFixedVector2D(X + Other.X, Y + Other.Y); }
	FORCEINLINE FFixedVector2D operator-(const FFixedVector2D& Other) const { return FFixedVector2D(X - Other.X, Y - Other.Y); }
	FORCEINLINE FFixedVector2D operator-() const { return FFixedVector2D(-X, -Y); }
	FORCEINLINE FFixedVector2D operator*(FFixedPoint Scale) const { return FFixedVector2D(X * Scale, Y * Scale); }
	FORCEINLINE FFixedVector2D operator/(FFixedPoint Divisor) const { return FFixedVector2D(X / Divisor, Y / Divisor); }

	// Comparison operators
	FORCEINLINE bool operator==(const FFixedVector2D& Other) const { return X == Other.X && Y == Other.Y; }
	FORCEINLINE bool operator!=(const FFixedVector2D& Other) const { return X != Other.X || Y != Other.Y; }

	// Dot product
	FORCEINLINE FFixedPoint operator|(const FFixedVector2D& Other) const { return X * Other.X + Y * Other.Y; }

	// Assignment operators
	FORCEINLINE FFixedVector2D& operator+=(const FFixedVector2D& Other) { X += Other.X; Y += Other.Y; return *this; }
	FORCEINLINE FFixedVector2D& operator-=(const FFixedVector2D& Other) { X -= Other.X; Y -= Other.Y; return *this; }
	FORCEINLINE FFixedVector2D& operator*=(FFixedPoint Scale) { X *= Scale; Y *= Scale; return *this; }
	FORCEINLINE FFixedVector2D& operator/=(FFixedPoint Divisor) { X /= Divisor; Y /= Divisor; return *this; }

	// Math
	// ===========================================================================================

	// Vector operations
	FORCEINLINE FFixedPoint SizeSquared() const { return X * X + Y * Y; }
	FORCEINLINE FFixedPoint Size() const 
	{ 
		// Calculate magnitude using integer square root
		return SeinMath::Sqrt(SizeSquared());
	}

	FORCEINLINE bool IsZero() const { return X == 0 && Y == 0; }
	FORCEINLINE bool IsNearlyZero(FFixedPoint Tolerance = FFixedPoint(100)) const 
	{ 
		return (X < 0 ? -X : X) <= Tolerance && 
		       (Y < 0 ? -Y : Y) <= Tolerance; 
	}

	// Static Math utilities
	// ===========================================================================================================

	// Factory methods
	FORCEINLINE static FFixedVector2D FromFloat(float InX, float InY) 
	{ 
		return FFixedVector2D(FFixedPoint::FromFloat(InX), FFixedPoint::FromFloat(InY)); 
	}

	FORCEINLINE static FFixedVector2D ZeroVector() { return FFixedVector2D(FFixedPoint::Zero, FFixedPoint::Zero); }
	FORCEINLINE static FFixedVector2D OneVector() { return FFixedVector2D(FFixedPoint::One, FFixedPoint::One); }
	FORCEINLINE static FFixedVector2D UnitVector() { return FFixedVector2D(FFixedPoint::One, FFixedPoint::Zero); }
	FORCEINLINE static FFixedVector2D Unit45Deg() { return FFixedVector2D(FFixedPoint::InvSqrt2, FFixedPoint::InvSqrt2); }

	// Static math utilities
	FORCEINLINE static FFixedPoint Distance(const FFixedVector2D& A, const FFixedVector2D& B) { return (B - A).Size(); }
	FORCEINLINE static FFixedPoint DistSquared(const FFixedVector2D& A, const FFixedVector2D& B) { return (B - A).SizeSquared(); }
	FORCEINLINE static FFixedPoint DotProduct(const FFixedVector2D& A, const FFixedVector2D& B) { return A.X * B.X + A.Y * B.Y; }
	FORCEINLINE static FFixedPoint CrossProduct(const FFixedVector2D& A, const FFixedVector2D& B) { return A.X * B.Y - A.Y * B.X; }

	FORCEINLINE static FFixedVector2D GetSafeNormal(const FFixedVector2D& V, FFixedPoint Tolerance = FFixedPoint::Epsilon)
	{
		const FFixedPoint SizeSq = V.SizeSquared();
		if (SizeSq > Tolerance)
		{
			const FFixedPoint VSize = V.Size();
			if (VSize != 0)
			{
				return FFixedVector2D(V.X / VSize, V.Y / VSize);
			}
		}
		return ZeroVector();
	}

	FORCEINLINE static FFixedVector2D Lerp(const FFixedVector2D& A, const FFixedVector2D& B, FFixedPoint Alpha)
	{
		return A + (B - A) * Alpha;
	}

	FORCEINLINE static FFixedVector2D ComponentMin(const FFixedVector2D& A, const FFixedVector2D& B)
	{
		return FFixedVector2D(
			(A.X < B.X) ? A.X : B.X,
			(A.Y < B.Y) ? A.Y : B.Y
		);
	}

	FORCEINLINE static FFixedVector2D ComponentMax(const FFixedVector2D& A, const FFixedVector2D& B)
	{
		return FFixedVector2D(
			(A.X > B.X) ? A.X : B.X,
			(A.Y > B.Y) ? A.Y : B.Y
		);
	}

	FORCEINLINE static FFixedVector2D GetAbs(const FFixedVector2D& V)
	{
		return FFixedVector2D(
			FFixedPoint(V.X < 0 ? -V.X : V.X),
			FFixedPoint(V.Y < 0 ? -V.Y : V.Y)
		);
	}

	FORCEINLINE static FFixedPoint GetMax(const FFixedVector2D& V)
	{
		return (V.X > V.Y) ? V.X : V.Y;
	}

	FORCEINLINE static FFixedPoint GetMin(const FFixedVector2D& V)
	{
		return (V.X < V.Y) ? V.X : V.Y;
	}

	FORCEINLINE static FFixedPoint GetAbsMax(const FFixedVector2D& V)
	{
		const int32 AbsX = V.X < 0 ? -V.X : V.X;
		const int32 AbsY = V.Y < 0 ? -V.Y : V.Y;
		return FFixedPoint(AbsX > AbsY ? AbsX : AbsY);
	}

	FORCEINLINE static FFixedPoint GetAbsMin(const FFixedVector2D& V)
	{
		const int32 AbsX = V.X < 0 ? -V.X : V.X;
		const int32 AbsY = V.Y < 0 ? -V.Y : V.Y;
		return FFixedPoint(AbsX < AbsY ? AbsX : AbsY);
	}

	// Conversion
	FORCEINLINE FVector2D ToFloat() const { return FVector2D(X.ToFloat(), Y.ToFloat()); }
	FORCEINLINE FString ToString() const { return FString::Printf(TEXT("X=%.3f Y=%.3f"), X.ToFloat(), Y.ToFloat()); }
};

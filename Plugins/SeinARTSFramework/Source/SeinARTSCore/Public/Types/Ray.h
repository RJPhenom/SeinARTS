/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		Ray.h
 * @date:		1/17/2026
 * @author:		RJ Macklem
 * @brief:		Deterministic ray using fixed-point arithmetic.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "FixedPoint.h"
#include "Vector.h"
#include "Math/MathLib.h"
#include "Ray.generated.h"

/**
 * Ray using fixed-point arithmetic.
 * Represents a ray defined by an origin point and a direction vector.
 * Parametric form: Point(t) = Origin + Direction * t, where t >= 0
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCORE_API FFixedRay
{
	GENERATED_BODY()

	/** Origin point of the ray. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedVector Origin;

	/** Direction vector of the ray (should be normalized). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedVector Direction;

	// Constructors
	// ====================================================================================================

	FORCEINLINE FFixedRay()
		: Origin(FFixedVector::ZeroVector)
		, Direction(FFixedVector::ForwardVector)
	{}

	FORCEINLINE FFixedRay(const FFixedVector& InOrigin, const FFixedVector& InDirection)
		: Origin(InOrigin)
		, Direction(FFixedVector::GetSafeNormal(InDirection, FFixedPoint::Epsilon))
	{}

	/** Creates a ray from two points (from Start to End). */
	FORCEINLINE static FFixedRay FromPoints(const FFixedVector& Start, const FFixedVector& End)
	{
		return FFixedRay(Start, End - Start);
	}

	// Queries
	// ====================================================================================================

	/** Returns a point along the ray at parameter t. */
	FORCEINLINE FFixedVector GetPointAt(FFixedPoint T) const
	{
		return Origin + Direction * T;
	}

	/** Returns the closest point on the ray to the given point. */
	FORCEINLINE FFixedVector GetClosestPoint(const FFixedVector& Point) const
	{
		FFixedVector ToPoint = Point - Origin;
		FFixedPoint T = FFixedVector::DotProduct(ToPoint, Direction);
		
		// Clamp T to [0, infinity) - rays don't go backwards
		if (T < FFixedPoint::Zero)
		{
			T = FFixedPoint::Zero;
		}
		
		return GetPointAt(T);
	}

	/** Returns the distance from the ray to a point. */
	FORCEINLINE FFixedPoint GetDistanceToPoint(const FFixedVector& Point) const
	{
		FFixedVector ClosestPoint = GetClosestPoint(Point);
		return (Point - ClosestPoint).Size();
	}

	/** Returns the parameter T for the closest point on the ray to the given point. */
	FORCEINLINE FFixedPoint GetClosestParameter(const FFixedVector& Point) const
	{
		FFixedVector ToPoint = Point - Origin;
		FFixedPoint T = FFixedVector::DotProduct(ToPoint, Direction);
		return SeinMath::Max(T, FFixedPoint::Zero);
	}

	// Modification
	// ====================================================================================================

	/** Translates the ray by a vector. */
	FORCEINLINE FFixedRay& Translate(const FFixedVector& Offset)
	{
		Origin += Offset;
		return *this;
	}

	/** Returns a translated copy of the ray. */
	FORCEINLINE FFixedRay GetTranslated(const FFixedVector& Offset) const
	{
		return FFixedRay(Origin + Offset, Direction);
	}

	// Operators
	// ====================================================================================================

	FORCEINLINE bool operator==(const FFixedRay& Other) const
	{
		return Origin == Other.Origin && Direction == Other.Direction;
	}

	FORCEINLINE bool operator!=(const FFixedRay& Other) const
	{
		return !(*this == Other);
	}

	FORCEINLINE FFixedRay operator+(const FFixedVector& Offset) const
	{
		return GetTranslated(Offset);
	}

	FORCEINLINE FFixedRay& operator+=(const FFixedVector& Offset)
	{
		return Translate(Offset);
	}

	// String Conversion
	// ====================================================================================================

	FORCEINLINE FString ToString() const
	{
		return FString::Printf(TEXT("Ray(Origin: %s, Direction: %s)"), *Origin.ToString(), *Direction.ToString());
	}
};

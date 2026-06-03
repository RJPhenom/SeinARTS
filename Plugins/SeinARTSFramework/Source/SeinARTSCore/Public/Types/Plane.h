/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		Plane.h
 * @date:		1/17/2026
 * @author:		RJ Macklem
 * @brief:		Deterministic plane using fixed-point arithmetic.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "FixedPoint.h"
#include "Vector.h"
#include "Math/MathLib.h"
#include "Plane.generated.h"

/**
 * Plane using fixed-point arithmetic.
 * Defined by a point on the plane and a normal vector.
 * Plane equation: Dot(Normal, Point) = D
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCORE_API FFixedPlane
{
	GENERATED_BODY()

	/** Normal vector of the plane (should be normalized). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedVector Normal;

	/** Distance from origin along the normal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedPoint D;

	// Constructors
	// ====================================================================================================

	FORCEINLINE FFixedPlane()
		: Normal(FFixedVector::UpVector)
		, D(FFixedPoint::Zero)
	{}

	FORCEINLINE FFixedPlane(const FFixedVector& InNormal, FFixedPoint InD)
		: Normal(InNormal)
		, D(InD)
	{}

	/** Creates a plane from a normal and a point on the plane. */
	FORCEINLINE FFixedPlane(const FFixedVector& InNormal, const FFixedVector& PointOnPlane)
		: Normal(FFixedVector::GetSafeNormal(InNormal, FFixedPoint::Epsilon))
		, D(FFixedVector::DotProduct(Normal, PointOnPlane))
	{}

	/** Creates a plane from three points. */
	FORCEINLINE static FFixedPlane FromPoints(const FFixedVector& A, const FFixedVector& B, const FFixedVector& C)
	{
		FFixedVector AB = B - A;
		FFixedVector AC = C - A;
		FFixedVector Normal = FFixedVector::GetSafeNormal(FFixedVector::CrossProduct(AB, AC), FFixedPoint::Epsilon);
		return FFixedPlane(Normal, A);
	}

	/** Converts from Unreal's FPlane. */
	FORCEINLINE static FFixedPlane FromPlane(const FPlane& Plane)
	{
		return FFixedPlane(
			FFixedVector::FromVector(FVector(Plane.X, Plane.Y, Plane.Z)),
			FFixedPoint::FromFloat(Plane.W)
		);
	}

	/** Converts to Unreal's FPlane. */
	FORCEINLINE FPlane ToPlane() const
	{
		return FPlane(Normal.X.ToFloat(), Normal.Y.ToFloat(), Normal.Z.ToFloat(), D.ToFloat());
	}

	// Queries
	// ====================================================================================================

	/** Returns the signed distance from the plane to a point. */
	FORCEINLINE FFixedPoint GetDistanceToPoint(const FFixedVector& Point) const
	{
		return FFixedVector::DotProduct(Normal, Point) - D;
	}

	/** Returns the closest point on the plane to the given point. */
	FORCEINLINE FFixedVector GetClosestPoint(const FFixedVector& Point) const
	{
		FFixedPoint Distance = GetDistanceToPoint(Point);
		return Point - Normal * Distance;
	}

	/** Projects a point onto the plane. */
	FORCEINLINE FFixedVector ProjectPoint(const FFixedVector& Point) const
	{
		return GetClosestPoint(Point);
	}

	/** Projects a vector onto the plane (removes component perpendicular to plane). */
	FORCEINLINE FFixedVector ProjectVector(const FFixedVector& Vector) const
	{
		FFixedPoint DotProduct = FFixedVector::DotProduct(Vector, Normal);
		return Vector - Normal * DotProduct;
	}

	/** Returns true if the point is on the plane (within epsilon tolerance). */
	FORCEINLINE bool IsPointOnPlane(const FFixedVector& Point, FFixedPoint Epsilon = FFixedPoint::Epsilon) const
	{
		FFixedPoint Distance = GetDistanceToPoint(Point);
		return SeinMath::Abs(Distance) <= Epsilon;
	}

	/** Returns true if the point is above the plane (in the direction of the normal). */
	FORCEINLINE bool IsPointAbove(const FFixedVector& Point) const
	{
		return GetDistanceToPoint(Point) > FFixedPoint::Zero;
	}

	/** Returns true if the point is below the plane (opposite the normal direction). */
	FORCEINLINE bool IsPointBelow(const FFixedVector& Point) const
	{
		return GetDistanceToPoint(Point) < FFixedPoint::Zero;
	}

	// Modification
	// ====================================================================================================

	/** Flips the plane (reverses the normal). */
	FORCEINLINE FFixedPlane& Flip()
	{
		Normal = -Normal;
		D = -D;
		return *this;
	}

	/** Returns a flipped copy of the plane. */
	FORCEINLINE FFixedPlane GetFlipped() const
	{
		return FFixedPlane(-Normal, -D);
	}

	/** Translates the plane by a vector. */
	FORCEINLINE FFixedPlane& Translate(const FFixedVector& Offset)
	{
		D += FFixedVector::DotProduct(Normal, Offset);
		return *this;
	}

	/** Returns a translated copy of the plane. */
	FORCEINLINE FFixedPlane GetTranslated(const FFixedVector& Offset) const
	{
		return FFixedPlane(Normal, D + FFixedVector::DotProduct(Normal, Offset));
	}

	// Operators
	// ====================================================================================================

	FORCEINLINE bool operator==(const FFixedPlane& Other) const
	{
		return Normal == Other.Normal && D == Other.D;
	}

	FORCEINLINE bool operator!=(const FFixedPlane& Other) const
	{
		return !(*this == Other);
	}

	// String Conversion
	// ====================================================================================================

	FORCEINLINE FString ToString() const
	{
		return FString::Printf(TEXT("Plane(Normal: %s, D: %s)"), *Normal.ToString(), *D.ToString());
	}
};

/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		Sphere.h
 * @date:		1/17/2026
 * @author:		RJ Macklem
 * @brief:		Deterministic sphere using fixed-point arithmetic.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "FixedPoint.h"
#include "Vector.h"
#include "Math/MathLib.h"
#include "Sphere.generated.h"

/**
 * Sphere using fixed-point arithmetic.
 * Represents a sphere defined by center and radius.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCORE_API FFixedSphere
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedVector Center;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedPoint Radius;

	// Constructors
	// ====================================================================================================

	FORCEINLINE FFixedSphere()
		: Center(FFixedVector::ZeroVector)
		, Radius(FFixedPoint::Zero)
	{}

	FORCEINLINE FFixedSphere(const FFixedVector& InCenter, FFixedPoint InRadius)
		: Center(InCenter)
		, Radius(InRadius)
	{}

	// Factory Methods
	// ====================================================================================================

	/** Converts from Unreal's FSphere. */
	FORCEINLINE static FFixedSphere FromSphere(const FSphere& Sphere)
	{
		return FFixedSphere(
			FFixedVector::FromVector(Sphere.Center),
			FFixedPoint::FromFloat(Sphere.W)
		);
	}

	/** Converts to Unreal's FSphere. */
	FORCEINLINE FSphere ToSphere() const
	{
		return FSphere(Center.ToVector(), Radius.ToFloat());
	}

	// Queries
	// ====================================================================================================

	/** Returns the volume of the sphere (4/3 * Pi * r^3). */
	FORCEINLINE FFixedPoint GetVolume() const
	{
		// Volume = 4/3 * Pi * r^3
		FFixedPoint R3 = Radius * Radius * Radius;
		return (FFixedPoint::Pi * R3 * FFixedPoint(4)) / FFixedPoint(3);
	}

	/** Returns the surface area of the sphere (4 * Pi * r^2). */
	FORCEINLINE FFixedPoint GetSurfaceArea() const
	{
		// Area = 4 * Pi * r^2
		FFixedPoint R2 = Radius * Radius;
		return FFixedPoint::Pi * R2 * FFixedPoint(4);
	}

	/** Returns true if the sphere contains the given point. */
	FORCEINLINE bool Contains(const FFixedVector& Point) const
	{
		return (Point - Center).SizeSquared() <= Radius * Radius;
	}

	/** Returns true if this sphere intersects with another sphere. */
	FORCEINLINE bool Intersects(const FFixedSphere& Other) const
	{
		FFixedPoint CombinedRadius = Radius + Other.Radius;
		return (Center - Other.Center).SizeSquared() <= CombinedRadius * CombinedRadius;
	}

	/** Returns true if this sphere is inside another sphere. */
	FORCEINLINE bool IsInside(const FFixedSphere& Other) const
	{
		FFixedPoint Distance = (Center - Other.Center).Size();
		return Distance + Radius <= Other.Radius;
	}

	/** Returns the distance from the sphere surface to a point (negative if inside). */
	FORCEINLINE FFixedPoint GetDistanceToPoint(const FFixedVector& Point) const
	{
		return (Point - Center).Size() - Radius;
	}

	/** Returns the closest point on the sphere surface to the given point. */
	FORCEINLINE FFixedVector GetClosestPoint(const FFixedVector& Point) const
	{
		FFixedVector Direction = Point - Center;
		FFixedPoint Distance = Direction.Size();
		
		if (Distance <= FFixedPoint::Epsilon)
		{
			// Point is at center, return any point on surface
			return Center + FFixedVector(Radius, FFixedPoint::Zero, FFixedPoint::Zero);
		}
		
		return Center + (Direction / Distance) * Radius;
	}

	// Modification
	// ====================================================================================================

	/** Expands the sphere to include the given point. */
	FORCEINLINE FFixedSphere& ExpandToInclude(const FFixedVector& Point)
	{
		FFixedPoint Distance = (Point - Center).Size();
		if (Distance > Radius)
		{
			// New radius is halfway between current edge and point
			FFixedPoint NewRadius = (Radius + Distance) * FFixedPoint::Half;
			// Move center toward point
			FFixedVector Direction = (Point - Center) / Distance;
			Center += Direction * (NewRadius - Radius);
			Radius = NewRadius;
		}
		return *this;
	}

	/** Expands the sphere to include another sphere. */
	FORCEINLINE FFixedSphere& ExpandToInclude(const FFixedSphere& Other)
	{
		FFixedVector ToOther = Other.Center - Center;
		FFixedPoint Distance = ToOther.Size();
		
		// Check if Other is already inside this sphere
		if (Distance + Other.Radius <= Radius)
		{
			return *this;
		}
		
		// Check if this sphere is inside Other
		if (Distance + Radius <= Other.Radius)
		{
			*this = Other;
			return *this;
		}
		
		// Calculate new sphere that encompasses both
		FFixedPoint NewRadius = (Radius + Distance + Other.Radius) * FFixedPoint::Half;
		FFixedVector Direction = ToOther / Distance;
		Center += Direction * (NewRadius - Radius);
		Radius = NewRadius;
		
		return *this;
	}

	/** Expands the sphere radius by a given amount. */
	FORCEINLINE FFixedSphere& Expand(FFixedPoint Amount)
	{
		Radius += Amount;
		return *this;
	}

	/** Returns an expanded copy of the sphere. */
	FORCEINLINE FFixedSphere GetExpanded(FFixedPoint Amount) const
	{
		return FFixedSphere(Center, Radius + Amount);
	}

	/** Translates the sphere by a vector. */
	FORCEINLINE FFixedSphere& Translate(const FFixedVector& Offset)
	{
		Center += Offset;
		return *this;
	}

	/** Returns a translated copy of the sphere. */
	FORCEINLINE FFixedSphere GetTranslated(const FFixedVector& Offset) const
	{
		return FFixedSphere(Center + Offset, Radius);
	}

	// Operators
	// ====================================================================================================

	FORCEINLINE bool operator==(const FFixedSphere& Other) const
	{
		return Center == Other.Center && Radius == Other.Radius;
	}

	FORCEINLINE bool operator!=(const FFixedSphere& Other) const
	{
		return !(*this == Other);
	}

	FORCEINLINE FFixedSphere operator+(const FFixedVector& Offset) const
	{
		return GetTranslated(Offset);
	}

	FORCEINLINE FFixedSphere& operator+=(const FFixedVector& Offset)
	{
		return Translate(Offset);
	}

	// String Conversion
	// ====================================================================================================

	FORCEINLINE FString ToString() const
	{
		return FString::Printf(TEXT("Sphere(Center: %s, Radius: %s)"), *Center.ToString(), *Radius.ToString());
	}
};

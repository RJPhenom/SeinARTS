/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		Capsule.h
 * @date:		1/17/2026
 * @author:		RJ Macklem
 * @brief:		Deterministic capsule using fixed-point arithmetic.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "FixedPoint.h"
#include "Vector.h"
#include "Math/MathLib.h"
#include "Capsule.generated.h"

/**
 * Capsule using fixed-point arithmetic.
 * Represents a capsule defined by a line segment and radius.
 * The capsule is essentially a sphere swept along a line segment.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCORE_API FFixedCapsule
{
	GENERATED_BODY()

	/** Start point of the capsule's central axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedVector Start;

	/** End point of the capsule's central axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedVector End;

	/** Radius of the capsule. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedPoint Radius;

	// Constructors
	// ====================================================================================================

	FORCEINLINE FFixedCapsule()
		: Start(FFixedVector::ZeroVector)
		, End(FFixedVector::ZeroVector)
		, Radius(FFixedPoint::Zero)
	{}

	FORCEINLINE FFixedCapsule(const FFixedVector& InStart, const FFixedVector& InEnd, FFixedPoint InRadius)
		: Start(InStart)
		, End(InEnd)
		, Radius(InRadius)
	{}

	/** Creates a capsule from center, half-height, and radius (typical Unreal convention). */
	FORCEINLINE static FFixedCapsule FromCenterAndHalfHeight(const FFixedVector& Center, FFixedPoint HalfHeight, FFixedPoint InRadius, const FFixedVector& UpDirection = FFixedVector::UpVector)
	{
		FFixedVector Offset = UpDirection * HalfHeight;
		return FFixedCapsule(Center - Offset, Center + Offset, InRadius);
	}

	// Queries
	// ====================================================================================================

	/** Returns the center of the capsule. */
	FORCEINLINE FFixedVector GetCenter() const
	{
		return (Start + End) * FFixedPoint::Half;
	}

	/** Returns the half-height of the capsule (distance from center to either end). */
	FORCEINLINE FFixedPoint GetHalfHeight() const
	{
		return (End - Start).Size() * FFixedPoint::Half;
	}

	/** Returns the full height of the capsule (distance between start and end). */
	FORCEINLINE FFixedPoint GetHeight() const
	{
		return (End - Start).Size();
	}

	/** Returns the direction vector from Start to End (normalized). */
	FORCEINLINE FFixedVector GetDirection() const
	{
		return FFixedVector::GetSafeNormal(End - Start, FFixedPoint::Epsilon);
	}

	/** Returns the volume of the capsule (cylinder + 2 hemispheres). */
	FORCEINLINE FFixedPoint GetVolume() const
	{
		// Volume = Pi * r^2 * h + 4/3 * Pi * r^3
		// Where h is the cylinder height (distance between Start and End)
		FFixedPoint R2 = Radius * Radius;
		FFixedPoint R3 = R2 * Radius;
		FFixedPoint CylinderHeight = GetHeight();
		
		FFixedPoint CylinderVolume = FFixedPoint::Pi * R2 * CylinderHeight;
		FFixedPoint SphereVolume = (FFixedPoint::Pi * R3 * FFixedPoint(4)) / FFixedPoint(3);
		
		return CylinderVolume + SphereVolume;
	}

	/** Returns the closest point on the capsule's central axis to the given point. */
	FORCEINLINE FFixedVector GetClosestPointOnAxis(const FFixedVector& Point) const
	{
		FFixedVector Axis = End - Start;
		FFixedPoint AxisLengthSq = Axis.SizeSquared();
		
		if (AxisLengthSq <= FFixedPoint::Epsilon)
		{
			// Degenerate case: Start and End are the same
			return Start;
		}
		
		// Project Point onto the axis
		FFixedVector ToPoint = Point - Start;
		FFixedPoint T = FFixedVector::DotProduct(ToPoint, Axis) / AxisLengthSq;
		T = SeinMath::Clamp(T, FFixedPoint::Zero, FFixedPoint::One);
		
		return Start + Axis * T;
	}

	/** Returns the closest point on the capsule surface to the given point. */
	FORCEINLINE FFixedVector GetClosestPoint(const FFixedVector& Point) const
	{
		FFixedVector ClosestOnAxis = GetClosestPointOnAxis(Point);
		FFixedVector Direction = Point - ClosestOnAxis;
		FFixedPoint Distance = Direction.Size();
		
		if (Distance <= FFixedPoint::Epsilon)
		{
			// Point is on the axis, return any point on surface
			FFixedVector Perpendicular = FFixedVector::CrossProduct(
				End - Start,
				FFixedVector(FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::Zero)
			);
			
			if (Perpendicular.SizeSquared() <= FFixedPoint::Epsilon)
			{
				Perpendicular = FFixedVector::CrossProduct(
					End - Start,
					FFixedVector(FFixedPoint::Zero, FFixedPoint::One, FFixedPoint::Zero)
				);
			}
			
			Perpendicular = FFixedVector::GetSafeNormal(Perpendicular, FFixedPoint::Epsilon);
			return ClosestOnAxis + Perpendicular * Radius;
		}
		
		return ClosestOnAxis + (Direction / Distance) * Radius;
	}

	/** Returns true if the capsule contains the given point. */
	FORCEINLINE bool Contains(const FFixedVector& Point) const
	{
		FFixedVector ClosestOnAxis = GetClosestPointOnAxis(Point);
		return (Point - ClosestOnAxis).SizeSquared() <= Radius * Radius;
	}

	/** Returns the distance from the capsule surface to a point (negative if inside). */
	FORCEINLINE FFixedPoint GetDistanceToPoint(const FFixedVector& Point) const
	{
		FFixedVector ClosestOnAxis = GetClosestPointOnAxis(Point);
		return (Point - ClosestOnAxis).Size() - Radius;
	}

	// Modification
	// ====================================================================================================

	/** Expands the capsule radius by a given amount. */
	FORCEINLINE FFixedCapsule& Expand(FFixedPoint Amount)
	{
		Radius += Amount;
		return *this;
	}

	/** Returns an expanded copy of the capsule. */
	FORCEINLINE FFixedCapsule GetExpanded(FFixedPoint Amount) const
	{
		return FFixedCapsule(Start, End, Radius + Amount);
	}

	/** Translates the capsule by a vector. */
	FORCEINLINE FFixedCapsule& Translate(const FFixedVector& Offset)
	{
		Start += Offset;
		End += Offset;
		return *this;
	}

	/** Returns a translated copy of the capsule. */
	FORCEINLINE FFixedCapsule GetTranslated(const FFixedVector& Offset) const
	{
		return FFixedCapsule(Start + Offset, End + Offset, Radius);
	}

	// Operators
	// ====================================================================================================

	FORCEINLINE bool operator==(const FFixedCapsule& Other) const
	{
		return Start == Other.Start && End == Other.End && Radius == Other.Radius;
	}

	FORCEINLINE bool operator!=(const FFixedCapsule& Other) const
	{
		return !(*this == Other);
	}

	FORCEINLINE FFixedCapsule operator+(const FFixedVector& Offset) const
	{
		return GetTranslated(Offset);
	}

	FORCEINLINE FFixedCapsule& operator+=(const FFixedVector& Offset)
	{
		return Translate(Offset);
	}

	// String Conversion
	// ====================================================================================================

	FORCEINLINE FString ToString() const
	{
		return FString::Printf(TEXT("Capsule(Start: %s, End: %s, Radius: %s)"), 
			*Start.ToString(), *End.ToString(), *Radius.ToString());
	}
};

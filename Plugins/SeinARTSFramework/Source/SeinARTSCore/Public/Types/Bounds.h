/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		Bounds.h
 * @date:		1/17/2026
 * @author:		RJ Macklem
 * @brief:		Deterministic combined bounding volume using fixed-point arithmetic.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "FixedPoint.h"
#include "Vector.h"
#include "Box.h"
#include "Sphere.h"
#include "Bounds.generated.h"

/**
 * Combined bounding volume using fixed-point arithmetic.
 * Contains both a box and sphere for efficient broad-phase collision detection.
 * Use box for tight axis-aligned checks, sphere for quick distance checks.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCORE_API FFixedBounds
{
	GENERATED_BODY()

	/** Center of the bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedVector Origin;

	/** Half-extent of the box bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedVector BoxExtent;

	/** Radius of the sphere bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedPoint SphereRadius;

	// Constructors
	// ====================================================================================================

	FORCEINLINE FFixedBounds()
		: Origin(FFixedVector::ZeroVector)
		, BoxExtent(FFixedVector::ZeroVector)
		, SphereRadius(FFixedPoint::Zero)
	{}

	FORCEINLINE FFixedBounds(const FFixedVector& InOrigin, const FFixedVector& InBoxExtent, FFixedPoint InSphereRadius)
		: Origin(InOrigin)
		, BoxExtent(InBoxExtent)
		, SphereRadius(InSphereRadius)
	{}

	/** Creates bounds from a box (calculates appropriate sphere radius). */
	FORCEINLINE static FFixedBounds FromBox(const FFixedBox& Box)
	{
		FFixedVector Center = Box.GetCenter();
		FFixedVector Extent = Box.GetExtent();
		FFixedPoint Radius = Extent.Size(); // Distance from center to corner
		return FFixedBounds(Center, Extent, Radius);
	}

	/** Creates bounds from a sphere (calculates appropriate box extent). */
	FORCEINLINE static FFixedBounds FromSphere(const FFixedSphere& Sphere)
	{
		FFixedVector Extent(Sphere.Radius, Sphere.Radius, Sphere.Radius);
		return FFixedBounds(Sphere.Center, Extent, Sphere.Radius);
	}

	/** Creates bounds from center and extent (calculates sphere radius as diagonal). */
	FORCEINLINE static FFixedBounds FromCenterAndExtent(const FFixedVector& Center, const FFixedVector& Extent)
	{
		FFixedPoint Radius = Extent.Size();
		return FFixedBounds(Center, Extent, Radius);
	}

	/** Converts from Unreal's FBoxSphereBounds. */
	FORCEINLINE static FFixedBounds FromBoxSphereBounds(const FBoxSphereBounds& Bounds)
	{
		return FFixedBounds(
			FFixedVector::FromVector(Bounds.Origin),
			FFixedVector::FromVector(Bounds.BoxExtent),
			FFixedPoint::FromFloat(Bounds.SphereRadius)
		);
	}

	/** Converts to Unreal's FBoxSphereBounds. */
	FORCEINLINE FBoxSphereBounds ToBoxSphereBounds() const
	{
		return FBoxSphereBounds(
			Origin.ToVector(),
			BoxExtent.ToVector(),
			SphereRadius.ToFloat()
		);
	}

	// Accessors
	// ====================================================================================================

	/** Returns the box component of the bounds. */
	FORCEINLINE FFixedBox GetBox() const
	{
		return FFixedBox::FromCenterAndExtent(Origin, BoxExtent);
	}

	/** Returns the sphere component of the bounds. */
	FORCEINLINE FFixedSphere GetSphere() const
	{
		return FFixedSphere(Origin, SphereRadius);
	}

	// Queries
	// ====================================================================================================

	/** Returns true if the bounds contains the given point (box test). */
	FORCEINLINE bool ContainsPoint(const FFixedVector& Point) const
	{
		return GetBox().Contains(Point);
	}

	/** Returns true if the bounds sphere contains the given point (faster, less accurate). */
	FORCEINLINE bool ContainsPointFast(const FFixedVector& Point) const
	{
		return (Point - Origin).SizeSquared() <= SphereRadius * SphereRadius;
	}

	/** Returns true if this bounds intersects with another bounds (sphere test). */
	FORCEINLINE bool IntersectsFast(const FFixedBounds& Other) const
	{
		FFixedPoint CombinedRadius = SphereRadius + Other.SphereRadius;
		return (Origin - Other.Origin).SizeSquared() <= CombinedRadius * CombinedRadius;
	}

	/** Returns true if this bounds intersects with another bounds (box test). */
	FORCEINLINE bool Intersects(const FFixedBounds& Other) const
	{
		return GetBox().Intersects(Other.GetBox());
	}

	/** Returns true if this bounds is inside another bounds. */
	FORCEINLINE bool IsInside(const FFixedBounds& Other) const
	{
		return GetBox().IsInside(Other.GetBox());
	}

	/** Returns the distance from the bounds to a point (approximate, uses sphere). */
	FORCEINLINE FFixedPoint GetDistanceToPoint(const FFixedVector& Point) const
	{
		return (Point - Origin).Size() - SphereRadius;
	}

	// Modification
	// ====================================================================================================

	/** Expands the bounds to include a point. */
	FORCEINLINE FFixedBounds& ExpandToInclude(const FFixedVector& Point)
	{
		FFixedBox Box = GetBox();
		Box.ExpandToInclude(Point);
		Origin = Box.GetCenter();
		BoxExtent = Box.GetExtent();
		SphereRadius = BoxExtent.Size();
		return *this;
	}

	/** Expands the bounds to include another bounds. */
	FORCEINLINE FFixedBounds& ExpandToInclude(const FFixedBounds& Other)
	{
		FFixedBox Box = GetBox();
		Box.ExpandToInclude(Other.GetBox());
		Origin = Box.GetCenter();
		BoxExtent = Box.GetExtent();
		SphereRadius = BoxExtent.Size();
		return *this;
	}

	/** Expands the bounds by a given amount in all directions. */
	FORCEINLINE FFixedBounds& Expand(FFixedPoint Amount)
	{
		BoxExtent += FFixedVector(Amount, Amount, Amount);
		SphereRadius += Amount;
		return *this;
	}

	/** Returns an expanded copy of the bounds. */
	FORCEINLINE FFixedBounds GetExpanded(FFixedPoint Amount) const
	{
		return FFixedBounds(Origin, BoxExtent + FFixedVector(Amount, Amount, Amount), SphereRadius + Amount);
	}

	/** Translates the bounds by a vector. */
	FORCEINLINE FFixedBounds& Translate(const FFixedVector& Offset)
	{
		Origin += Offset;
		return *this;
	}

	/** Returns a translated copy of the bounds. */
	FORCEINLINE FFixedBounds GetTranslated(const FFixedVector& Offset) const
	{
		return FFixedBounds(Origin + Offset, BoxExtent, SphereRadius);
	}

	// Operators
	// ====================================================================================================

	FORCEINLINE bool operator==(const FFixedBounds& Other) const
	{
		return Origin == Other.Origin && BoxExtent == Other.BoxExtent && SphereRadius == Other.SphereRadius;
	}

	FORCEINLINE bool operator!=(const FFixedBounds& Other) const
	{
		return !(*this == Other);
	}

	FORCEINLINE FFixedBounds operator+(const FFixedVector& Offset) const
	{
		return GetTranslated(Offset);
	}

	FORCEINLINE FFixedBounds& operator+=(const FFixedVector& Offset)
	{
		return Translate(Offset);
	}

	// String Conversion
	// ====================================================================================================

	FORCEINLINE FString ToString() const
	{
		return FString::Printf(TEXT("Bounds(Origin: %s, BoxExtent: %s, SphereRadius: %s)"), 
			*Origin.ToString(), *BoxExtent.ToString(), *SphereRadius.ToString());
	}
};

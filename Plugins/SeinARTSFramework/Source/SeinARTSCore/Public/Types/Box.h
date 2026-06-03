/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		Box.h
 * @date:		1/17/2026
 * @author:		RJ Macklem
 * @brief:		Deterministic axis-aligned bounding box using fixed-point arithmetic.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "FixedPoint.h"
#include "Vector.h"
#include "Box.generated.h"

/**
 * Axis-aligned bounding box using fixed-point arithmetic.
 * Represents a box defined by minimum and maximum corners.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCORE_API FFixedBox
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedVector Min;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFixedVector Max;

	// Constructors
	// ====================================================================================================

	FORCEINLINE FFixedBox()
		: Min(FFixedVector::ZeroVector)
		, Max(FFixedVector::ZeroVector)
	{}

	FORCEINLINE FFixedBox(const FFixedVector& InMin, const FFixedVector& InMax)
		: Min(InMin)
		, Max(InMax)
	{}

	FORCEINLINE FFixedBox(const FFixedVector& Center, FFixedPoint HalfExtent)
		: Min(Center.X - HalfExtent, Center.Y - HalfExtent, Center.Z - HalfExtent)
		, Max(Center.X + HalfExtent, Center.Y + HalfExtent, Center.Z + HalfExtent)
	{}

	// Factory Methods
	// ====================================================================================================

	/** Creates a box from center and extent (half-size). */
	FORCEINLINE static FFixedBox FromCenterAndExtent(const FFixedVector& Center, const FFixedVector& Extent)
	{
		return FFixedBox(Center - Extent, Center + Extent);
	}

	/** Creates an invalid box (Min > Max, useful for expanding). */
	FORCEINLINE static FFixedBox InvalidBox()
	{
		return FFixedBox(
			FFixedVector(FFixedPoint::MaxValue, FFixedPoint::MaxValue, FFixedPoint::MaxValue),
			FFixedVector(FFixedPoint::MinValue, FFixedPoint::MinValue, FFixedPoint::MinValue)
		);
	}

	/** Converts from Unreal's FBox. */
	FORCEINLINE static FFixedBox FromBox(const FBox& Box)
	{
		return FFixedBox(
			FFixedVector::FromVector(Box.Min),
			FFixedVector::FromVector(Box.Max)
		);
	}

	/** Converts to Unreal's FBox. */
	FORCEINLINE FBox ToBox() const
	{
		return FBox(Min.ToVector(), Max.ToVector());
	}

	// Queries
	// ====================================================================================================

	/** Returns the center of the box. */
	FORCEINLINE FFixedVector GetCenter() const
	{
		return (Min + Max) * FFixedPoint::Half;
	}

	/** Returns the extent (half-size) of the box. */
	FORCEINLINE FFixedVector GetExtent() const
	{
		return (Max - Min) * FFixedPoint::Half;
	}

	/** Returns the size (full dimensions) of the box. */
	FORCEINLINE FFixedVector GetSize() const
	{
		return Max - Min;
	}

	/** Returns the volume of the box. */
	FORCEINLINE FFixedPoint GetVolume() const
	{
		FFixedVector Size = GetSize();
		return Size.X * Size.Y * Size.Z;
	}

	/** Returns true if the box is valid (Min <= Max in all axes). */
	FORCEINLINE bool IsValid() const
	{
		return Min.X <= Max.X && Min.Y <= Max.Y && Min.Z <= Max.Z;
	}

	/** Returns true if the box contains the given point. */
	FORCEINLINE bool Contains(const FFixedVector& Point) const
	{
		return Point.X >= Min.X && Point.X <= Max.X &&
		       Point.Y >= Min.Y && Point.Y <= Max.Y &&
		       Point.Z >= Min.Z && Point.Z <= Max.Z;
	}

	/** Returns true if this box intersects with another box. */
	FORCEINLINE bool Intersects(const FFixedBox& Other) const
	{
		return Min.X <= Other.Max.X && Max.X >= Other.Min.X &&
		       Min.Y <= Other.Max.Y && Max.Y >= Other.Min.Y &&
		       Min.Z <= Other.Max.Z && Max.Z >= Other.Min.Z;
	}

	/** Returns true if this box is inside another box. */
	FORCEINLINE bool IsInside(const FFixedBox& Other) const
	{
		return Min.X >= Other.Min.X && Max.X <= Other.Max.X &&
		       Min.Y >= Other.Min.Y && Max.Y <= Other.Max.Y &&
		       Min.Z >= Other.Min.Z && Max.Z <= Other.Max.Z;
	}

	// Modification
	// ====================================================================================================

	/** Expands the box to include the given point. */
	FORCEINLINE FFixedBox& ExpandToInclude(const FFixedVector& Point)
	{
		Min.X = SeinMath::Min(Min.X, Point.X);
		Min.Y = SeinMath::Min(Min.Y, Point.Y);
		Min.Z = SeinMath::Min(Min.Z, Point.Z);
		Max.X = SeinMath::Max(Max.X, Point.X);
		Max.Y = SeinMath::Max(Max.Y, Point.Y);
		Max.Z = SeinMath::Max(Max.Z, Point.Z);
		return *this;
	}

	/** Expands the box to include another box. */
	FORCEINLINE FFixedBox& ExpandToInclude(const FFixedBox& Other)
	{
		Min.X = SeinMath::Min(Min.X, Other.Min.X);
		Min.Y = SeinMath::Min(Min.Y, Other.Min.Y);
		Min.Z = SeinMath::Min(Min.Z, Other.Min.Z);
		Max.X = SeinMath::Max(Max.X, Other.Max.X);
		Max.Y = SeinMath::Max(Max.Y, Other.Max.Y);
		Max.Z = SeinMath::Max(Max.Z, Other.Max.Z);
		return *this;
	}

	/** Expands the box by a given amount in all directions. */
	FORCEINLINE FFixedBox& Expand(FFixedPoint Amount)
	{
		Min -= FFixedVector(Amount, Amount, Amount);
		Max += FFixedVector(Amount, Amount, Amount);
		return *this;
	}

	/** Returns an expanded copy of the box. */
	FORCEINLINE FFixedBox GetExpanded(FFixedPoint Amount) const
	{
		FFixedBox Result = *this;
		return Result.Expand(Amount);
	}

	/** Translates the box by a vector. */
	FORCEINLINE FFixedBox& Translate(const FFixedVector& Offset)
	{
		Min += Offset;
		Max += Offset;
		return *this;
	}

	/** Returns a translated copy of the box. */
	FORCEINLINE FFixedBox GetTranslated(const FFixedVector& Offset) const
	{
		return FFixedBox(Min + Offset, Max + Offset);
	}

	// Operators
	// ====================================================================================================

	FORCEINLINE bool operator==(const FFixedBox& Other) const
	{
		return Min == Other.Min && Max == Other.Max;
	}

	FORCEINLINE bool operator!=(const FFixedBox& Other) const
	{
		return !(*this == Other);
	}

	FORCEINLINE FFixedBox operator+(const FFixedVector& Offset) const
	{
		return GetTranslated(Offset);
	}

	FORCEINLINE FFixedBox& operator+=(const FFixedVector& Offset)
	{
		return Translate(Offset);
	}

	// String Conversion
	// ====================================================================================================

	FORCEINLINE FString ToString() const
	{
		return FString::Printf(TEXT("Box(Min: %s, Max: %s)"), *Min.ToString(), *Max.ToString());
	}
};

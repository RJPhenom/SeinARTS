/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		Transform.h
 * @date:		1/17/2026
 * @author:		RJ Macklem
 * @brief:		Deterministic fixed-point transform for lockstep simulation.
 * 				Stores position, rotation (quaternion), and scale.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "Types/Rotator.h"
#include "Transform.generated.h"

/**
 * Deterministic fixed-point transform.
 * Position, Rotation (quaternion), and Scale for entity placement in lockstep simulation.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCORE_API FFixedTransform
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Math|Transform")
	FFixedVector Location;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Math|Transform")
	FFixedQuaternion Rotation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Math|Transform")
	FFixedVector Scale;

	// Constructors
	// ====================================================================================================
	
	FORCEINLINE FFixedTransform()
		: Location(FFixedVector::ZeroVector)
		, Rotation(FFixedQuaternion::Identity)
		, Scale(FFixedVector::Identity)
	{}

	FORCEINLINE FFixedTransform(const FFixedVector& InLocation)
		: Location(InLocation)
		, Rotation(FFixedQuaternion::Identity)
		, Scale(FFixedVector::Identity)
	{}

	FORCEINLINE FFixedTransform(const FFixedQuaternion& InRotation)
		: Location(FFixedVector::ZeroVector)
		, Rotation(InRotation)
		, Scale(FFixedVector::Identity)
	{}

	FORCEINLINE FFixedTransform(const FFixedRotator& InRotation)
		: Location(FFixedVector::ZeroVector)
		, Rotation(InRotation.Quaternion())
		, Scale(FFixedVector::Identity)
	{}

	FORCEINLINE FFixedTransform(const FFixedVector& InLocation, const FFixedQuaternion& InRotation)
		: Location(InLocation)
		, Rotation(InRotation)
		, Scale(FFixedVector::Identity)
	{}

	FORCEINLINE FFixedTransform(const FFixedVector& InLocation, const FFixedRotator& InRotation)
		: Location(InLocation)
		, Rotation(InRotation.Quaternion())
		, Scale(FFixedVector::Identity)
	{}

	FORCEINLINE FFixedTransform(const FFixedVector& InLocation, const FFixedQuaternion& InRotation, const FFixedVector& InScale)
		: Location(InLocation)
		, Rotation(InRotation)
		, Scale(InScale)
	{}

	FORCEINLINE FFixedTransform(const FFixedVector& InLocation, const FFixedRotator& InRotation, const FFixedVector& InScale)
		: Location(InLocation)
		, Rotation(InRotation.Quaternion())
		, Scale(InScale)
	{}

	// Static Factory Methods
	// ====================================================================================================

	static FORCEINLINE FFixedTransform Identity()
	{
		return FFixedTransform();
	}

	static FORCEINLINE FFixedTransform FromTransform(const FTransform& T)
	{
		return FFixedTransform(
			FFixedVector::FromVector(T.GetLocation()),
			FFixedQuaternion::FromQuat(T.GetRotation()),
			FFixedVector::FromVector(T.GetScale3D())
		);
	}

	FORCEINLINE FTransform ToTransform() const
	{
		return FTransform(Rotation.ToQuat(), Location.ToVector(), Scale.ToVector());
	}

	// Accessors
	// ====================================================================================================

	FORCEINLINE FFixedVector        GetLocation() const { return Location; }
	FORCEINLINE FFixedRotator       GetRotation() const { return Rotation.Rotator(); }
	FORCEINLINE FFixedVector        GetScale() const { return Scale; }
	FORCEINLINE FFixedQuaternion    GetQuaternionRotation() const { return Rotation; }

	FORCEINLINE void SetLocation(const FFixedVector& NewLocation) { Location = NewLocation; }
	FORCEINLINE void SetRotation(const FFixedQuaternion& NewRotation) { Rotation = NewRotation; }
	FORCEINLINE void SetScale(const FFixedVector& NewScale) { Scale = NewScale; }


	// Transform Operations
	// ====================================================================================================

	/**
	 * Transform a position by this transform.
	 * Result = Rotation * (Scale * Point) + Location
	 */
	FORCEINLINE FFixedVector TransformPosition(const FFixedVector& Point) const
	{
		// Scale
		FFixedVector ScaledPoint = Point * Scale;
		// Rotate
		FFixedVector RotatedPoint = Rotation.RotateVector(ScaledPoint);
		// Translate
		return RotatedPoint + Location;
	}

	/**
	 * Transform a direction vector by this transform (no Location, no scale).
	 * Result = Rotation * Vector
	 */
	FORCEINLINE FFixedVector TransformVector(const FFixedVector& Vector) const
	{
		return Rotation.RotateVector(Vector);
	}

	/**
	 * Transform a direction vector by this transform (includes scale, no Location).
	 * Result = Rotation * (Scale * Vector)
	 */
	FORCEINLINE FFixedVector TransformVectorNoLocation(const FFixedVector& Vector) const
	{
		FFixedVector ScaledVector = Vector * Scale;
		return Rotation.RotateVector(ScaledVector);
	}

	/**
	 * Inverse transform a position.
	 * Result = InvRotation * ((Point - Location) / Scale)
	 */
	FORCEINLINE FFixedVector InverseTransformPosition(const FFixedVector& Point) const
	{
		// Inverse translate
		FFixedVector TranslatedPoint = Point - Location;
		// Inverse rotate
		FFixedVector RotatedPoint = Rotation.Inverse().RotateVector(TranslatedPoint);
		// Inverse scale
		return RotatedPoint / Scale;
	}

	/**
	 * Inverse transform a direction vector (no Location, no scale).
	 * Result = InvRotation * Vector
	 */
	FORCEINLINE FFixedVector InverseTransformVector(const FFixedVector& Vector) const
	{
		return Rotation.Inverse().RotateVector(Vector);
	}

	/**
	 * Get the inverse of this transform.
	 */
	FORCEINLINE FFixedTransform Inverse() const
	{
		FFixedQuaternion InvRotation = Rotation.Inverse();
		FFixedVector InvScale = FFixedVector::Identity / Scale;
		FFixedVector InvLocation = InvRotation.RotateVector(-Location) * InvScale;
		
		return FFixedTransform(InvLocation, InvRotation, InvScale);
	}

	/**
	 * Multiply two transforms.
	 * Result = B * A (applies A first, then B)
	 */
	static FORCEINLINE FFixedTransform Multiply(const FFixedTransform& A, const FFixedTransform& B)
	{
		FFixedQuaternion NewRotation = B.Rotation * A.Rotation;
		FFixedVector NewScale = B.Scale * A.Scale;
		FFixedVector NewLocation = B.TransformPosition(A.Location);
		
		return FFixedTransform(NewLocation, NewRotation, NewScale);
	}

	/**
	 * Linear interpolation between two transforms.
	 */
	static FORCEINLINE FFixedTransform Lerp(const FFixedTransform& A, const FFixedTransform& B, FFixedPoint Alpha)
	{
		FFixedVector NewLocation = FFixedVector::Lerp(A.Location, B.Location, Alpha);
		FFixedQuaternion NewRotation = FFixedQuaternion::Slerp(A.Rotation, B.Rotation, Alpha);
		FFixedVector NewScale = FFixedVector::Lerp(A.Scale, B.Scale, Alpha);
		
		return FFixedTransform(NewLocation, NewRotation, NewScale);
	}

	// Operators
	// ====================================================================================================

	/**
	 * Multiply two transforms (apply A first, then this)
	 */
	FORCEINLINE FFixedTransform operator*(const FFixedTransform& Other) const
	{
		return Multiply(Other, *this);
	}

	FORCEINLINE bool operator==(const FFixedTransform& Other) const
	{
		return Location == Other.Location
			&& Rotation == Other.Rotation
			&& Scale == Other.Scale;
	}

	FORCEINLINE bool operator!=(const FFixedTransform& Other) const
	{
		return !(*this == Other);
	}

	// Utility
	// ====================================================================================================

	FORCEINLINE FString ToString() const
	{
		return FString::Printf(TEXT("T: %s R: %s S: %s"),
			*Location.ToString(),
			*Rotation.Rotator().ToString(),
			*Scale.ToString());
	}

	/**
	 * Blend between two transforms with separate position/rotation/scale blend factors.
	 */
	static FORCEINLINE FFixedTransform BlendFromIdentity(const FFixedTransform& Source, FFixedPoint PositionBlend, FFixedPoint RotationBlend, FFixedPoint ScaleBlend)
	{
		FFixedVector NewLocation = Source.Location * PositionBlend;
		FFixedQuaternion NewRotation = FFixedQuaternion::Slerp(FFixedQuaternion::Identity, Source.Rotation, RotationBlend);
		FFixedVector NewScale = FFixedVector::Lerp(FFixedVector::Identity, Source.Scale, ScaleBlend);
		
		return FFixedTransform(NewLocation, NewRotation, NewScale);
	}

	/**
	 * Get a transform that only has the Location component.
	 */
	FORCEINLINE FFixedTransform GetLocationOnly() const
	{
		return FFixedTransform(Location);
	}

	/**
	 * Get a transform that only has the rotation component.
	 */
	FORCEINLINE FFixedTransform GetRotationOnly() const
	{
		return FFixedTransform(Rotation);
	}

	/**
	 * Add a Location offset to this transform.
	 */
	FORCEINLINE void AddToLocation(const FFixedVector& DeltaLocation)
	{
		Location = Location + DeltaLocation;
	}

	/**
	 * Accumulate another transform's rotation on top of this one.
	 */
	FORCEINLINE void AccumulateRotation(const FFixedQuaternion& DeltaRotation)
	{
		Rotation = DeltaRotation * Rotation;
	}

	/**
	 * Set the Location while keeping rotation and scale.
	 */
	FORCEINLINE void SetLocationAndScale(const FFixedVector& NewLocation, const FFixedVector& NewScale)
	{
		Location = NewLocation;
		Scale = NewScale;
	}
};

/** Hash function for FFixedTransform */
FORCEINLINE uint32 GetTypeHash(const FFixedTransform& T)
{
	uint32 Hash = GetTypeHash(T.Location);
	Hash = HashCombine(Hash, GetTypeHash(T.Rotation));
	Hash = HashCombine(Hash, GetTypeHash(T.Scale));
	return Hash;
}

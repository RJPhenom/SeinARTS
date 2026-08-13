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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Math|Vector")
	FFixedPoint X;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Math|Vector")
	FFixedPoint Y;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Math|Vector")
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

	/** Squared magnitude that saturates instead of wrapping when a valid vector
	 *  is longer than the 32.32 squared-value range. In-range results are exact. */
	FORCEINLINE FFixedPoint SizeSquaredSaturated() const
	{
		return SumRawMagnitudeSquaresSaturated(
			RawMagnitude(X.Value),
			RawMagnitude(Y.Value),
			RawMagnitude(Z.Value));
	}
	FORCEINLINE FFixedPoint Size() const 
	{ 
		const FFixedPoint SizeSq = SizeSquaredSaturated();
		if (SizeSq != FFixedPoint::MaxValue)
		{
			return SeinMath::Sqrt(SizeSq);
		}

		// Scale first when the square cannot be represented. Components of the
		// scaled vector are in [-1, 1], so its square is always safe.
		const FFixedPoint MaxComponent = GetAbsMax(*this);
		if (MaxComponent == FFixedPoint::Zero)
		{
			return FFixedPoint::Zero;
		}
		const FFixedVector Scaled = *this / MaxComponent;
		const FFixedPoint ScaledSize =
			SeinMath::Sqrt(Scaled.SizeSquared());
		if (ScaledSize > FFixedPoint::MaxValue / MaxComponent)
		{
			return FFixedPoint::MaxValue;
		}
		return ScaledSize * MaxComponent;
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
	/** Distance that cannot wrap while forming B-A. Values beyond the 32.32
	 *  scalar range saturate; every representable result is exact to Size(). */
	FORCEINLINE static FFixedPoint DistanceSaturated(const FFixedVector& A, const FFixedVector& B)
	{
		const uint64 DX = RawDistance(A.X.Value, B.X.Value);
		const uint64 DY = RawDistance(A.Y.Value, B.Y.Value);
		const uint64 DZ = RawDistance(A.Z.Value, B.Z.Value);
		if (DX > static_cast<uint64>(INT64_MAX)
			|| DY > static_cast<uint64>(INT64_MAX)
			|| DZ > static_cast<uint64>(INT64_MAX))
		{
			return FFixedPoint::MaxValue;
		}
		return MakeSignedDifference(A, B, DX, DY, DZ).Size();
	}
	FORCEINLINE static FFixedPoint DistSquared(const FFixedVector& A, const FFixedVector& B) { return (B - A).SizeSquared(); }
	/** Squared distance that computes each raw component delta before squaring,
	 *  so opposite representable endpoints cannot wrap during subtraction. */
	FORCEINLINE static FFixedPoint DistSquaredSaturated(const FFixedVector& A, const FFixedVector& B)
	{
		return SumRawMagnitudeSquaresSaturated(
			RawDistance(A.X.Value, B.X.Value),
			RawDistance(A.Y.Value, B.Y.Value),
			RawDistance(A.Z.Value, B.Z.Value));
	}
	FORCEINLINE static FFixedPoint PlanarDistSquaredSaturated(
		const FFixedVector& A,
		const FFixedVector& B)
	{
		return SumRawMagnitudeSquaresSaturated(
			RawDistance(A.X.Value, B.X.Value),
			RawDistance(A.Y.Value, B.Y.Value),
			0);
	}
	FORCEINLINE static FFixedPoint SquareSaturated(FFixedPoint Value)
	{
		return SumRawMagnitudeSquaresSaturated(
			RawMagnitude(Value.Value), 0, 0);
	}
	/** Exact radius predicate over raw 32.32 deltas. Unlike comparing two
	 *  saturated squared scalars, overflow can never alias with equality. */
	FORCEINLINE static bool IsDistanceWithin(
		const FFixedVector& A,
		const FFixedVector& B,
		FFixedPoint Radius)
	{
		return Radius >= FFixedPoint::Zero
			&& IsRawDistanceWithinRadius(
				RawDistance(A.X.Value, B.X.Value),
				RawDistance(A.Y.Value, B.Y.Value),
				RawDistance(A.Z.Value, B.Z.Value),
				static_cast<uint64>(Radius.Value));
	}
	FORCEINLINE static bool IsPlanarDistanceWithin(
		const FFixedVector& A,
		const FFixedVector& B,
		FFixedPoint Radius)
	{
		return Radius >= FFixedPoint::Zero
			&& IsRawDistanceWithinRadius(
				RawDistance(A.X.Value, B.X.Value),
				RawDistance(A.Y.Value, B.Y.Value),
				0,
				static_cast<uint64>(Radius.Value));
	}
	/** Exact comparison against an already-squared 32.32 planar threshold.
	 *  This preserves legacy squared-radius semantics without allowing two
	 *  saturated values to alias at MaxValue. */
	FORCEINLINE static bool IsPlanarDistSquaredWithin(
		const FFixedVector& A,
		const FFixedVector& B,
		FFixedPoint SquaredRadius)
	{
		if (SquaredRadius < FFixedPoint::Zero)
		{
			return false;
		}
		const uint64 DX = RawDistance(A.X.Value, B.X.Value);
		const uint64 DY = RawDistance(A.Y.Value, B.Y.Value);
		const uint64 ThresholdRaw =
			static_cast<uint64>(SquaredRadius.Value);
#if defined(__GNUC__) || defined(__clang__)
		const unsigned __int128 XSquare =
			(static_cast<unsigned __int128>(DX) * DX) >> 32;
		const unsigned __int128 YSquare =
			(static_cast<unsigned __int128>(DY) * DY) >> 32;
		if (XSquare > ThresholdRaw || YSquare > ThresholdRaw)
		{
			return false;
		}
		return XSquare + YSquare <= ThresholdRaw;
#elif defined(_MSC_VER)
		uint64 XHigh = 0;
		uint64 YHigh = 0;
		const uint64 XLow = _umul128(DX, DX, &XHigh);
		const uint64 YLow = _umul128(DY, DY, &YHigh);
		if ((XHigh >> 32) != 0 || (YHigh >> 32) != 0)
		{
			return false;
		}
		const uint64 XSquare = (XHigh << 32) | (XLow >> 32);
		const uint64 YSquare = (YHigh << 32) | (YLow >> 32);
		return XSquare <= ThresholdRaw
			&& YSquare <= ThresholdRaw - XSquare;
#else
#error "Platform does not support 128-bit multiplication"
#endif
	}
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
		const FFixedPoint SizeSq = V.SizeSquaredSaturated();
		if (SizeSq > Tolerance)
		{
			if (SizeSq != FFixedPoint::MaxValue)
			{
				const FFixedPoint VSize = SeinMath::Sqrt(SizeSq);
				if (VSize != FFixedPoint::Zero)
				{
					return V / VSize;
				}
			}
			else
			{
				// Size() must saturate for vectors whose true length exceeds the
				// scalar range. Normalize the bounded component ratios instead.
				const FFixedPoint Scale = GetAbsMax(V);
				if (Scale != FFixedPoint::Zero)
				{
					const FFixedVector Scaled = V / Scale;
					const FFixedPoint ScaledSize =
						SeinMath::Sqrt(Scaled.SizeSquaredSaturated());
					if (ScaledSize != FFixedPoint::Zero)
					{
						return Scaled / ScaledSize;
					}
				}
			}
		}
		return FFixedVector::ZeroVector;
	}

	/** Unit direction from From to To without first forming a wrapping delta. */
	FORCEINLINE static FFixedVector GetSafeNormalDifference(
		const FFixedVector& From,
		const FFixedVector& To,
		FFixedPoint Tolerance = FFixedPoint::Epsilon)
	{
		uint64 DX = RawDistance(From.X.Value, To.X.Value);
		uint64 DY = RawDistance(From.Y.Value, To.Y.Value);
		uint64 DZ = RawDistance(From.Z.Value, To.Z.Value);
		uint64 MaxMagnitude = DX;
		if (DY > MaxMagnitude) MaxMagnitude = DY;
		if (DZ > MaxMagnitude) MaxMagnitude = DZ;
		if (MaxMagnitude > static_cast<uint64>(INT64_MAX))
		{
			// One common shift is sufficient for any uint64 magnitude and
			// preserves component ratios to the available fixed-point precision.
			DX >>= 1;
			DY >>= 1;
			DZ >>= 1;
		}
		return GetSafeNormal(
			MakeSignedDifference(From, To, DX, DY, DZ), Tolerance);
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
		uint64 MaxMagnitude = RawMagnitude(V.X.Value);
		const uint64 YMagnitude = RawMagnitude(V.Y.Value);
		const uint64 ZMagnitude = RawMagnitude(V.Z.Value);
		if (YMagnitude > MaxMagnitude) MaxMagnitude = YMagnitude;
		if (ZMagnitude > MaxMagnitude) MaxMagnitude = ZMagnitude;
		return MaxMagnitude > static_cast<uint64>(INT64_MAX)
			? FFixedPoint::MaxValue
			: FFixedPoint(static_cast<int64>(MaxMagnitude));
	}

	// Get absolute minimum component
	FORCEINLINE static FFixedPoint GetAbsMin(const FFixedVector& V)
	{
		const FFixedPoint AbsX = SeinMath::Abs(V.X);
		const FFixedPoint AbsY = SeinMath::Abs(V.Y);
		const FFixedPoint AbsZ = SeinMath::Abs(V.Z);
		
		FFixedPoint MinVal = AbsX;
		if (AbsY < MinVal) MinVal = AbsY;
		if (AbsZ < MinVal) MinVal = AbsZ;
		return MinVal;
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

private:
	FORCEINLINE static uint64 RawMagnitude(int64 Raw)
	{
		return Raw < 0
			? 0ULL - static_cast<uint64>(Raw)
			: static_cast<uint64>(Raw);
	}

	FORCEINLINE static uint64 RawDistance(int64 A, int64 B)
	{
		const uint64 AValue = static_cast<uint64>(A);
		const uint64 BValue = static_cast<uint64>(B);
		return A >= B ? AValue - BValue : BValue - AValue;
	}

	FORCEINLINE static bool TrySquareRawMagnitude(
		uint64 Magnitude,
		uint64& OutShiftedSquare)
	{
#if defined(__GNUC__) || defined(__clang__)
		const unsigned __int128 Product =
			static_cast<unsigned __int128>(Magnitude) * Magnitude;
		const unsigned __int128 Shifted = Product >> 32;
		if (Shifted > static_cast<uint64>(INT64_MAX))
		{
			return false;
		}
		OutShiftedSquare = static_cast<uint64>(Shifted);
#elif defined(_MSC_VER)
		uint64 High = 0;
		const uint64 Low = _umul128(Magnitude, Magnitude, &High);
		if ((High >> 32) != 0)
		{
			return false;
		}
		OutShiftedSquare = (High << 32) | (Low >> 32);
		if (OutShiftedSquare > static_cast<uint64>(INT64_MAX))
		{
			return false;
		}
#else
#error "Platform does not support 128-bit multiplication"
#endif
		return true;
	}

	FORCEINLINE static FFixedPoint SumRawMagnitudeSquaresSaturated(
		uint64 XMagnitude,
		uint64 YMagnitude,
		uint64 ZMagnitude)
	{
		uint64 Sum = 0;
		const uint64 Magnitudes[] = {XMagnitude, YMagnitude, ZMagnitude};
		for (const uint64 Magnitude : Magnitudes)
		{
			uint64 Square = 0;
			if (!TrySquareRawMagnitude(Magnitude, Square)
				|| Square > static_cast<uint64>(INT64_MAX) - Sum)
			{
				return FFixedPoint::MaxValue;
			}
			Sum += Square;
		}
		return FFixedPoint(static_cast<int64>(Sum));
	}

	FORCEINLINE static bool IsRawDistanceWithinRadius(
		uint64 DX,
		uint64 DY,
		uint64 DZ,
		uint64 Radius)
	{
		if (DX > Radius || DY > Radius || DZ > Radius)
		{
			return false;
		}
#if defined(__GNUC__) || defined(__clang__)
		const unsigned __int128 DistanceSquared =
			static_cast<unsigned __int128>(DX) * DX
			+ static_cast<unsigned __int128>(DY) * DY
			+ static_cast<unsigned __int128>(DZ) * DZ;
		const unsigned __int128 RadiusSquared =
			static_cast<unsigned __int128>(Radius) * Radius;
		return DistanceSquared <= RadiusSquared;
#elif defined(_MSC_VER)
		uint64 SumHigh = 0;
		uint64 SumLow = 0;
		const uint64 Magnitudes[] = {DX, DY, DZ};
		for (const uint64 Magnitude : Magnitudes)
		{
			uint64 ProductHigh = 0;
			const uint64 ProductLow =
				_umul128(Magnitude, Magnitude, &ProductHigh);
			const uint64 PreviousLow = SumLow;
			SumLow += ProductLow;
			SumHigh += ProductHigh + (SumLow < PreviousLow ? 1ULL : 0ULL);
		}
		uint64 RadiusHigh = 0;
		const uint64 RadiusLow = _umul128(Radius, Radius, &RadiusHigh);
		return SumHigh < RadiusHigh
			|| (SumHigh == RadiusHigh && SumLow <= RadiusLow);
#else
#error "Platform does not support 128-bit multiplication"
#endif
	}

	FORCEINLINE static FFixedVector MakeSignedDifference(
		const FFixedVector& From,
		const FFixedVector& To,
		uint64 DX,
		uint64 DY,
		uint64 DZ)
	{
		return FFixedVector(
			FFixedPoint(To.X.Value < From.X.Value
				? -static_cast<int64>(DX) : static_cast<int64>(DX)),
			FFixedPoint(To.Y.Value < From.Y.Value
				? -static_cast<int64>(DY) : static_cast<int64>(DY)),
			FFixedPoint(To.Z.Value < From.Z.Value
				? -static_cast<int64>(DZ) : static_cast<int64>(DZ)));
	}

public:
	
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

/** The default vector is exactly three all-zero FFixedPoint values. */
template<>
struct TStructOpsTypeTraits<FFixedVector>
	: public TStructOpsTypeTraitsBase2<FFixedVector>
{
	enum
	{
		WithZeroConstructor = true,
	};
};

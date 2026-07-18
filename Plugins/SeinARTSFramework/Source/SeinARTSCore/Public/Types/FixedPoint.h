/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		FixedPoint.h
 * @date:		1/16/2026
 * @author:		RJ Macklem
 * @brief:		Deterministic fixed-point number (32.32 format) for lockstep 
 * 				simulation. Provides deterministic arithmetic operations.
 * @disclaimer: This code was generated in part by an AI language model.
 */
 
#pragma once

#include "CoreMinimal.h"

// Platform-specific includes for 128-bit math
#if defined(_MSC_VER)
	#include <intrin.h>  // For _mul128
#endif

#include "FixedPoint.generated.h"

USTRUCT(BlueprintType, meta = (
	SeinDeterministic,
	HasNativeMake  = "/Script/SeinARTSCoreEntity.MathBPFL.MakeFixedPointFromParts",
	HasNativeBreak = "/Script/SeinARTSCoreEntity.MathBPFL.BreakFixedPointToParts"))
struct SEINARTSCORE_API FFixedPoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int64 Value;

	// Constructors
	constexpr FFixedPoint() : Value(0) {}
	explicit constexpr FFixedPoint(int64 InValue) : Value(InValue) {}

	/** Native binary serializer (enabled via the TStructOpsTypeTraits below). Writes
	 *  the raw 32.32 int64, so a TArray<FFixedPoint> — or any FFixedPoint UPROPERTY —
	 *  serializes as a compact bulk blob instead of a TAGGED reflected struct (which
	 *  cost ~3-4x per value and made struct-of-FixedPoint arrays bloat ~15x on disk).
	 *  Determinism-neutral: the int64 value is already platform-stable. Content that
	 *  predated this serializer was migrated to the native layout in July 2026. */
	bool Serialize(FArchive& Ar)
	{
		Ar << Value;
		return true;
	}

private:
	/** Reinterpret a raw two's-complement bit pattern without relying on the
	 *  implementation-defined unsigned-to-signed integer conversion. */
	static FORCEINLINE int64 FromRawBits(uint64 Bits)
	{
		return BitCast<int64>(Bits);
	}

	static FORCEINLINE uint64 ToRawBits(int64 Raw)
	{
		return static_cast<uint64>(Raw);
	}

	// Portable deterministic 128-by-64 division. Using this on every compiler
	// keeps quotient semantics identical and avoids Clang's compiler-rt
	// __divti3/__udivti3 dependency, which launcher UE builds do not export.
	static FORCEINLINE int64 Div128By64(int64 NumeratorHigh, uint64 NumeratorLow, int64 Divisor)
	{
		// Handle signs
		bool ResultNegative = (NumeratorHigh < 0) != (Divisor < 0);

		// Take the absolute value of the full 128-bit numerator by applying
		// two's-complement negation across both halves.
		uint64 AbsHigh;
		uint64 AbsLow;
		if (NumeratorHigh < 0)
		{
			AbsLow  = (~static_cast<uint64>(NumeratorLow)) + 1;
			AbsHigh = (~static_cast<uint64>(NumeratorHigh)) + (AbsLow == 0 ? 1 : 0);
		}
		else
		{
			AbsHigh = static_cast<uint64>(NumeratorHigh);
			AbsLow  = NumeratorLow;
		}

		const uint64 DivisorBits = ToRawBits(Divisor);
		const uint64 AbsDivisor = Divisor < 0 ? (0ULL - DivisorBits) : DivisorBits;
		
		// Binary long division (128-bit by 64-bit)
		uint64 Quotient = 0;
		uint64 Remainder = 0;
		
		for (int i = 127; i >= 0; --i)
		{
			// Shift remainder left by 1 and add next bit from dividend
			Remainder <<= 1;
			if (i >= 64) Remainder |= (AbsHigh >> (i - 64)) & 1;
			else Remainder |= (AbsLow >> i) & 1;
			
			// If remainder >= divisor, subtract and set quotient bit
			if (Remainder >= AbsDivisor)
			{
				Remainder -= AbsDivisor;
				if (i < 64) Quotient |= (1ULL << i);
			}
		}
		
		// Apply the sign in unsigned two's-complement space. This also defines
		// the otherwise-overflowing -INT64_MIN case and preserves the low 64
		// quotient bits used by the existing Win64 implementation.
		const uint64 ResultBits = ResultNegative ? (0ULL - Quotient) : Quotient;
		return FromRawBits(ResultBits);
	}

public:

	// Operators
	// ============================================================================================================

	// Implicit conversion to int64 for internal operations
	operator int64() const { return Value; }

	// Basic arithmetic operators
	/** Arithmetic overflow is defined as raw modulo-2^64 wrap. This matches
	 *  the framework's established Win64 behavior while avoiding signed UB. */
	FORCEINLINE FFixedPoint operator+(const FFixedPoint& Other) const
	{
		return FFixedPoint(FromRawBits(ToRawBits(Value) + ToRawBits(Other.Value)));
	}
	FORCEINLINE FFixedPoint operator-(const FFixedPoint& Other) const
	{
		return FFixedPoint(FromRawBits(ToRawBits(Value) - ToRawBits(Other.Value)));
	}
	FORCEINLINE FFixedPoint operator-() const
	{
		return FFixedPoint(FromRawBits(0ULL - ToRawBits(Value)));
	}
	FORCEINLINE FFixedPoint operator*(const FFixedPoint& Other) const 
	{ 
		// 64-bit multiplication requires 128-bit intermediate to avoid overflow
		#if defined(__GNUC__) || defined(__clang__)
			// The signed product fits in int128. Convert its bit pattern to
			// unsigned before shifting so a negative right shift is fully defined.
			const __int128 Product = static_cast<__int128>(Value) * static_cast<__int128>(Other.Value);
			const unsigned __int128 ProductBits = static_cast<unsigned __int128>(Product);
			return FFixedPoint(FromRawBits(static_cast<uint64>(ProductBits >> 32)));
		#elif defined(_MSC_VER)
			// MSVC: use _mul128 intrinsic
			int64 High;
			const int64 Low = _mul128(Value, Other.Value, &High);
			// Assemble the low 64 bits of the shifted product in unsigned
			// space. Shifting a negative signed High was undefined C++.
			const uint64 ShiftedBits = (ToRawBits(High) << 32) | (ToRawBits(Low) >> 32);
			return FFixedPoint(FromRawBits(ShiftedBits));
		#else
			#error "Platform does not support 128-bit multiplication"
		#endif
	}
	
	FORCEINLINE FFixedPoint operator/(const FFixedPoint& Other) const 
	{ 
		// Division by zero check
		check(Other.Value != 0 && "FFixedPoint division by zero");
		if (Other.Value == 0) return FFixedPoint::MaxValue; // Fallback for shipping builds
		
		// Create the signed Value << 32 numerator by assembling its two halves,
		// then use the shared software divider. This is bit-identical across the
		// supported MSVC and Clang toolchains.
		#if defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
			const uint64 ValueBits = ToRawBits(Value);
			uint64 HighBits = ValueBits >> 32;
			if (Value < 0) HighBits |= 0xFFFFFFFF00000000ULL;
			const int64 High = FromRawBits(HighBits);
			const uint64 Low = ValueBits << 32;
			return FFixedPoint(Div128By64(High, Low, Other.Value));
		#else
			#error "Platform does not support 128-bit division"
		#endif
	}

	// Comparison operators
	FORCEINLINE bool operator==(const FFixedPoint& Other) const { return Value == Other.Value; }
	FORCEINLINE bool operator!=(const FFixedPoint& Other) const { return Value != Other.Value; }
	FORCEINLINE bool operator<(const FFixedPoint& Other) const { return Value < Other.Value; }
	FORCEINLINE bool operator<=(const FFixedPoint& Other) const { return Value <= Other.Value; }
	FORCEINLINE bool operator>(const FFixedPoint& Other) const { return Value > Other.Value; }
	FORCEINLINE bool operator>=(const FFixedPoint& Other) const { return Value >= Other.Value; }

	// Assignment operators
	FORCEINLINE FFixedPoint& operator+=(const FFixedPoint& Other) { *this = *this + Other; return *this; }
	FORCEINLINE FFixedPoint& operator-=(const FFixedPoint& Other) { *this = *this - Other; return *this; }
	FORCEINLINE FFixedPoint& operator*=(const FFixedPoint& Other) { *this = *this * Other; return *this; }
	FORCEINLINE FFixedPoint& operator/=(const FFixedPoint& Other) { *this = *this / Other; return *this; }

	// Factory methods
	FORCEINLINE static FFixedPoint FromInt(int32 IntValue)
	{
		return FFixedPoint(FromRawBits(static_cast<uint64>(static_cast<int64>(IntValue)) << 32));
	}
	/** Values outside the representable 32-bit integer range retain the
	 *  established low-32-bit wrapping behavior. */
	FORCEINLINE static FFixedPoint FromInt64(int64 IntValue)
	{
		return FFixedPoint(FromRawBits(static_cast<uint64>(IntValue) << 32));
	}
	
	/**
	 * WARNING: NON-DETERMINISTIC! Only use for editor/visualization purposes.
	 * Floating-point operations may produce different results across platforms/compilers.
	 * NEVER use this in simulation logic - use FromInt() instead.
	 */
	FORCEINLINE static FFixedPoint FromFloat(float FloatValue)
	{
		if (FMath::IsNaN(FloatValue)) return FFixedPoint::Zero;
		if (!FMath::IsFinite(FloatValue))
		{
			return FloatValue < 0.0f ? FFixedPoint::MinValue : FFixedPoint::MaxValue;
		}

		const double Scaled = static_cast<double>(FloatValue) * 4294967296.0;
		if (Scaled >= static_cast<double>(INT64_MAX)) return FFixedPoint::MaxValue;
		if (Scaled <= static_cast<double>(INT64_MIN)) return FFixedPoint::MinValue;
		return FFixedPoint(static_cast<int64>(Scaled));
	}

	// Conversion methods
	// ===========================================================================================

	/**
	 * To int32. 
	 * 
	 * Be aware of potential overflow if the fixed-point value exceeds the representable range of int32.
	 */
	FORCEINLINE int32 ToInt() const
	{
		const uint32 IntegerBits = static_cast<uint32>(ToRawBits(Value) >> 32);
		return BitCast<int32>(IntegerBits);
	}

	/**
	 * To int64. 
	 * 
	 * Be aware of potential overflow if the fixed-point value exceeds the representable range of int64.
	 */
	FORCEINLINE int64 ToInt64() const { return static_cast<int64>(ToInt()); }

	/**
	 * Ceil to int32 (rounds toward +infinity). Deterministic, integer-only —
	 * safe in simulation logic, unlike a ToFloat()-based ceil.
	 */
	FORCEINLINE int32 CeilToInt() const
	{
		const uint64 RoundedBits = ToRawBits(Value) + 0xFFFFFFFFULL;
		return BitCast<int32>(static_cast<uint32>(RoundedBits >> 32));
	}
	
	/**
	 * To float.
	 * 
	 * WARNING: NON-DETERMINISTIC! Only use for visualization/debugging.
	 * DO NOT use in simulation logic - results may vary across platforms.
	 */
	FORCEINLINE float ToFloat() const { return static_cast<float>(Value) / 4294967296.0f; }

	/**
	 * To string. 
	 * 
	 * WARNING: NON-DETERMINISTIC! Only use for visualization/debugging.
	 * DO NOT use in simulation logic - results may vary across platforms.
	 */
	FORCEINLINE FString ToString() const { return FString::Printf(TEXT("%.6f"), ToFloat()); }

	// Comparison utilities
	// ===========================================================================================

	/**
	 * Checks if this fixed-point value is nearly equal to another within a tolerance.
	 */
	FORCEINLINE bool IsNearlyEqual(FFixedPoint Other, FFixedPoint Tolerance = FFixedPoint::Epsilon) const
	{
		if (Tolerance.Value < 0) return false;
		const uint64 ThisBits = ToRawBits(Value);
		const uint64 OtherBits = ToRawBits(Other.Value);
		const uint64 Distance = Value >= Other.Value
			? (ThisBits - OtherBits)
			: (OtherBits - ThisBits);
		return Distance <= static_cast<uint64>(Tolerance.Value);
	}

	// Mathematical Constants (declarations)
	// ===========================================================================================

	// Fundamental constants
	static const FFixedPoint Zero;
	static const FFixedPoint One;              // 1.0
	static const FFixedPoint Half;             // 0.5
	static const FFixedPoint Two;              // 2.0
	
	// Pi and related constants
	static const FFixedPoint Pi;               // 3.14159265359 (π)
	static const FFixedPoint TwoPi;            // 6.28318530718 (2π)
	static const FFixedPoint HalfPi;           // 1.57079632679 (π/2)
	static const FFixedPoint QuarterPi;        // 0.78539816339 (π/4)
	static const FFixedPoint InvPi;            // 0.31830988618 (1/π)
	static const FFixedPoint InvTwoPi;         // 0.15915494309 (1/2π)
	
	// Euler's number and related
	static const FFixedPoint E;                // 2.71828182846 (e)
	static const FFixedPoint Log2E;            // 1.44269504089 (log₂(e))
	static const FFixedPoint Log10E;           // 0.43429448190 (log₁₀(e))
	static const FFixedPoint Ln2;              // 0.69314718056 (ln(2))
	static const FFixedPoint Ln10;             // 2.30258509299 (ln(10))
	
	// Square roots
	static const FFixedPoint Sqrt2;            // 1.41421356237 (√2)
	static const FFixedPoint Sqrt3;            // 1.73205080757 (√3)
	static const FFixedPoint SqrtHalf;         // 0.70710678118 (√0.5 = 1/√2)
	static const FFixedPoint InvSqrt2;         // 0.70710678118 (1/√2)
	static const FFixedPoint InvSqrt3;         // 0.57735026919 (1/√3)
	
	// Golden ratio
	static const FFixedPoint Phi;              // 1.61803398875 (φ)
	static const FFixedPoint InvPhi;           // 0.61803398875 (1/φ)
	
	// Angle conversions
	static const FFixedPoint DegToRad;         // 0.01745329252 (π/180)
	static const FFixedPoint RadToDeg;         // 57.2957795131 (180/π)
	
	// Tolerance values
	static const FFixedPoint SmallNumber;      // ~2.328e-10 - smallest non-zero fractional value (1/2^32)
	static const FFixedPoint KindaSmallNumber; // ~0.01
	static const FFixedPoint BigNumber;        // Large number for range checking
	static const FFixedPoint Epsilon;          // ~2.328e-8 - tolerance for equality comparisons
	
	// Numeric limits
	static const FFixedPoint MaxValue;         // Maximum representable value (~2,147,483,647.0)
	static const FFixedPoint MinValue;         // Minimum representable value (~-2,147,483,648.0)
	
	// Common fractions
	static const FFixedPoint Third;            // 0.33333333333 (1/3)
	static const FFixedPoint TwoThirds;        // 0.66666666667 (2/3)
	static const FFixedPoint Quarter;          // 0.25 (1/4)
	static const FFixedPoint ThreeQuarters;    // 0.75 (3/4)
	
	// Conversion helpers
	static constexpr int32 Shift = 32;							// Bit shift for conversions
	static constexpr int64 FractionalMask = 0xFFFFFFFFLL;		// Mask for fractional part
	static constexpr int64 IntegerMask = 0xFFFFFFFF00000000LL;	// Mask for integer part
};

/** Hash function for FFixedPoint so it can be used in TMap/TSet and state hashing */
FORCEINLINE uint32 GetTypeHash(const FFixedPoint& FP)
{
	return GetTypeHash(FP.Value);
}

/** Enable FFixedPoint's native binary serializer (Serialize above) so every
 *  FFixedPoint instance / array serializes as a raw int64 blob rather than as a
 *  tagged reflected struct. This is what keeps fixed-point data compact on disk. */
template<>
struct TStructOpsTypeTraits<FFixedPoint> : public TStructOpsTypeTraitsBase2<FFixedPoint>
{
	enum
	{
		WithSerializer = true,
	};
};

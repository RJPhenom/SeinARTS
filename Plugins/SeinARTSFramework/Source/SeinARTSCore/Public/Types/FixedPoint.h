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

private:

	// Helper: Deterministic 128-bit division for MSVC (pure integer implementation)
	static FORCEINLINE int64 Div128By64(int64 NumeratorHigh, uint64 NumeratorLow, int64 Divisor)
	{
		// Handle signs
		bool ResultNegative = (NumeratorHigh < 0) != (Divisor < 0);

		// Take the absolute value of the FULL 128-bit numerator. The 128-bit value
		// is (NumeratorHigh << 64) | NumeratorLow interpreted as int128. Negation
		// is a 128-bit two's complement: bitwise-NOT the unsigned bit pattern of
		// both halves, then add 1 to the low half with carry into the high half.
		// (The previous version mistakenly started AbsHigh at -NumeratorHigh and
		// then re-negated, producing a wrong unsigned magnitude for negative
		// numerators — every negative-numerator divide returned garbage, which
		// is what made the X component of normalized direction vectors explode
		// when the click was W/SW/NW of the unit.)
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

		uint64 AbsDivisor = Divisor < 0 ? (Divisor == INT64_MIN ? (uint64)INT64_MAX + 1 : -Divisor) : Divisor;
		
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
		
		// Apply sign
		int64 Result = static_cast<int64>(Quotient);
		return ResultNegative ? -Result : Result;
	}

public:

	// Operators
	// ============================================================================================================

	// Implicit conversion to int64 for internal operations
	operator int64() const { return Value; }

	// Basic arithmetic operators
	FORCEINLINE FFixedPoint operator+(const FFixedPoint& Other) const { return FFixedPoint(Value + Other.Value); }
	FORCEINLINE FFixedPoint operator-(const FFixedPoint& Other) const { return FFixedPoint(Value - Other.Value); }
	FORCEINLINE FFixedPoint operator-() const { return FFixedPoint(-Value); }
	FORCEINLINE FFixedPoint operator*(const FFixedPoint& Other) const 
	{ 
		// 64-bit multiplication requires 128-bit intermediate to avoid overflow
		#if defined(__GNUC__) || defined(__clang__)
			// GCC/Clang: use __int128
			__int128 Result = (static_cast<__int128>(Value) * static_cast<__int128>(Other.Value)) >> 32;
			return FFixedPoint(static_cast<int64>(Result));
		#elif defined(_MSC_VER)
			// MSVC: use _mul128 intrinsic
			int64 High;
			int64 Low = _mul128(Value, Other.Value, &High);
			// Combine high and low parts after shift: (High << 32) | (Low >> 32)
			return FFixedPoint((High << 32) | (static_cast<uint64>(Low) >> 32));
		#else
			#error "Platform does not support 128-bit multiplication"
		#endif
	}
	
	FORCEINLINE FFixedPoint operator/(const FFixedPoint& Other) const 
	{ 
		// Division by zero check
		check(Other.Value != 0 && "FFixedPoint division by zero");
		if (Other.Value == 0) return FFixedPoint::MaxValue; // Fallback for shipping builds
		
		// 64-bit division requires 128-bit intermediate for precision
		#if defined(__GNUC__) || defined(__clang__)
			// GCC/Clang: use __int128
			__int128 Numerator = static_cast<__int128>(Value) << 32;
			return FFixedPoint(static_cast<int64>(Numerator / Other.Value));
		#elif defined(_MSC_VER)
			// MSVC: deterministic software 128-bit division
			// Create 128-bit numerator: Value << 32
			int64 High = Value >> 32;      // Sign-extended upper bits
			uint64 Low = static_cast<uint64>(Value) << 32;  // Lower bits shifted
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
	FORCEINLINE FFixedPoint& operator+=(const FFixedPoint& Other) { Value += Other.Value; return *this; }
	FORCEINLINE FFixedPoint& operator-=(const FFixedPoint& Other) { Value -= Other.Value; return *this; }
	FORCEINLINE FFixedPoint& operator*=(const FFixedPoint& Other) { *this = *this * Other; return *this; }
	FORCEINLINE FFixedPoint& operator/=(const FFixedPoint& Other) { *this = *this / Other; return *this; }

	// Factory methods
	FORCEINLINE static FFixedPoint FromInt(int32 IntValue) { return FFixedPoint(static_cast<int64>(IntValue) << 32); }
	FORCEINLINE static FFixedPoint FromInt64(int64 IntValue) { return FFixedPoint(IntValue << 32); }
	
	/**
	 * WARNING: NON-DETERMINISTIC! Only use for editor/visualization purposes.
	 * Floating-point operations may produce different results across platforms/compilers.
	 * NEVER use this in simulation logic - use FromInt() instead.
	 */
	FORCEINLINE static FFixedPoint FromFloat(float FloatValue) { return FFixedPoint(static_cast<int64>(FloatValue * 4294967296.0)); }

	// Conversion methods
	// ===========================================================================================

	/**
	 * To int32. 
	 * 
	 * Be aware of potential overflow if the fixed-point value exceeds the representable range of int32.
	 */
	FORCEINLINE int32 ToInt() const { return static_cast<int32>(Value >> 32); }

	/**
	 * To int64. 
	 * 
	 * Be aware of potential overflow if the fixed-point value exceeds the representable range of int64.
	 */
	FORCEINLINE int64 ToInt64() const { return Value >> 32; }
	
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
		FFixedPoint Diff = *this - Other;
		return (Diff < 0 ? -Diff : Diff) <= Tolerance;
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
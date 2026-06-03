/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		MathLib.h
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		Deterministic fixed-point math library for lockstep
 * 				simulation. Provides the SeinMath namespace containing
 * 				square root (Newton-Raphson), absolute value, min/max/clamp,
 * 				floor/ceil/round, modulo, linear and Hermite interpolation,
 * 				integer power, natural exp (Pade approximant), natural/base-2/
 * 				base-10 logarithm, and trigonometric functions (sin, cos, tan,
 * 				asin, acos, atan, atan2) driven by a 1024-entry quarter-circle
 * 				lookup table for full cross-platform determinism.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"

namespace SeinMath
{
	// Square root using Newton-Raphson approximation (faster than bit-by-bit)
	FORCEINLINE FFixedPoint Sqrt(FFixedPoint X)
	{
		check(X >= 0 && "Sqrt of negative number");
		if (X <= 0) return FFixedPoint::Zero;
		if (X == FFixedPoint::One) return FFixedPoint::One;

		// Initial guess using bit manipulation (find highest bit of the raw 32.32
		// value, then take 2^(HighBit/2) as a starting point near sqrt(X)).
		int64 Val = static_cast<int64>(X);
		int32 HighBit = 63;
		while (HighBit >= 0 && ((Val >> HighBit) & 1) == 0)
		{
			HighBit--;
		}
		FFixedPoint Guess(1LL << ((HighBit + 32) / 2));

		// Newton-Raphson: x_new = (x + N/x) / 2.
		// Route the divide through FFixedPoint::operator/, which uses the
		// deterministic 128-bit divide. The previous implementation used
		// `(Val << 32) / Guess` in raw int64 — that overflows for any X >= 0.5
		// (Val << 32 needs 96+ bits for distance-squared values), producing
		// garbage Quotient and a useless converged "guess" that's nowhere near
		// sqrt(X). For values in the 25..1e9 range (typical squared distances
		// in cm world units) the function returned values 100x to 10000x off.
		// Bit-shift halving still works on the FFixedPoint raw int64 because
		// halving the raw representation halves the represented value.
		for (int32 i = 0; i < 4; ++i)
		{
			if (static_cast<int64>(Guess) == 0) break;
			const FFixedPoint Quotient = X / Guess;
			Guess = FFixedPoint((static_cast<int64>(Guess) + static_cast<int64>(Quotient)) >> 1);
		}

		return Guess;
	}
	
	// Inverse square root (1/sqrt(x)) - useful for fast normalization
	FORCEINLINE FFixedPoint InverseSqrt(FFixedPoint X)
	{
		check(X > 0 && "InverseSqrt of non-positive number");
		if (X <= 0) return FFixedPoint::MaxValue;
		
		// Use 1 / Sqrt(X) with Newton-Raphson for better precision
		FFixedPoint SqrtX = Sqrt(X);
		if (SqrtX == 0) return FFixedPoint::MaxValue;
		return FFixedPoint::One / SqrtX;
	}

	// Absolute value
	FORCEINLINE FFixedPoint Abs(FFixedPoint X)
	{
		return FFixedPoint(X < 0 ? -X : X);
	}

	// Minimum
	FORCEINLINE FFixedPoint Min(FFixedPoint A, FFixedPoint B)
	{
		return A < B ? A : B;
	}

	// Maximum
	FORCEINLINE FFixedPoint Max(FFixedPoint A, FFixedPoint B)
	{
		return A > B ? A : B;
	}

	// Clamp
	FORCEINLINE FFixedPoint Clamp(FFixedPoint Value, FFixedPoint Min, FFixedPoint Max)
	{
		if (Value < Min) return Min;
		if (Value > Max) return Max;
		return Value;
	}

	// Integer variants for platform-independent clamping
	FORCEINLINE int32 Min(int32 A, int32 B) { return A < B ? A : B; }
	FORCEINLINE int32 Max(int32 A, int32 B) { return A > B ? A : B; }
	FORCEINLINE int64 Min(int64 A, int64 B) { return A < B ? A : B; }
	FORCEINLINE int64 Max(int64 A, int64 B) { return A > B ? A : B; }
	FORCEINLINE int64 Clamp(int64 Value, int64 Min, int64 Max)
	{
		if (Value < Min) return Min;
		if (Value > Max) return Max;
		return Value;
	}

	// Sign - returns -1, 0, or 1
	FORCEINLINE FFixedPoint Sign(FFixedPoint X)
	{
		if (X > 0) return FFixedPoint::One;
		if (X < 0) return -FFixedPoint::One;
		return FFixedPoint::Zero;
	}

	// Floor - rounds down to nearest integer
	FORCEINLINE FFixedPoint Floor(FFixedPoint X)
	{
		// Mask off fractional bits (lower 32 bits), adjust for negative numbers
		int64 Val = static_cast<int64>(X);
		if (X < 0 && (Val & 0xFFFFFFFFLL) != 0)
		{
			// Negative number with fractional part - round down (more negative)
			return FFixedPoint((Val & 0xFFFFFFFF00000000LL) - 0x100000000LL);
		}
		return FFixedPoint(Val & 0xFFFFFFFF00000000LL);
	}

	// Ceil - rounds up to nearest integer
	FORCEINLINE FFixedPoint Ceil(FFixedPoint X)
	{
		int64 Val = static_cast<int64>(X);
		// If there's a fractional part and number is positive, round up
		if ((Val & 0xFFFFFFFFLL) != 0 && Val > 0)
		{
			return FFixedPoint((Val & 0xFFFFFFFF00000000LL) + 0x100000000LL);
		}
		return FFixedPoint(Val & 0xFFFFFFFF00000000LL);
	}

	// Round - rounds to nearest integer (0.5 rounds up)
	FORCEINLINE FFixedPoint Round(FFixedPoint X)
	{
		int64 Val = static_cast<int64>(X);
		int64 Frac = Val & 0xFFFFFFFFLL;
		int64 Integer = Val & 0xFFFFFFFF00000000LL;
		
		// Check if fractional part is >= 0.5 (0x80000000 in 32.32 fixed-point)
		if (Val >= 0)
		{
			// Positive: add 1 if frac >= 0.5
			if (Frac >= 0x80000000LL)
			{
				return FFixedPoint(Integer + 0x100000000LL);
			}
		}
		else
		{
			// Negative: subtract 1 if frac >= 0.5 (in absolute terms)
			if (Frac >= 0x80000000LL)
			{
				return FFixedPoint(Integer - 0x100000000LL);
			}
		}
		
		return FFixedPoint(Integer);
	}

	// Modulo - deterministic remainder operation
	FORCEINLINE FFixedPoint Mod(FFixedPoint X, FFixedPoint Y)
	{
		if (Y == 0) return FFixedPoint::Zero;
		
		int64 XVal = static_cast<int64>(X);
		int64 YVal = static_cast<int64>(Y);
		
		// C++ % operator for fixed-point values
		int64 Result = XVal % YVal;
		
		// Ensure result has same sign as divisor (Euclidean modulo behavior)
		if (Result != 0 && ((Result ^ YVal) < 0))
		{
			Result += YVal;
		}
		
		return FFixedPoint(Result);
	}

	// Frac - returns fractional part of a number
	FORCEINLINE FFixedPoint Frac(FFixedPoint X)
	{
		return X - Floor(X);
	}

	// Interpolation
	// ===============================================================================================================

	// Linear interpolation
	FORCEINLINE FFixedPoint Lerp(FFixedPoint A, FFixedPoint B, FFixedPoint Alpha)
	{
		return A + (B - A) * Alpha;
	}

	// Inverse linear interpolation - finds Alpha given A, B, and Value
	FORCEINLINE FFixedPoint InverseLerp(FFixedPoint A, FFixedPoint B, FFixedPoint Value)
	{
		if (A == B) return FFixedPoint::Zero;
		return (Value - A) / (B - A);
	}

	// Smooth Hermite interpolation (cubic ease in/out)
	FORCEINLINE FFixedPoint SmoothStep(FFixedPoint A, FFixedPoint B, FFixedPoint Alpha)
	{
		// Clamp alpha to [0, 1]
		FFixedPoint T = Clamp(Alpha, FFixedPoint::Zero, FFixedPoint::One);
		// Smoothstep formula: 3t² - 2t³
		FFixedPoint T2 = T * T;
		FFixedPoint T3 = T2 * T;
		FFixedPoint SmoothAlpha = (T2 * FFixedPoint::FromInt(3)) - (T3 * FFixedPoint::FromInt(2));
		return Lerp(A, B, SmoothAlpha);
	}

	// Power and Exponential Functions
	// ===============================================================================================================

	// Power function with integer exponent (fast exponentiation by squaring)
	FORCEINLINE FFixedPoint Pow(FFixedPoint Base, int32 Exponent)
	{
		if (Exponent == 0) return FFixedPoint::One;
		
		// Handle negative exponents
		bool IsNegative = Exponent < 0;
		int32 Exp = IsNegative ? -Exponent : Exponent;
		
		FFixedPoint Result = FFixedPoint::One;
		FFixedPoint CurrentBase = Base;
		
		// Binary exponentiation for O(log n) complexity
		while (Exp > 0)
		{
			if (Exp & 1)
			{
				Result = Result * CurrentBase;
			}
			CurrentBase = CurrentBase * CurrentBase;
			Exp >>= 1;
		}
		
		return IsNegative ? (FFixedPoint::One / Result) : Result;
	}

	// Natural exponential function (e^x) using Padé approximation for determinism
	// Valid range: approximately -10 to +10
	FORCEINLINE FFixedPoint Exp(FFixedPoint X)
	{
		// Range reduction: e^x = e^(n*ln(2) + r) = 2^n * e^r
		// where |r| < ln(2)/2 ≈ 0.347
		
		// Ln(2) in fixed point
		const FFixedPoint Ln2 = FFixedPoint::Ln2;
		
		// Calculate n = floor(x / ln(2))
		FFixedPoint XDivLn2 = X / Ln2;
		int32 N = XDivLn2.ToInt();
		
		// Calculate r = x - n*ln(2)
		FFixedPoint R = X - FFixedPoint::FromInt(N) * Ln2;
		
		// Padé approximant [5/5] for e^r where |r| < ln(2)/2
		// Numerator: 1 + r/2 + r²/9 + r³/72 + r⁴/1008 + r⁵/30240
		// Denominator: 1 - r/2 + r²/9 - r³/72 + r⁴/1008 - r⁵/30240
		
		const FFixedPoint R2 = R * R;
		const FFixedPoint R3 = R2 * R;
		const FFixedPoint R4 = R3 * R;
		const FFixedPoint R5 = R4 * R;
		
		// Calculate numerator
		FFixedPoint Num = FFixedPoint::One;
		Num = Num + R / FFixedPoint::FromInt(2);
		Num = Num + R2 / FFixedPoint::FromInt(9);
		Num = Num + R3 / FFixedPoint::FromInt(72);
		Num = Num + R4 / FFixedPoint::FromInt(1008);
		Num = Num + R5 / FFixedPoint::FromInt(30240);
		
		// Calculate denominator
		FFixedPoint Den = FFixedPoint::One;
		Den = Den - R / FFixedPoint::FromInt(2);
		Den = Den + R2 / FFixedPoint::FromInt(9);
		Den = Den - R3 / FFixedPoint::FromInt(72);
		Den = Den + R4 / FFixedPoint::FromInt(1008);
		Den = Den - R5 / FFixedPoint::FromInt(30240);
		
		// e^r ≈ Num / Den
		FFixedPoint ExpR = Num / Den;
		
		// Multiply by 2^n using bit shift
		// 2^n in fixed point = 1 << (32 + n)
		if (N >= 0)
		{
			// For positive n, shift left
			if (N < 32) // Prevent overflow
			{
				int64 ScaledResult = static_cast<int64>(ExpR) << N;
				return FFixedPoint(ScaledResult);
			}
			else
			{
				return FFixedPoint::MaxValue; // Overflow
			}
		}
		else
		{
			// For negative n, shift right
			if (N > -64) // Prevent underflow to zero
			{
				int64 ScaledResult = static_cast<int64>(ExpR) >> (-N);
				return FFixedPoint(ScaledResult);
			}
			else
			{
				return FFixedPoint::Zero; // Underflow
			}
		}
	}

	// Natural logarithm (ln(x)) using bit analysis + polynomial approximation
	// Valid range: x > 0
	FORCEINLINE FFixedPoint Log(FFixedPoint X)
	{
		check(X > 0 && "Log of non-positive number");
		if (X <= 0) return -FFixedPoint::MaxValue; // Fallback for shipping builds
		if (X == FFixedPoint::One) return FFixedPoint::Zero;
		
		int64 Val = static_cast<int64>(X);
		
		// Find the integer part: ln(x) = ln(2^n * m) = n*ln(2) + ln(m)
		// where 1 <= m < 2 (normalized mantissa)
		
		// Count leading zeros to find the exponent
		int32 Exponent = 0;
		int64 AbsVal = Val < 0 ? -Val : Val;
		
		// Find position of highest set bit (this is our n in 2^n)
		int32 HighBit = 63;
		while (HighBit >= 0 && ((AbsVal >> HighBit) & 1) == 0)
		{
			HighBit--;
		}
		
		// Exponent relative to fixed-point format (bit 32 represents 1.0)
		Exponent = HighBit - 32;
		
		// Extract mantissa: shift value so that 1.0 <= mantissa < 2.0
		int64 Mantissa;
		if (Exponent >= 0)
		{
			Mantissa = Val >> Exponent;
		}
		else
		{
			Mantissa = Val << (-Exponent);
		}
		
		// Now mantissa is in range [2^32, 2^33), representing [1.0, 2.0)
		// Calculate ln(mantissa) using polynomial approximation
		// ln(1+x) ≈ x - x²/2 + x³/3 - x⁴/4 + x⁵/5 for |x| < 1
		
		// Shift mantissa to be in range [0, 1) by subtracting 1.0
		FFixedPoint M = FFixedPoint(Mantissa - (1LL << 32)); // M = mantissa - 1.0
		
		// Polynomial approximation: ln(1+M)
		const FFixedPoint M2 = M * M;
		const FFixedPoint M3 = M2 * M;
		const FFixedPoint M4 = M3 * M;
		const FFixedPoint M5 = M4 * M;
		
		FFixedPoint LnMantissa = M;
		LnMantissa = LnMantissa - M2 / FFixedPoint::FromInt(2);
		LnMantissa = LnMantissa + M3 / FFixedPoint::FromInt(3);
		LnMantissa = LnMantissa - M4 / FFixedPoint::FromInt(4);
		LnMantissa = LnMantissa + M5 / FFixedPoint::FromInt(5);
		
		// Result: ln(x) = n*ln(2) + ln(mantissa)
		return FFixedPoint::FromInt(Exponent) * FFixedPoint::Ln2 + LnMantissa;
	}

	// Logarithm base 2
	FORCEINLINE FFixedPoint Log2(FFixedPoint X)
	{
		return Log(X) / FFixedPoint::Ln2;
	}

	// Logarithm base 10
	FORCEINLINE FFixedPoint Log10(FFixedPoint X)
	{
		return Log(X) / FFixedPoint::Ln10;
	}

	// Sine/Cos Lookup Table
	// ===============================================================================================================
    // 1024 entries for 0 to 90 degrees (quarter circle)
	// Values are in fixed-point format (32.32), calculated as sin(i * π/2 / 1024) * 2^32
	static constexpr int32 SIN_TABLE_SIZE = 1024;
	extern SEINARTSCORE_API const int64 SIN_TABLE[SIN_TABLE_SIZE + 1];

	// Trigonometry
	// ===============================================================================================================

	// Sin using lookup table - input in radians (fixed-point)
	FORCEINLINE FFixedPoint Sin(FFixedPoint Radians)
	{
		// Normalize to [0, 2π] using deterministic modulo
		int64 Val = static_cast<int64>(Radians);
		const int64 TwoPi = static_cast<int64>(FFixedPoint::TwoPi);
		
		// Efficient modulo 2π (handles large angles)
		Val = Val % TwoPi;
		if (Val < 0) Val += TwoPi;
		
		// Determine quadrant and adjust
		bool Negative = false;
		bool UseComplement = false;
		
		// Quadrants 3-4 (π to 2π): negative
		if (Val >= FFixedPoint::Pi)
		{
			Val -= FFixedPoint::Pi;
			Negative = true;
		}
		
		// Quadrants 2 and 4 (π/2 to π, 3π/2 to 2π): use complement
		if (Val > FFixedPoint::HalfPi)
		{
			Val = FFixedPoint::Pi - Val;
			UseComplement = true;
		}
		
		// Convert to table index (0 to SIN_TABLE_SIZE maps to 0 to π/2)
		int64 Index = (static_cast<int64>(Val) * SIN_TABLE_SIZE) / FFixedPoint::HalfPi;
		if (Index >= SIN_TABLE_SIZE) Index = SIN_TABLE_SIZE;
		
		int64 Result = SIN_TABLE[Index];
		return FFixedPoint(Negative ? -Result : Result);
	}

	// Cosine using lookup table - cos(x) = sin(x + π/2)
	FORCEINLINE FFixedPoint Cos(FFixedPoint Radians)
	{
		return Sin(FFixedPoint(Radians + FFixedPoint::HalfPi));
	}

	// Tangent - tan(x) = sin(x) / cos(x)
	FORCEINLINE FFixedPoint Tan(FFixedPoint Radians)
	{
		FFixedPoint CosVal = Cos(Radians);
		if (CosVal == 0) return FFixedPoint::MaxValue; // Return max for undefined
		return Sin(Radians) / CosVal;
	}

	// Inverse Trigonometry — three-stage range reduction so the polynomial
	// always operates on a small argument (|A| <= 1/3 worst case), where the
	// truncated Maclaurin series is accurate to ~10⁻⁶. WITHOUT the half-angle
	// step the polynomial near |x|=1 had ~0.09 rad error (1/11 from truncating
	// at x⁹/9), which manifested as a periodic per-tick yaw twitch in the
	// wheeled-vehicle YawFromRotation read-back path — same forward vector
	// went in, slightly-wrong yaw came out.
	//
	// Reductions, in order:
	//   1. Sign:        atan(-x) = -atan(x)               (handle negation)
	//   2. Reciprocal:  atan(x) = π/2 - atan(1/x)         for |x| > 1
	//   3. Half-angle:  atan(A) = π/4 + atan((A-1)/(A+1)) for A > 1/2
	// After (3), the polynomial input is always in (-1/3, 1/2].
	FORCEINLINE FFixedPoint Atan(FFixedPoint X)
	{
		// 1. Sign — atan is odd.
		bool bNegate = false;
		FFixedPoint A = X;
		if (A < FFixedPoint::Zero) { A = -A; bNegate = true; }

		// 2. Reciprocal — collapse |x| > 1 to (0, 1].
		bool bComplement = false;
		if (A > FFixedPoint::One)
		{
			A = FFixedPoint::One / A;
			bComplement = true;
		}

		// 3. Half-angle — the load-bearing fix. Without this, A in [0.5, 1]
		//    feeds the polynomial near its convergence boundary and accumulates
		//    error on the order of 1/(2N+1) where N = polynomial order.
		bool bHalfAngle = false;
		if (A > FFixedPoint::Half)
		{
			A = (A - FFixedPoint::One) / (A + FFixedPoint::One);
			bHalfAngle = true;
		}

		// Polynomial: atan(A) ≈ A - A³/3 + A⁵/5 - A⁷/7 + A⁹/9
		// For |A| <= 1/3 (our worst case after reductions), 5 terms gives
		// ~10⁻⁶ absolute error — comfortably below sin/cos table precision.
		const FFixedPoint A2 = A * A;
		const FFixedPoint A3 = A * A2;
		const FFixedPoint A5 = A3 * A2;
		const FFixedPoint A7 = A5 * A2;
		const FFixedPoint A9 = A7 * A2;

		FFixedPoint Result = A;
		Result = Result - A3 / FFixedPoint(3 * FFixedPoint::One);
		Result = Result + A5 / FFixedPoint(5 * FFixedPoint::One);
		Result = Result - A7 / FFixedPoint(7 * FFixedPoint::One);
		Result = Result + A9 / FFixedPoint(9 * FFixedPoint::One);

		// Unwind reductions in reverse.
		if (bHalfAngle)  Result = Result + FFixedPoint::QuarterPi;
		if (bComplement) Result = FFixedPoint::HalfPi - Result;
		if (bNegate)     Result = -Result;
		return Result;
	}

	// Atan2 - returns angle in radians from -π to π
	FORCEINLINE FFixedPoint Atan2(FFixedPoint Y, FFixedPoint X)
	{
		if (X == 0)
		{
			if (Y > 0) return FFixedPoint::HalfPi;
			if (Y < 0) return -FFixedPoint::HalfPi;
			return FFixedPoint(0);
		}
		
		if (X > 0)
		{
			return Atan(Y / X);
		}
		else
		{
			if (Y >= 0)
				return FFixedPoint::Pi + Atan(Y / X);
			else
				return -FFixedPoint::Pi + Atan(Y / X);
		}
	}

	// Asin using Newton-Raphson method
	FORCEINLINE FFixedPoint Asin(FFixedPoint X)
	{
		const int64 AbsX = X < 0 ? -X : X;
		if (AbsX > FFixedPoint::One) return FFixedPoint(0); // Clamp
		
		// Use identity: asin(x) = atan2(x, sqrt(1 - x²))
		FFixedPoint OneMinusX2 = FFixedPoint::One - (X * X);
		
		if (OneMinusX2 <= 0) 
		{
			return X >= 0 ? FFixedPoint::HalfPi : -FFixedPoint::HalfPi;
		}
		
		// Use optimized sqrt (Newton-Raphson, much faster than bit-by-bit)
		FFixedPoint SqrtOneMinusX2 = Sqrt(OneMinusX2);
		return Atan2(X, SqrtOneMinusX2);
	}

	// Acos using identity: acos(x) = π/2 - asin(x)
	FORCEINLINE FFixedPoint Acos(FFixedPoint X)
	{
		return FFixedPoint::HalfPi - Asin(X);
	}
}

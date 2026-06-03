/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		MathBPFL.h
 * @date:		1/16/2026
 * @author:		RJ Macklem
 * @brief:		Blueprint Function Library for deterministic fixed-point math operations.
 * 				Provides Blueprint-accessible wrappers for all SeinMath functions.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Math/MathLib.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/Vector2D.h"
#include "Types/Rotator.h"
#include "Types/Quat.h"
#include "Types/Transform.h"
#include "Types/Random.h"
#include "MathBPFL.generated.h"

/**
 * Blueprint Function Library for deterministic fixed-point math operations
 */
UCLASS(meta = (DisplayName = "SeinARTS Fixed-point Math Library"))
class SEINARTSCOREENTITY_API UMathBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// FFixedPoint - Constants
	// ====================================================================================================
	
	/** Returns the constant 0.0 */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Zero"))
	static FFixedPoint Zero() { return FFixedPoint::Zero; }
	
	/** Returns the constant 1.0 */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point One"))
	static FFixedPoint One() { return FFixedPoint::One; }
	
	/** Returns the constant 0.5 */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Half"))
	static FFixedPoint Half() { return FFixedPoint::Half; }
	
	/** Returns the constant 2.0 */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Two"))
	static FFixedPoint Two() { return FFixedPoint::Two; }
	
	/** Returns Pi (3.14159265359) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Pi"))
	static FFixedPoint Pi() { return FFixedPoint::Pi; }
	
	/** Returns 2*Pi (6.28318530718) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Two Pi"))
	static FFixedPoint TwoPi() { return FFixedPoint::TwoPi; }
	
	/** Returns Pi/2 (1.57079632679) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Half Pi"))
	static FFixedPoint HalfPi() { return FFixedPoint::HalfPi; }
	
	/** Returns Pi/4 (0.78539816339) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Quarter Pi"))
	static FFixedPoint QuarterPi() { return FFixedPoint::QuarterPi; }
	
	/** Returns 1/Pi (0.31830988618) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Inverse Pi"))
	static FFixedPoint InvPi() { return FFixedPoint::InvPi; }
	
	/** Returns 1/(2*Pi) (0.15915494309) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Inverse Two Pi"))
	static FFixedPoint InvTwoPi() { return FFixedPoint::InvTwoPi; }
	
	/** Returns Euler's number e (2.71828182846) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point E"))
	static FFixedPoint E() { return FFixedPoint::E; }
	
	/** Returns log base 2 of e (1.44269504089) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Log2(E)"))
	static FFixedPoint Log2E() { return FFixedPoint::Log2E; }
	
	/** Returns log base 10 of e (0.43429448190) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Log10(E)"))
	static FFixedPoint Log10E() { return FFixedPoint::Log10E; }
	
	/** Returns natural log of 2 (0.69314718056) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Ln(2)"))
	static FFixedPoint Ln2() { return FFixedPoint::Ln2; }
	
	/** Returns natural log of 10 (2.30258509299) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Ln(10)"))
	static FFixedPoint Ln10() { return FFixedPoint::Ln10; }
	
	/** Returns square root of 2 (1.41421356237) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Sqrt(2)"))
	static FFixedPoint Sqrt2() { return FFixedPoint::Sqrt2; }
	
	/** Returns square root of 3 (1.73205080757) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Sqrt(3)"))
	static FFixedPoint Sqrt3() { return FFixedPoint::Sqrt3; }
	
	/** Returns square root of 0.5 (0.70710678118) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Sqrt(0.5)"))
	static FFixedPoint SqrtHalf() { return FFixedPoint::SqrtHalf; }
	
	/** Returns 1/sqrt(2) (0.70710678118) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point 1/Sqrt(2)"))
	static FFixedPoint InvSqrt2() { return FFixedPoint::InvSqrt2; }
	
	/** Returns 1/sqrt(3) (0.57735026919) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point 1/Sqrt(3)"))
	static FFixedPoint InvSqrt3() { return FFixedPoint::InvSqrt3; }
	
	/** Returns the golden ratio (1.61803398875) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Golden Ratio (Phi)"))
	static FFixedPoint Phi() { return FFixedPoint::Phi; }
	
	/** Returns 1 divided by the golden ratio (0.61803398875) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point 1/Phi"))
	static FFixedPoint InvPhi() { return FFixedPoint::InvPhi; }
	
	/** Returns degrees to radians conversion factor (0.01745329252) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Deg to Rad"))
	static FFixedPoint DegToRad() { return FFixedPoint::DegToRad; }
	
	/** Returns radians to degrees conversion factor (57.2957795131) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Rad to Deg"))
	static FFixedPoint RadToDeg() { return FFixedPoint::RadToDeg; }
	
	/** Returns a very small number for tolerance comparisons (~0.0000152587) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Small Number"))
	static FFixedPoint SmallNumber() { return FFixedPoint::SmallNumber; }
	
	/** Returns a small number for tolerance comparisons (~0.01) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Kinda Small Number"))
	static FFixedPoint KindaSmallNumber() { return FFixedPoint::KindaSmallNumber; }
	
	/** Returns a large number near the maximum representable value (32767.0) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Big Number"))
	static FFixedPoint BigNumber() { return FFixedPoint::BigNumber; }
	
	/** Returns epsilon for equality tolerance (~0.000152587) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Epsilon"))
	static FFixedPoint Epsilon() { return FFixedPoint::Epsilon; }
	
	/** Returns the maximum representable value (~32768) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Max Value"))
	static FFixedPoint MaxFixedValue() { return FFixedPoint::MaxValue; }
	
	/** Returns the minimum representable value (~-32768) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Min Value"))
	static FFixedPoint MinFixedValue() { return FFixedPoint::MinValue; }
	
	/** Returns 1/3 (0.33333333333) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point One Third"))
	static FFixedPoint Third() { return FFixedPoint::Third; }
	
	/** Returns 2/3 (0.66666666667) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Two Thirds"))
	static FFixedPoint TwoThirds() { return FFixedPoint::TwoThirds; }
	
	/** Returns 1/4 (0.25) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point One Quarter"))
	static FFixedPoint Quarter() { return FFixedPoint::Quarter; }
	
	/** Returns 3/4 (0.75) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint|Constants", meta = (DisplayName = "Fixed-point Three Quarters"))
	static FFixedPoint ThreeQuarters() { return FFixedPoint::ThreeQuarters; }

	// FFixedPoint - Basic Math
	// ====================================================================================================
	
    /** Makes a FixedPoint rational number from a given integer. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "Make FixedPoint from Int", CompactNodeTitle = "->"))
	static FFixedPoint MakeFixedPointFromInt(int32 IntValue) { return FFixedPoint::FromInt(IntValue); }

	/**
	 * Deterministic 32.32 literal make node. Replaces the default struct make for FFixedPoint.
	 *
	 * IntegerPart goes into the upper 32 bits (whole number side of the decimal).
	 * FractionalPart is the raw lower 32 bits of the 32.32 layout — reinterpreted as unsigned,
	 * so FractionalPart = 0x80000000 (-2147483648 signed) means exactly 0.5.
	 *
	 * This is the deterministic authoring path: no float involvement at any stage, and the
	 * result is bit-identical across every platform. For convenience authoring of arbitrary
	 * decimals, use "Make FixedPoint from Float" (non-deterministic, editor/visual only).
	 */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (
		NativeMakeFunc,
		DisplayName = "Make FixedPoint",
		Keywords = "make construct fixed point literal deterministic"))
	static FFixedPoint MakeFixedPointFromParts(int32 IntegerPortion, int32 FractionPortion)
	{
		const int64 Hi  = static_cast<int64>(IntegerPortion) << 32;
		const int64 Lo  = static_cast<int64>(static_cast<uint32>(FractionPortion));
		return FFixedPoint(Hi | Lo);
	}

	/** Break a FFixedPoint back into its 32.32 integer and fractional halves. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (
		NativeBreakFunc,
		DisplayName = "Break FixedPoint"))
	static void BreakFixedPointToParts(FFixedPoint Value, int32& IntegerPortion, int32& FractionPortion)
	{
		IntegerPortion      = static_cast<int32>(Value.Value >> 32);
		FractionPortion = static_cast<int32>(static_cast<uint32>(Value.Value & 0xFFFFFFFF));
	}
	
	/** Makes a FixedPoint rational number from a given float. Note: conversion may lose precision. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "Make FixedPoint from Float", CompactNodeTitle = "->"))
	static FFixedPoint MakeFixedPointFromFloat(float FloatValue) { return FFixedPoint::FromFloat(FloatValue); }
	
	/** Converts a FixedPoint to an integer (truncates fractional part). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "To Int", CompactNodeTitle = "->"))
	static int32 FixedPointToInt(FFixedPoint Value) { return Value.ToInt(); }
	
	/** Converts a FixedPoint to a float for visualization purposes. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "To Float", CompactNodeTitle = "->"))
	static float FixedPointToFloat(FFixedPoint Value) { return Value.ToFloat(); }
	
	/** Adds two FixedPoint values together. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "Add (Fixed-point)", CompactNodeTitle = "+"))
	static FFixedPoint Add_FixedPointFixedPoint(FFixedPoint A, FFixedPoint B) { return A + B; }
	
	/** Subtracts B from A. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "Subtract (Fixed-point)", CompactNodeTitle = "-"))
	static FFixedPoint Subtract_FixedPointFixedPoint(FFixedPoint A, FFixedPoint B) { return A - B; }
	
	/** Multiplies two FixedPoint values together. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "Multiply", CompactNodeTitle = "*"))
	static FFixedPoint Multiply_FixedPointFixedPoint(FFixedPoint A, FFixedPoint B) { return A * B; }
	
	/** Divides A by B. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "Divide", CompactNodeTitle = "/"))
	static FFixedPoint Divide_FixedPointFixedPoint(FFixedPoint A, FFixedPoint B) { return A / B; }
	
	/** Negates a FixedPoint value (returns -A). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "Negate", CompactNodeTitle = "-"))
	static FFixedPoint Negate_FixedPoint(FFixedPoint A) { return -A; }
	
	/** Returns true if A is exactly equal to B. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "Equal", CompactNodeTitle = "=="))
	static bool EqualEqual_FixedPointFixedPoint(FFixedPoint A, FFixedPoint B) { return A == B; }
	
	/** Returns true if A is not equal to B. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "Not Equal", CompactNodeTitle = "!="))
	static bool NotEqual_FixedPointFixedPoint(FFixedPoint A, FFixedPoint B) { return A != B; }
	
	/** Returns true if A is less than B. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "Less Than", CompactNodeTitle = "<"))
	static bool Less_FixedPointFixedPoint(FFixedPoint A, FFixedPoint B) { return A < B; }
	
	/** Returns true if A is less than or equal to B. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "Less Than or Equal", CompactNodeTitle = "<="))
	static bool LessEqual_FixedPointFixedPoint(FFixedPoint A, FFixedPoint B) { return A <= B; }
	
	/** Returns true if A is greater than B. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "Greater Than", CompactNodeTitle = ">"))
	static bool Greater_FixedPointFixedPoint(FFixedPoint A, FFixedPoint B) { return A > B; }
	
	/** Returns true if A is greater than or equal to B. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "Greater Than or Equal", CompactNodeTitle = ">="))
	static bool GreaterEqual_FixedPointFixedPoint(FFixedPoint A, FFixedPoint B) { return A >= B; }
	
	// MathLib Functions
	// ====================================================================================================
	
	/** Returns the square root of X using deterministic fixed-point arithmetic. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint")
	static FFixedPoint Sqrt(FFixedPoint X) { return SeinMath::Sqrt(X); }
	
	/** Returns the absolute value of X. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint")
	static FFixedPoint Abs(FFixedPoint X) { return SeinMath::Abs(X); }
	
	/** Returns the minimum of A and B. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint")
	static FFixedPoint Min(FFixedPoint A, FFixedPoint B) { return SeinMath::Min(A, B); }
	
	/** Returns the maximum of A and B. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint")
	static FFixedPoint Max(FFixedPoint A, FFixedPoint B) { return SeinMath::Max(A, B); }
	
	/** Clamps Value to be between Min and Max (inclusive). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint")
	static FFixedPoint Clamp(FFixedPoint Value, FFixedPoint Min, FFixedPoint Max) { return SeinMath::Clamp(Value, Min, Max); }
	
	/** Returns the sign of X (-1, 0, or 1). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint")
	static FFixedPoint Sign(FFixedPoint X) { return SeinMath::Sign(X); }
	
	/** Rounds X down to the nearest integer. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint")
	static FFixedPoint Floor(FFixedPoint X) { return SeinMath::Floor(X); }
	
	/** Rounds X up to the nearest integer. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint")
	static FFixedPoint Ceil(FFixedPoint X) { return SeinMath::Ceil(X); }
	
	/** Rounds X to the nearest integer (0.5 rounds up). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint")
	static FFixedPoint Round(FFixedPoint X) { return SeinMath::Round(X); }
	
	/** Returns the remainder of X divided by Y (deterministic modulo). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "Mod", CompactNodeTitle = "%"))
	static FFixedPoint Mod(FFixedPoint X, FFixedPoint Y) { return SeinMath::Mod(X, Y); }
	
	/** Returns the fractional part of X. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint")
	static FFixedPoint Frac(FFixedPoint X) { return SeinMath::Frac(X); }

	/** Linearly interpolates between A and B based on Alpha (unclamped). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|Interpolation")
	static FFixedPoint Lerp_FixedPoint(FFixedPoint A, FFixedPoint B, FFixedPoint Alpha) { return SeinMath::Lerp(A, B, Alpha); }

	/** Finds the interpolation alpha that would result in Value given A and B. Returns (Value - A) / (B - A). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|Interpolation")
	static FFixedPoint InverseLerp(FFixedPoint A, FFixedPoint B, FFixedPoint Value) { return SeinMath::InverseLerp(A, B, Value); }

	/** Smooth Hermite interpolation between A and B (cubic ease in/out). Alpha is clamped to [0,1]. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|Interpolation")
	static FFixedPoint SmoothStep(FFixedPoint A, FFixedPoint B, FFixedPoint Alpha) { return SeinMath::SmoothStep(A, B, Alpha); }

	/** Checks if two FixedPoint values are nearly equal within a given tolerance. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint")
	static bool IsNearlyEqual_FixedPoint(FFixedPoint A, FFixedPoint B, FFixedPoint Tolerance) { return A.IsNearlyEqual(B, Tolerance); }

	/** Raises Base to the power of Exponent. Example: Pow(2, 3) = 8. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "Power"))
	static FFixedPoint Pow(FFixedPoint Base, int32 Exponent) { return SeinMath::Pow(Base, Exponent); }

	/** Returns e raised to the power of X (natural exponential function). Valid range approximately -10 to +10. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "Exp (e^X)"))
	static FFixedPoint Exp(FFixedPoint X) { return SeinMath::Exp(X); }

	/** Returns the natural logarithm of X (base e). X must be greater than 0. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "Natural Log (ln)"))
	static FFixedPoint Log(FFixedPoint X) { return SeinMath::Log(X); }

	/** Returns the logarithm base 2 of X. X must be greater than 0. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "Log Base 2"))
	static FFixedPoint Log2(FFixedPoint X) { return SeinMath::Log2(X); }

	/** Returns the logarithm base 10 of X. X must be greater than 0. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedPoint", meta = (DisplayName = "Log Base 10"))
	static FFixedPoint Log10(FFixedPoint X) { return SeinMath::Log10(X); }
	
	/** Returns the sine of Radians using a deterministic lookup table. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|Trigonometry")
	static FFixedPoint Sin(FFixedPoint Radians) { return SeinMath::Sin(Radians); }
	
	/** Returns the cosine of Radians using a deterministic lookup table. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|Trigonometry")
	static FFixedPoint Cos(FFixedPoint Radians) { return SeinMath::Cos(Radians); }
	
	/** Returns the tangent of Radians. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|Trigonometry")
	static FFixedPoint Tan(FFixedPoint Radians) { return SeinMath::Tan(Radians); }
	
	/** Returns the arcsine of X in radians. X should be in the range [-1, 1]. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|Trigonometry")
	static FFixedPoint Asin(FFixedPoint X) { return SeinMath::Asin(X); }
	
	/** Returns the arccosine of X in radians. X should be in the range [-1, 1]. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|Trigonometry")
	static FFixedPoint Acos(FFixedPoint X) { return SeinMath::Acos(X); }
	
	/** Returns the arctangent of X in radians. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|Trigonometry")
	static FFixedPoint Atan(FFixedPoint X) { return SeinMath::Atan(X); }
	
	/** Returns the arctangent of Y/X in radians, using the signs of both to determine the quadrant. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|Trigonometry")
	static FFixedPoint Atan2(FFixedPoint Y, FFixedPoint X) { return SeinMath::Atan2(Y, X); }
	
	// FFixedVector2D
	// ====================================================================================================
	
	/** Creates a FixedVector2D from X and Y components. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector2D", meta = (DisplayName = "Make FixedVector2D"))
	static FFixedVector2D MakeFixedVector2D(FFixedPoint X, FFixedPoint Y) { return FFixedVector2D(X, Y); }
	
	/** Converts a float Vector2D to a FixedVector2D. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector2D", meta = (DisplayName = "From Vector2D", CompactNodeTitle = "->"))
	static FFixedVector2D MakeFixedVector2DFromVector2D(FVector2D V) { return FFixedVector2D::FromFloat(V.X, V.Y); }
	
	/** Converts a FixedVector2D to a float Vector2D for visualization. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector2D", meta = (DisplayName = "To Vector2D", CompactNodeTitle = "->"))
	static FVector2D FixedVector2DToVector2D(const FFixedVector2D& V) { return V.ToFloat(); }
	
	/** Adds two FixedVector2D vectors together. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector2D", meta = (DisplayName = "Add", CompactNodeTitle = "+"))
	static FFixedVector2D Add_FixedVector2D(const FFixedVector2D& A, const FFixedVector2D& B) { return A + B; }
	
	/** Subtracts B from A. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector2D", meta = (DisplayName = "Subtract", CompactNodeTitle = "-"))
	static FFixedVector2D Subtract_FixedVector2D(const FFixedVector2D& A, const FFixedVector2D& B) { return A - B; }
	
	/** Multiplies a FixedVector2D by a scalar. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector2D", meta = (DisplayName = "Multiply", CompactNodeTitle = "*"))
	static FFixedVector2D Multiply_FixedVector2DFixedPoint(const FFixedVector2D& V, FFixedPoint Scale) { return V * Scale; }
	
	/** Divides a FixedVector2D by a scalar. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector2D", meta = (DisplayName = "Divide", CompactNodeTitle = "/"))
	static FFixedVector2D Divide_FixedVector2DFixedPoint(const FFixedVector2D& V, FFixedPoint Divisor) { return V / Divisor; }
	
	/** Returns the dot product of two FixedVector2D vectors. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector2D", meta = (DisplayName = "Dot Product", CompactNodeTitle = "•"))
	static FFixedPoint DotProduct2D(const FFixedVector2D& A, const FFixedVector2D& B) { return A | B; }
	
	/** Returns the cross product (2D) of two FixedVector2D vectors. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector2D")
	static FFixedPoint CrossProduct2D(const FFixedVector2D& A, const FFixedVector2D& B) { return FFixedVector2D::CrossProduct(A, B); }
	
	/** Returns the length (magnitude) of a FixedVector2D. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector2D")
	static FFixedPoint Size2D(const FFixedVector2D& V) { return V.Size(); }
	
	/** Returns the squared length of a FixedVector2D (faster than Size). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector2D")
	static FFixedPoint SizeSquared2D(const FFixedVector2D& V) { return V.SizeSquared(); }
	
	/** Returns a normalized copy of the vector if safe to do so, otherwise returns a zero vector. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector2D")
	static FFixedVector2D GetSafeNormal2D(const FFixedVector2D& V, FFixedPoint Tolerance) { return FFixedVector2D::GetSafeNormal(V, Tolerance); }
	
	/** Returns true if the vector is exactly zero. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector2D")
	static bool IsZero2D(const FFixedVector2D& V) { return V.IsZero(); }
	
	/** Returns true if the vector is nearly zero within the specified tolerance. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector2D")
	static bool IsNearlyZero2D(const FFixedVector2D& V, FFixedPoint Tolerance) { return V.IsNearlyZero(Tolerance); }
	
	/** Returns the distance between two 2D points. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector2D")
	static FFixedPoint Distance2D(const FFixedVector2D& A, const FFixedVector2D& B) { return FFixedVector2D::Distance(A, B); }
	
	/** Returns the squared distance between two 2D points (faster than Distance). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector2D")
	static FFixedPoint DistanceSquared2D(const FFixedVector2D& A, const FFixedVector2D& B) { return FFixedVector2D::DistSquared(A, B); }
	
	/** Linearly interpolates between A and B based on Alpha. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector2D")
	static FFixedVector2D Lerp2D(const FFixedVector2D& A, const FFixedVector2D& B, FFixedPoint Alpha) { return FFixedVector2D::Lerp(A, B, Alpha); }
	
	// FFixedVector
	// ====================================================================================================
	
	/** Returns a zero vector (0, 0, 0) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector|Constants", meta = (DisplayName = "Zero Vector"))
	static FFixedVector ZeroVector() { return FFixedVector::ZeroVector; }
	
	/** Returns an identity vector (1, 1, 1) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector|Constants", meta = (DisplayName = "One Vector"))
	static FFixedVector IdentityVector() { return FFixedVector::Identity; }
	
	/** Returns the up vector (0, 0, 1) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector|Constants", meta = (DisplayName = "Up Vector"))
	static FFixedVector UpVector() { return FFixedVector::UpVector; }
	
	/** Returns the down vector (0, 0, -1) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector|Constants", meta = (DisplayName = "Down Vector"))
	static FFixedVector DownVector() { return FFixedVector::DownVector; }
	
	/** Returns the forward vector (1, 0, 0) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector|Constants", meta = (DisplayName = "Forward Vector"))
	static FFixedVector ForwardVector() { return FFixedVector::ForwardVector; }
	
	/** Returns the backward vector (-1, 0, 0) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector|Constants", meta = (DisplayName = "Backward Vector"))
	static FFixedVector BackVector() { return FFixedVector::BackwardVector; }
	
	/** Returns the right vector (0, 1, 0) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector|Constants", meta = (DisplayName = "Right Vector"))
	static FFixedVector RightVector() { return FFixedVector::RightVector; }
	
	/** Returns the left vector (0, -1, 0) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector|Constants", meta = (DisplayName = "Left Vector"))
	static FFixedVector LeftVector() { return FFixedVector::LeftVector; }
	
	/** Creates a FixedVector from X, Y, and Z components. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector", meta = (DisplayName = "Make FixedVector"))
	static FFixedVector MakeFixedVector(FFixedPoint X, FFixedPoint Y, FFixedPoint Z) { return FFixedVector(X, Y, Z); }
	
	/** Converts a float Vector to a FixedVector. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector", meta = (DisplayName = "From Vector", CompactNodeTitle = "->"))
	static FFixedVector MakeFixedVectorFromVector(FVector V) { return FFixedVector::FromVector(V); }
	
	/** Converts a FixedVector to a float Vector for visualization. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector", meta = (DisplayName = "To Vector", CompactNodeTitle = "->"))
	static FVector FixedVectorToVector(const FFixedVector& V) { return V.ToVector(); }
	
	/** Adds two FixedVector vectors together. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector", meta = (DisplayName = "Add", CompactNodeTitle = "+"))
	static FFixedVector Add_FixedVector(const FFixedVector& A, const FFixedVector& B) { return A + B; }
	
	/** Subtracts B from A. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector", meta = (DisplayName = "Subtract", CompactNodeTitle = "-"))
	static FFixedVector Subtract_FixedVector(const FFixedVector& A, const FFixedVector& B) { return A - B; }
	
	/** Multiplies a FixedVector by a scalar. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector", meta = (DisplayName = "Multiply", CompactNodeTitle = "*"))
	static FFixedVector Multiply_FixedVectorFixedPoint(const FFixedVector& V, FFixedPoint Scale) { return V * Scale; }
	
	/** Divides a FixedVector by a scalar. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector", meta = (DisplayName = "Divide", CompactNodeTitle = "/"))
	static FFixedVector Divide_FixedVectorFixedPoint(const FFixedVector& V, FFixedPoint Divisor) { return V / Divisor; }
	
	/** Returns the dot product of two FixedVector vectors. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector", meta = (DisplayName = "Dot Product", CompactNodeTitle = "•"))
	static FFixedPoint DotProduct(const FFixedVector& A, const FFixedVector& B) { return A | B; }
	
	/** Returns the cross product of two FixedVector vectors. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector", meta = (DisplayName = "Cross Product", CompactNodeTitle = "×"))
	static FFixedVector CrossProduct(const FFixedVector& A, const FFixedVector& B) { return A ^ B; }
	
	/** Returns the length (magnitude) of a FixedVector. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector")
	static FFixedPoint VectorSize(const FFixedVector& V) { return V.Size(); }
	
	/** Returns the squared length of a FixedVector (faster than VectorSize). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector")
	static FFixedPoint VectorSizeSquared(const FFixedVector& V) { return V.SizeSquared(); }
	
	/** Returns a normalized copy of the vector if safe to do so, otherwise returns a zero vector. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector")
	static FFixedVector GetSafeNormal(const FFixedVector& V, FFixedPoint Tolerance) { return FFixedVector::GetSafeNormal(V, Tolerance); }

	/** Returns a normalized copy of the vector. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector")
	static FFixedVector GetNormalized(const FFixedVector& V, FFixedPoint Tolerance) { return V.GetNormalized(Tolerance); }

	/** Checks if the vector is normalized (has length of 1) within tolerance. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector")
	static bool IsNormalized(const FFixedVector& V, FFixedPoint Tolerance) { return V.IsNormalized(Tolerance); }

	/** Returns vector clamped to the specified size range. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector")
	static FFixedVector GetClampedToSize(const FFixedVector& V, FFixedPoint Min, FFixedPoint Max) { return V.GetClampedToSize(Min, Max); }

	/** Returns vector clamped to the specified maximum size. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector")
	static FFixedVector GetClampedToMaxSize(const FFixedVector& V, FFixedPoint MaxSize) { return V.GetClampedToMaxSize(MaxSize); }

	/** Projects vector V onto Target vector. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector", meta = (DisplayName = "Project Vector Onto"))
	static FFixedVector ProjectVectorOnTo(const FFixedVector& V, const FFixedVector& Target) { return FFixedVector::ProjectOnTo(V, Target); }

	/** Projects vector V onto Normal (which must be normalized). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector", meta = (DisplayName = "Project Vector Onto Normal"))
	static FFixedVector ProjectVectorOnToNormal(const FFixedVector& V, const FFixedVector& Normal) { return FFixedVector::ProjectOnToNormal(V, Normal); }
	
	/** Returns true if the vector is exactly zero. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector")
	static bool IsZeroVector(const FFixedVector& V) { return V.IsZero(); }
	
	/** Returns true if the vector is nearly zero within the specified tolerance. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector")
	static bool IsNearlyZeroVector(const FFixedVector& V, FFixedPoint Tolerance) { return V.IsNearlyZero(Tolerance); }
	
	/** Returns the distance between two points. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector")
	static FFixedPoint VectorDistance(const FFixedVector& A, const FFixedVector& B) { return FFixedVector::Distance(A, B); }
	
	/** Returns the squared distance between two points (faster than VectorDistance). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector")
	static FFixedPoint VectorDistanceSquared(const FFixedVector& A, const FFixedVector& B) { return FFixedVector::DistSquared(A, B); }
	
	/** Linearly interpolates between A and B based on Alpha. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector")
	static FFixedVector VectorLerp(const FFixedVector& A, const FFixedVector& B, FFixedPoint Alpha) { return FFixedVector::Lerp(A, B, Alpha); }
	
	/** Projects vector V onto the target vector. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector")
	static FFixedVector ProjectOnTo(const FFixedVector& V, const FFixedVector& Target) { return FFixedVector::ProjectOnTo(V, Target); }
	
	/** Clamps the size of a vector to be within the specified range. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector")
	static FFixedVector ClampVectorSize(const FFixedVector& V, FFixedPoint Min, FFixedPoint Max) { return FFixedVector::ClampSize(V, Min, Max); }
	
	/** Returns a vector with the absolute value of each component. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedVector")
	static FFixedVector VectorAbs(const FFixedVector& V) { return FFixedVector::Abs(V); }
	
	// FFixedRotator
	// ====================================================================================================
	
	/** Returns a zero rotator (0, 0, 0) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedRotator|Constants", meta = (DisplayName = "Zero Rotator"))
	static FFixedRotator ZeroRotator() { return FFixedRotator::ZeroRotator; }
	
	/** Creates a FixedRotator from Pitch, Yaw, and Roll components. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedRotator", meta = (DisplayName = "Make FixedRotator"))
	static FFixedRotator MakeFixedRotator(FFixedPoint Pitch, FFixedPoint Yaw, FFixedPoint Roll) { return FFixedRotator(Pitch, Yaw, Roll); }
	
	/** Converts a float Rotator to a FixedRotator. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedRotator", meta = (DisplayName = "From Rotator", CompactNodeTitle = "->"))
	static FFixedRotator MakeFixedRotatorFromRotator(FRotator R) { return FFixedRotator::FromRotator(R); }
	
	/** Converts a FixedRotator to a float Rotator for visualization. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedRotator", meta = (DisplayName = "To Rotator", CompactNodeTitle = "->"))
	static FRotator FixedRotatorToRotator(const FFixedRotator& R) { return R.ToRotator(); }
	
	/** Adds two FixedRotator rotations together. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedRotator", meta = (DisplayName = "Add", CompactNodeTitle = "+"))
	static FFixedRotator Add_FixedRotator(const FFixedRotator& A, const FFixedRotator& B) { return A + B; }
	
	/** Subtracts B from A. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedRotator", meta = (DisplayName = "Subtract", CompactNodeTitle = "-"))
	static FFixedRotator Subtract_FixedRotator(const FFixedRotator& A, const FFixedRotator& B) { return A - B; }
	
	/** Returns true if the rotator is exactly zero. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedRotator")
	static bool IsZeroRotator(const FFixedRotator& R) { return R.IsZero(); }
	
	/** Returns a normalized copy of the rotator (angles clamped to valid ranges). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedRotator")
	static FFixedRotator GetNormalizedRotator(const FFixedRotator& R) { return R.GetNormalized(); }
	
	/** Returns the forward direction vector for this rotator. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedRotator")
	static FFixedVector GetForwardVector(const FFixedRotator& R) { return R.GetForwardVector(); }
	
	/** Returns the right direction vector for this rotator. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedRotator")
	static FFixedVector GetRightVector(const FFixedRotator& R) { return R.GetRightVector(); }
	
	/** Returns the up direction vector for this rotator. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedRotator")
	static FFixedVector GetUpVector(const FFixedRotator& R) { return R.GetUpVector(); }
	
	/** Rotates a vector by this rotator. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedRotator")
	static FFixedVector RotateVector(const FFixedRotator& R, const FFixedVector& V) { return R.RotateVector(V); }
	
	/** Unrotates a vector (applies inverse rotation). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedRotator")
	static FFixedVector UnrotateVector(const FFixedRotator& R, const FFixedVector& V) { return R.UnrotateVector(V); }
	
	/** Converts a FixedRotator to a FixedQuaternion. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedRotator")
	static FFixedQuaternion RotatorToQuaternion(const FFixedRotator& R) { return R.Quaternion(); }
	
	/** Converts a FixedQuaternion to a FixedRotator. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedRotator")
	static FFixedRotator QuaternionToRotator(const FFixedQuaternion& Q) { return Q.Rotator(); }
	
	// FFixedQuaternion
	// ====================================================================================================
	
	/** Creates a FixedQuaternion from X, Y, Z, and W components. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedQuaternion", meta = (DisplayName = "Make FixedQuaternion"))
	static FFixedQuaternion MakeFixedQuaternion(FFixedPoint X, FFixedPoint Y, FFixedPoint Z, FFixedPoint W) { return FFixedQuaternion(X, Y, Z, W); }
	
	/** Converts a float Quat to a FixedQuaternion. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedQuaternion", meta = (DisplayName = "From Quat", CompactNodeTitle = "->"))
	static FFixedQuaternion MakeFixedQuaternionFromQuat(FQuat Q) { return FFixedQuaternion::FromQuat(Q); }
	
	/** Converts a FixedQuaternion to a float Quat for visualization. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedQuaternion", meta = (DisplayName = "To Quat", CompactNodeTitle = "->"))
	static FQuat FixedQuaternionToQuat(const FFixedQuaternion& Q) { return Q.ToQuat(); }
	
	/** Returns an identity quaternion (no rotation). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedQuaternion")
	static FFixedQuaternion QuaternionIdentity() { return FFixedQuaternion::Identity; }
	
	/** Creates a quaternion from Euler angles (Pitch, Yaw, Roll). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedQuaternion")
	static FFixedQuaternion MakeQuaternionFromEuler(const FFixedVector& Euler) { return FFixedQuaternion::MakeFromEulers(Euler); }
	
	/** Converts a quaternion to Euler angles (Pitch, Yaw, Roll). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedQuaternion")
	static FFixedVector QuaternionToEuler(const FFixedQuaternion& Q) { return Q.Eulers(); }

	/** Creates a quaternion from an axis and angle (axis should be normalized, angle in radians). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedQuaternion")
	static FFixedQuaternion MakeQuaternionFromAxisAndAngle(const FFixedVector& Axis, FFixedPoint AngleRad) { return FFixedQuaternion::FromAxisAndAngle(Axis, AngleRad); }
	
	/** Multiplies two quaternions together (combines rotations). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedQuaternion", meta = (DisplayName = "Multiply", CompactNodeTitle = "*"))
	static FFixedQuaternion Multiply_QuaternionQuaternion(const FFixedQuaternion& A, const FFixedQuaternion& B) { return A * B; }
	
	/** Rotates a vector by this quaternion. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedQuaternion")
	static FFixedVector QuaternionRotateVector(const FFixedQuaternion& Q, const FFixedVector& V) { return Q.RotateVector(V); }
	
	/** Returns the inverse of a quaternion (opposite rotation). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedQuaternion")
	static FFixedQuaternion InverseQuaternion(const FFixedQuaternion& Q) { return Q.Inverse(); }
	
	/** Returns a normalized copy of the quaternion. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedQuaternion")
	static FFixedQuaternion NormalizeQuaternion(const FFixedQuaternion& Q) { return Q.GetNormalized(); }
	
	/** Spherically interpolates between two quaternions. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedQuaternion")
	static FFixedQuaternion SlerpQuaternion(const FFixedQuaternion& A, const FFixedQuaternion& B, FFixedPoint Alpha) { return FFixedQuaternion::Slerp(A, B, Alpha); }

	/** Returns the forward direction vector (X axis) for this quaternion. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedQuaternion")
	static FFixedVector GetQuaternionForwardVector(const FFixedQuaternion& Q) { return Q.GetForwardVector(); }

	/** Returns the right direction vector (Y axis) for this quaternion. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedQuaternion")
	static FFixedVector GetQuaternionRightVector(const FFixedQuaternion& Q) { return Q.GetRightVector(); }

	/** Returns the up direction vector (Z axis) for this quaternion. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedQuaternion")
	static FFixedVector GetQuaternionUpVector(const FFixedQuaternion& Q) { return Q.GetUpVector(); }
	
	/** Returns the X axis direction vector for this quaternion. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedQuaternion")
	static FFixedVector GetQuaternionAxisX(const FFixedQuaternion& Q) { return Q.GetAxisX(); }
	
	/** Returns the Y axis direction vector for this quaternion. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedQuaternion")
	static FFixedVector GetQuaternionAxisY(const FFixedQuaternion& Q) { return Q.GetAxisY(); }
	
	/** Returns the Z axis direction vector for this quaternion. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedQuaternion")
	static FFixedVector GetQuaternionAxisZ(const FFixedQuaternion& Q) { return Q.GetAxisZ(); }

	// FFixedTransform
	// ====================================================================================================
	
	/** Creates a FixedTransform from location, rotation, and scale. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedTransform", meta = (DisplayName = "Make Fixed-point Transform"))
	static FFixedTransform MakeFixedTransform(FFixedVector Location, FFixedQuaternion Rotation, FFixedVector Scale) { return FFixedTransform(Location, Rotation, Scale); }
	
	/** Creates a FixedTransform from location and rotation (uniform scale of 1). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedTransform", meta = (DisplayName = "Make Fixed-point Transform (T+R)"))
	static FFixedTransform MakeFixedTransformFromTR(FFixedVector Location, FFixedQuaternion Rotation) { return FFixedTransform(Location, Rotation); }
	
	/** Converts a float Transform to a FixedTransform. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedTransform", meta = (DisplayName = "From Transform", CompactNodeTitle = "->"))
	static FFixedTransform MakeFixedTransformFromTransform(FTransform T) { return FFixedTransform::FromTransform(T); }
	
	/** Converts a FixedTransform to a float Transform for visualization. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedTransform", meta = (DisplayName = "To Transform", CompactNodeTitle = "->"))
	static FTransform FixedTransformToTransform(const FFixedTransform& T) { return T.ToTransform(); }
	
	/** Returns an identity fixed-point transform (no location, no rotation, scale of 1). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedTransform")
	static FFixedTransform IdentityTransform() { return FFixedTransform::Identity(); }
	
	/** Gets the location component of a fixed-point transform. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedTransform")
	static FFixedVector GetLocation(const FFixedTransform& T) { return T.GetLocation(); }
	
	/** Gets the rotation component of a fixed-point transform as a quaternion. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedTransform")
	static FFixedQuaternion GetQuaternionRotation(const FFixedTransform& T) { return T.GetQuaternionRotation(); }
	
	/** Gets the rotation component of a fixed-point transform. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedTransform")
	static FFixedRotator GetRotation(const FFixedTransform& T) { return T.GetRotation(); }
	
	/** Gets the scale component of a fixed-point transform. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedTransform")
	static FFixedVector GetScale(const FFixedTransform& T) { return T.GetScale(); }
	
	/** Sets the location component of a fixed-point transform. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedTransform")
	static FFixedTransform SetLocation(const FFixedTransform& T, FFixedVector NewLocation) 
	{ 
		FFixedTransform Result = T;
		Result.SetLocation(NewLocation);
		return Result;
	}
	
	/** Sets the rotation component of a fixed-point transform. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedTransform")
	static FFixedTransform SetRotation(const FFixedTransform& T, FFixedQuaternion NewRotation) 
	{ 
		FFixedTransform Result = T;
		Result.SetRotation(NewRotation);
		return Result;
	}
	
	/** Sets the scale component of a fixed-point transform. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedTransform")
	static FFixedTransform SetScale(const FFixedTransform& T, FFixedVector NewScale) 
	{ 
		FFixedTransform Result = T;
		Result.SetScale(NewScale);
		return Result;
	}
	
	/** Transforms a position by the given fixed-point transform. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedTransform")
	static FFixedVector TransformPosition(const FFixedTransform& T, const FFixedVector& Position) { return T.TransformPosition(Position); }
	
	/** Transforms a direction vector by the given fixed-point transform (no location). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedTransform")
	static FFixedVector TransformDirection(const FFixedTransform& T, const FFixedVector& Direction) { return T.TransformVector(Direction); }
	
	/** Inverse transforms a fixed-point position vector. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedTransform")
	static FFixedVector InverseTransformPosition(const FFixedTransform& T, const FFixedVector& Position) { return T.InverseTransformPosition(Position); }
	
	/** Inverse transforms a fixed-point direction vector. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedTransform")
	static FFixedVector InverseTransformDirection(const FFixedTransform& T, const FFixedVector& Direction) { return T.InverseTransformVector(Direction); }
	
	/** Returns the inverse of a fixed-point transform. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedTransform")
	static FFixedTransform InverseTransform(const FFixedTransform& T) { return T.Inverse(); }
	
	/** Multiplies two fixed-point transforms together (applies A first, then B). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedTransform", meta = (DisplayName = "Multiply", CompactNodeTitle = "*"))
	static FFixedTransform Multiply_TransformTransform(const FFixedTransform& A, const FFixedTransform& B) { return B * A; }
	
	/** Linearly interpolates between two fixed-point transforms. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|FixedTransform")
	static FFixedTransform LerpTransform(const FFixedTransform& A, const FFixedTransform& B, FFixedPoint Alpha) { return FFixedTransform::Lerp(A, B, Alpha); }

	// FFixedRandom (Deterministic Random Number Generator)
	// ====================================================================================================
	
	/** Creates a new RNG with a seed value. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Make Random"))
	static FFixedRandom MakeRandom(int64 Seed) { return FFixedRandom(static_cast<uint64>(Seed)); }
	
	/** Creates a new RNG with default seed. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Make Default Random"))
	static FFixedRandom MakeDefaultRandom() { return FFixedRandom(); }
	
	/** Sets the seed for the random number generator. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Set Random Seed"))
	static void SetRandomSeed(UPARAM(ref) FFixedRandom& RNG, int64 Seed) { RNG.SetSeed(static_cast<uint64>(Seed)); }
	
	/** Gets the current RNG state for serialization. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Get Random State"))
	static void GetRandomState(UPARAM(ref) FFixedRandom& RNG, int64& OutState0, int64& OutState1)
	{
		uint64 S0, S1;
		RNG.GetState(S0, S1);
		OutState0 = static_cast<int64>(S0);
		OutState1 = static_cast<int64>(S1);
	}
	
	/** Sets the RNG state for deserialization. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Set Random State (Deterministic)"))
	static void SetRandomState(UPARAM(ref) FFixedRandom& RNG, int64 InState0, int64 InState1) { RNG.SetState(static_cast<uint64>(InState0), static_cast<uint64>(InState1)); }
	
	/** Generates a random int32 value. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Random Int"))
	static void RandomInt(UPARAM(ref) FFixedRandom& RNG, int32& Result) { Result = RNG.Int(); }
	
	/** Generates a random int64 value. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Random Int64"))
	static void RandomInt64(UPARAM(ref) FFixedRandom& RNG, int64& Result) { Result = RNG.Int64(); }
	
	/** Generates a random fixed-point value in range [0, 1]. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Random Fixed Point"))
	static void RandomFixedPoint(UPARAM(ref) FFixedRandom& RNG, FFixedPoint& Result) { Result = RNG.FixedPoint(); }
	
	/** Generates a random boolean (50/50 chance). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Random Bool"))
	static void RandomBool(UPARAM(ref) FFixedRandom& RNG, bool& Result) { Result = RNG.Bool(); }
	
	/** Generates a random boolean with given probability (0 to 1). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Random Bool With Probability"))
	static void RandomBoolWithProbability(UPARAM(ref) FFixedRandom& RNG, FFixedPoint Probability, bool& Result) { Result = RNG.Bool(Probability); }
	
	/** Generates a random fixed-point value in range [Min, Max]. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Random Range"))
	static void RandomRange(UPARAM(ref) FFixedRandom& RNG, FFixedPoint Min, FFixedPoint Max, FFixedPoint& Result) { Result = RNG.Range(Min, Max); }
	
	/** Generates a random int32 in range [Min, Max] (inclusive). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Random Int Range"))
	static void RandomIntRange(UPARAM(ref) FFixedRandom& RNG, int32 Min, int32 Max, int32& Result) { Result = RNG.IntRange(Min, Max); }
	
	/** Generates a random 2D point inside a circle. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Random Point In Circle"))
	static void RandomPointInCircle(UPARAM(ref) FFixedRandom& RNG, FFixedVector2D Centre, FFixedPoint Radius, FFixedVector2D& Result) { Result = RNG.PointInCircle(Centre, Radius); }
	
	/** Generates a random 3D point inside a sphere. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Random Point In Sphere"))
	static void RandomPointInSphere(UPARAM(ref) FFixedRandom& RNG, FFixedVector Centre, FFixedPoint Radius, FFixedVector& Result) { Result = RNG.PointInSphere(Centre, Radius); }
	
	/** Generates a random 2D point inside a rectangle. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Random Point In Rect"))
	static void RandomPointInRect(UPARAM(ref) FFixedRandom& RNG, FFixedVector2D Centre, FFixedVector2D HalfExtents, FFixedVector2D& Result) { Result = RNG.PointInRect(Centre, HalfExtents); }
	
	/** Generates a random 3D point inside a box. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Random Point In Box"))
	static void RandomPointInBox(UPARAM(ref) FFixedRandom& RNG, FFixedVector Centre, FFixedVector HalfExtents, FFixedVector& Result) { Result = RNG.PointInBox(Centre, HalfExtents); }
	
	/** Generates a random 2D point inside unit circle. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Random Inside Unit Circle"))
	static void RandomInsideUnitCircle(UPARAM(ref) FFixedRandom& RNG, FFixedVector2D& Result) { Result = RNG.InsideUnitCircle(); }
	
	/** Generates a random unit 2D vector. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Random On Unit Circle"))
	static void RandomOnUnitCircle(UPARAM(ref) FFixedRandom& RNG, FFixedVector2D& Result) { Result = RNG.OnUnitCircle(); }
	
	/** Generates a random 3D point inside unit sphere. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Random Inside Unit Sphere"))
	static void RandomInsideUnitSphere(UPARAM(ref) FFixedRandom& RNG, FFixedVector& Result) { Result = RNG.InsideUnitSphere(); }
	
	/** Generates a random unit 3D vector. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Random On Unit Sphere"))
	static void RandomOnUnitSphere(UPARAM(ref) FFixedRandom& RNG, FFixedVector& Result) { Result = RNG.OnUnitSphere(); }
	
	/** Generates a random yaw angle in radians. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Random Yaw"))
	static void RandomYaw(UPARAM(ref) FFixedRandom& RNG, FFixedPoint& Result) { Result = RNG.Yaw(); }
	
	/** Generates a random pitch angle in radians. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Random Pitch"))
	static void RandomPitch(UPARAM(ref) FFixedRandom& RNG, FFixedPoint& Result) { Result = RNG.Pitch(); }
	
	/** Generates a random roll angle in radians. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Random Roll"))
	static void RandomRoll(UPARAM(ref) FFixedRandom& RNG, FFixedPoint& Result) { Result = RNG.Roll(); }
	
	/** Generates a random rotator with random pitch, yaw, and roll. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Math|Random", meta = (DisplayName = "Random Rotator"))
	static void RandomRotator(UPARAM(ref) FFixedRandom& RNG, FFixedRotator& Result) { Result = RNG.RandomRotator(); }
};

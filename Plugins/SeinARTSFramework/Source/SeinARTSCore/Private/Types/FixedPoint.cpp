/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		FixedPoint.cpp
 * @date:		1/17/2026
 * @author:		RJ Macklem
 * @brief:		Definitions for FFixedPoint constants (32.32 format).
 * @disclaimer: This code was generated in part by an AI language model.
 */

#include "Types/FixedPoint.h"

// Fundamental constants (value * 2^32)
const FFixedPoint FFixedPoint::Zero(0LL);
const FFixedPoint FFixedPoint::One(4294967296LL);                       // 1.0 * 2^32
const FFixedPoint FFixedPoint::Half(2147483648LL);                      // 0.5 * 2^32
const FFixedPoint FFixedPoint::Two(8589934592LL);                       // 2.0 * 2^32

// Pi and related constants
const FFixedPoint FFixedPoint::Pi(13493037704LL);                       // 3.14159265359 * 2^32
const FFixedPoint FFixedPoint::TwoPi(26986075409LL);                    // 6.28318530718 * 2^32
const FFixedPoint FFixedPoint::HalfPi(6746518852LL);                    // 1.57079632679 * 2^32
const FFixedPoint FFixedPoint::QuarterPi(3373259426LL);                 // 0.78539816339 * 2^32
const FFixedPoint FFixedPoint::InvPi(1367130551LL);                     // 0.31830988618 * 2^32
const FFixedPoint FFixedPoint::InvTwoPi(683565275LL);                   // 0.15915494309 * 2^32

// Euler's number and related
const FFixedPoint FFixedPoint::E(11674931555LL);                        // 2.71828182846 * 2^32
const FFixedPoint FFixedPoint::Log2E(6196328879LL);                     // 1.44269504089 * 2^32
const FFixedPoint FFixedPoint::Log10E(1865674548LL);                    // 0.43429448190 * 2^32
const FFixedPoint FFixedPoint::Ln2(2977044471LL);                       // 0.69314718056 * 2^32
const FFixedPoint FFixedPoint::Ln10(9887560890LL);                      // 2.30258509299 * 2^32

// Square roots
const FFixedPoint FFixedPoint::Sqrt2(6074000999LL);                     // 1.41421356237 * 2^32
const FFixedPoint FFixedPoint::Sqrt3(7435165934LL);                     // 1.73205080757 * 2^32
const FFixedPoint FFixedPoint::SqrtHalf(3037000499LL);                  // 0.70710678118 * 2^32
const FFixedPoint FFixedPoint::InvSqrt2(3037000499LL);                  // 0.70710678118 * 2^32
const FFixedPoint FFixedPoint::InvSqrt3(2479700166LL);                  // 0.57735026919 * 2^32

// Golden ratio
const FFixedPoint FFixedPoint::Phi(6949203465LL);                       // 1.61803398875 * 2^32
const FFixedPoint FFixedPoint::InvPhi(2654267169LL);                    // 0.61803398875 * 2^32

// Angle conversions
const FFixedPoint FFixedPoint::DegToRad(74961320LL);                    // 0.01745329252 * 2^32
const FFixedPoint FFixedPoint::RadToDeg(246083499208LL);                // 57.2957795131 * 2^32

// Tolerance values
const FFixedPoint FFixedPoint::SmallNumber(1LL);                        // Smallest representable non-zero (~2.3e-10)
const FFixedPoint FFixedPoint::KindaSmallNumber(42949672LL);            // 0.01 * 2^32
const FFixedPoint FFixedPoint::BigNumber(9223372036854775807LL);        // Near max positive (2^63-1)
const FFixedPoint FFixedPoint::Epsilon(655LL);                          // Small epsilon (~1.5e-7)

// Numeric limits
const FFixedPoint FFixedPoint::MaxValue(9223372036854775807LL);         // Maximum representable value (2^63-1) ~2.1 billion
const FFixedPoint FFixedPoint::MinValue(-9223372036854775807LL - 1LL);  // Minimum representable value (-2^63) ~-2.1 billion

// Common fractions
const FFixedPoint FFixedPoint::Third(1431655765LL);                     // 0.33333333333 * 2^32
const FFixedPoint FFixedPoint::TwoThirds(2863311531LL);                 // 0.66666666667 * 2^32
const FFixedPoint FFixedPoint::Quarter(1073741824LL);                   // 0.25 * 2^32
const FFixedPoint FFixedPoint::ThreeQuarters(3221225472LL);             // 0.75 * 2^32

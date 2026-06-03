/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		Vector.cpp
 * @date:		1/17/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of FFixedVector static constants.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#include "Types/Vector.h"

// FFixedVector static constants
const FFixedVector FFixedVector::ZeroVector     = FFixedVector(FFixedPoint::Zero, FFixedPoint::Zero, FFixedPoint::Zero);
const FFixedVector FFixedVector::Identity       = FFixedVector(FFixedPoint::One, FFixedPoint::One, FFixedPoint::One);
const FFixedVector FFixedVector::UpVector       = FFixedVector(FFixedPoint::Zero, FFixedPoint::Zero, FFixedPoint::One);
const FFixedVector FFixedVector::DownVector     = FFixedVector(FFixedPoint::Zero, FFixedPoint::Zero, -FFixedPoint::One);
const FFixedVector FFixedVector::ForwardVector  = FFixedVector(FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::Zero);
const FFixedVector FFixedVector::BackwardVector = FFixedVector(-FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::Zero);
const FFixedVector FFixedVector::RightVector    = FFixedVector(FFixedPoint::Zero, FFixedPoint::One, FFixedPoint::Zero);
const FFixedVector FFixedVector::LeftVector     = FFixedVector(FFixedPoint::Zero, -FFixedPoint::One, FFixedPoint::Zero);
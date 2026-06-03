/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		Quat.cpp
 * @date:		1/17/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of FFixedQuaternion static constants.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#include "Types/Quat.h"
#include "Types/Rotator.h"

// FFixedQuaternion static constants
const FFixedQuaternion FFixedQuaternion::Identity = FFixedQuaternion(FFixedPoint::Zero, FFixedPoint::Zero, FFixedPoint::Zero, FFixedPoint::One);

// To Rotator conversion
FFixedRotator FFixedQuaternion::Rotator() const
{
    const FFixedVector Euler = Eulers();
    return FFixedRotator(
        Euler.Y * FFixedPoint::RadToDeg,  // Pitch
        Euler.Z * FFixedPoint::RadToDeg,  // Yaw
        Euler.X * FFixedPoint::RadToDeg   // Roll
    );
}
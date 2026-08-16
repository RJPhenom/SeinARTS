/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCombatMath.h
 * @brief   Shared module-internal fixed-point helpers (named namespace so
 *          unity builds can merge the combat translation units).
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"

namespace SeinCombatInternal
{
	/** Overflow-safe planar distance (the raw Size() wraps beyond ~463 m). */
	FORCEINLINE FFixedPoint PlanarDistanceSaturated(
		const FFixedVector& A, const FFixedVector& B)
	{
		FFixedVector PlanarA = A;
		FFixedVector PlanarB = B;
		PlanarA.Z = FFixedPoint::Zero;
		PlanarB.Z = FFixedPoint::Zero;
		return FFixedVector::DistanceSaturated(PlanarA, PlanarB);
	}
}

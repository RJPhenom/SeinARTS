/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    FixedCurve.cpp
 */

#include "Math/FixedCurve.h"
#include "Curves/RichCurve.h"

FFixedPoint FFixedCurve::Sample(FFixedPoint In) const
{
	const FRichCurve* Rich = Curve.GetRichCurveConst();
	if (!Rich) return FFixedPoint::Zero;

	const TArray<FRichCurveKey>& Keys = Rich->GetConstRefOfKeys();
	const int32 N = Keys.Num();
	if (N == 0) return FFixedPoint::Zero;
	if (N == 1) return FFixedPoint::FromFloat(Keys[0].Value);

	// Clamp outside the key range to the end values.
	if (In <= FFixedPoint::FromFloat(Keys[0].Time))     { return FFixedPoint::FromFloat(Keys[0].Value); }
	if (In >= FFixedPoint::FromFloat(Keys[N - 1].Time)) { return FFixedPoint::FromFloat(Keys[N - 1].Value); }

	for (int32 i = 0; i + 1 < N; ++i)
	{
		const FFixedPoint T0 = FFixedPoint::FromFloat(Keys[i].Time);
		const FFixedPoint T1 = FFixedPoint::FromFloat(Keys[i + 1].Time);
		if (In < T0 || In > T1) continue;

		const FFixedPoint V0 = FFixedPoint::FromFloat(Keys[i].Value);
		const FFixedPoint V1 = FFixedPoint::FromFloat(Keys[i + 1].Value);

		// Constant: hold the left value across the segment.
		if (Keys[i].InterpMode == RCIM_Constant) { return V0; }

		const FFixedPoint Span = T1 - T0;
		if (Span <= FFixedPoint::Zero) { return V0; }  // coincident keys
		const FFixedPoint T = (In - T0) / Span;        // normalized 0..1 within the segment

		// Linear (also the fallback for RCIM_None).
		if (Keys[i].InterpMode != RCIM_Cubic)
		{
			return V0 + (V1 - V0) * T;
		}

		// Cubic Hermite — the same basis FRichCurve uses (FMath::CubicInterp), in fixed-point. The
		// per-unit tangents are scaled by the segment span, matching FRichCurve's convention.
		const FFixedPoint M0    = FFixedPoint::FromFloat(Keys[i].LeaveTangent)      * Span;
		const FFixedPoint M1    = FFixedPoint::FromFloat(Keys[i + 1].ArriveTangent) * Span;
		const FFixedPoint T2    = T * T;
		const FFixedPoint T3    = T2 * T;
		const FFixedPoint Two   = FFixedPoint::Two;
		const FFixedPoint Three = FFixedPoint::FromInt(3);
		const FFixedPoint H00 = Two * T3 - Three * T2 + FFixedPoint::One;  //  2t³ - 3t² + 1
		const FFixedPoint H10 = T3 - Two * T2 + T;                          //   t³ - 2t² + t
		const FFixedPoint H01 = Three * T2 - Two * T3;                      // -2t³ + 3t²
		const FFixedPoint H11 = T3 - T2;                                    //   t³ - t²
		return H00 * V0 + H10 * M0 + H01 * V1 + H11 * M1;
	}

	return FFixedPoint::FromFloat(Keys[N - 1].Value);
}

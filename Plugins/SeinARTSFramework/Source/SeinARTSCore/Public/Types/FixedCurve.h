/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    FixedCurve.h
 * @brief   A deterministic 1-D response curve sampled in fixed-point — usable as movement-mode tuning.
 */

#pragma once

#include "CoreMinimal.h"
#include "FixedPoint.h"
#include "Vector2D.h"
#include "FixedCurve.generated.h"

/**
 * A deterministic 1-D response curve, sampled in fixed-point — safe as movement-mode tuning data
 * (it carries the SeinDeterministic marker, so it's accepted as a tuning variable / UDS field).
 *
 * Keys are (X = input, Y = output) points. `Sample` interpolates piecewise-linearly between adjacent
 * keys and clamps to the end values outside the key range. Keys must be sorted by X ascending.
 *
 * This is the array-of-points interim form: edited as a normal array of (X, Y) fixed-point pairs in
 * the details panel (no curve-editor canvas yet). It already drives the common vehicle-feel curves
 * (grip vs. speed, throttle falloff, turn-rate vs. speed); a Slate curve widget can layer on later.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCORE_API FFixedCurve
{
	GENERATED_BODY()

	/** Curve points — each X is an input, each Y the output at that input. Keep sorted by X ascending. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Curve")
	TArray<FFixedVector2D> Keys;

	/** Output at `In` — piecewise-linear between keys, clamped to the end values outside the key range.
	 *  Deterministic (fixed-point throughout). Returns 0 for an empty curve. */
	FFixedPoint Sample(FFixedPoint In) const
	{
		const int32 N = Keys.Num();
		if (N == 0) return FFixedPoint::Zero;
		if (N == 1 || In <= Keys[0].X) return Keys[0].Y;
		if (In >= Keys[N - 1].X) return Keys[N - 1].Y;

		for (int32 i = 0; i + 1 < N; ++i)
		{
			const FFixedPoint X0 = Keys[i].X;
			const FFixedPoint X1 = Keys[i + 1].X;
			if (In >= X0 && In <= X1)
			{
				const FFixedPoint Span = X1 - X0;
				if (Span <= FFixedPoint::Zero) return Keys[i].Y;  // coincident keys → step
				const FFixedPoint T = (In - X0) / Span;
				return Keys[i].Y + (Keys[i + 1].Y - Keys[i].Y) * T;
			}
		}
		return Keys[N - 1].Y;
	}
};

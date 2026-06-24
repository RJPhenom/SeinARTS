/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    FixedCurve.h
 * @brief   A response curve authored with Unreal's native curve editor, sampled deterministically
 *          in fixed-point — usable as movement-mode tuning.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Curves/CurveFloat.h"   // FRuntimeFloatCurve (native curve editor)
#include "FixedCurve.generated.h"

/**
 * A 1-D response curve authored with Unreal's native curve editor and sampled DETERMINISTICALLY in
 * fixed-point — safe as movement-mode tuning data (it carries the SeinDeterministic marker, so it's
 * accepted as a tuning variable / UDS field).
 *
 * The curve is authored as a normal FRuntimeFloatCurve, so designers get the full curve-editor UX
 * (drag points, tangents, interp modes). `Sample` evaluates it in fixed-point by reading the AUTHORED
 * keys + tangents and converting them to fixed-point. That's lockstep-safe: curve data is authored
 * content, identical on every client, so the float→fixed conversion and the fixed-point evaluation
 * are bit-deterministic — the same "editor-authored values, deterministic at runtime" pattern as
 * baked cover-slot scatter. Constant / Linear / Cubic interp are matched (Cubic via the same Hermite
 * basis FRichCurve uses); weighted tangents are treated as unweighted.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FFixedCurve
{
	GENERATED_BODY()

	/** The curve, authored with Unreal's native curve editor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Curve")
	FRuntimeFloatCurve Curve;

	/** Output at `In`, evaluated in fixed-point against the authored keys (deterministic). Clamps to
	 *  the end values outside the key range; returns 0 for an empty curve. */
	FFixedPoint Sample(FFixedPoint In) const;
};

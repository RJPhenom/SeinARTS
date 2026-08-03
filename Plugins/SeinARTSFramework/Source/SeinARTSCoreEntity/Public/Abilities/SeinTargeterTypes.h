/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTargeterTypes.h
 * @brief   Wire-format types for the targeter system.
 *
 *          The targeter is a render-side state machine owned by the local
 *          player. It captures cursor input (point click, point + drag-rotate
 *          for buildings, line drag for strafing runs) and emits a single
 *          deterministic FSeinCommand on confirm. The captured input travels
 *          to the sim via FSeinTargeterPoint, packed into the broker order
 *          payload alongside the existing FormationEnd / context fields.
 *
 *          Each FSeinTargeterPoint represents one full input cycle the
 *          targeter required:
 *            - Simple point   → Location populated, AuxLocation zero
 *            - Drag-rotate    → Location populated, RotationStep populated
 *            - Drag-line      → Location + AuxLocation populated
 *
 *          Multi-target abilities (e.g. "throw 3 grenades") declare TargetCount > 1
 *          on their USeinTargeterSpec; the targeter loops N capture cycles and
 *          ships TargeterPoints.Num() == N entries in a single command.
 *
 *          Abilities read TargeterPoints[0].Location into TargetLocation for
 *          single-target convenience; the full array is exposed as a runtime
 *          UPROPERTY on USeinAbility for multi-target abilities to iterate.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinTargeterTypes.generated.h"

/**
 * One captured target from a targeter input cycle.
 *
 * Three fields cover all current spec types without forcing every ability to
 * understand every spec's payload shape:
 *   - Location:     primary world point (always set on capture)
 *   - AuxLocation:  secondary world point for drag specs (line endpoint).
 *                   Zero when the spec is point-only.
 *   - RotationStep: 0..N-1 quantized rotation index for drag-rotate specs.
 *                   N defaults to 4 (90° steps, matching the placement-yaw convention)
 *                   but specs can declare different step counts.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinTargeterPoint
{
	GENERATED_BODY()

	/** Primary captured world point (RMB-down for drag, RMB-click for simple point). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Targeter")
	FFixedVector Location;

	/** Secondary captured world point — drag-line endpoint. Zero for non-drag specs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Targeter")
	FFixedVector AuxLocation;

	/** Quantized rotation index for drag-rotate specs with snapping enabled.
	 *  When the spec's RotationStepDegrees > 0, this holds 0..StepCount-1 for
	 *  the chosen step (e.g. 0/1/2/3 for 90°-step cardinal orientations).
	 *  Zero for non-rotation specs OR free-rotation drag (RotationStepDegrees = 0).
	 *
	 *  Most ability OnActivate logic should read YawDegrees instead — it works
	 *  uniformly for both snapped and free rotation. RotationStep is kept for
	 *  legacy + cases where the integer step is genuinely useful (e.g. mapping
	 *  to the navigation placement gate's discrete yaw convention). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Targeter")
	uint8 RotationStep = 0;

	/** Captured yaw rotation in degrees (continuous, 0..360). For drag-rotate
	 *  specs this is computed from anchor → cursor direction at confirm time:
	 *    - Spec with RotationStepDegrees > 0  → snapped to nearest step
	 *    - Spec with RotationStepDegrees == 0 → free rotation, raw cursor yaw
	 *  Zero for non-drag specs (Point spec). Read this in OnActivate to spawn
	 *  the building / aim the ability at the captured rotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Targeter")
	FFixedPoint YawDegrees = FFixedPoint::Zero;

	/** Convenience constructor for the simple-point case. */
	FSeinTargeterPoint() = default;
	explicit FSeinTargeterPoint(const FFixedVector& InLocation)
		: Location(InLocation) {}
	FSeinTargeterPoint(const FFixedVector& InLocation, const FFixedVector& InAux)
		: Location(InLocation), AuxLocation(InAux) {}
};

/** Every defaulted field above has an all-zero representation. Advertising
 *  that contract lets bounded reflected decoding allocate targeter-point
 *  arrays without invoking a native constructor that might hide variable
 *  storage outside the reflected byte limits. */
template<>
struct TStructOpsTypeTraits<FSeinTargeterPoint>
	: public TStructOpsTypeTraitsBase2<FSeinTargeterPoint>
{
	enum
	{
		WithZeroConstructor = true,
	};
};

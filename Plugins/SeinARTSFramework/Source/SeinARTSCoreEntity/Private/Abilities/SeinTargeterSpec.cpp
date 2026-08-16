/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTargeterSpec.cpp
 * @brief   Targeter spec base + Phase 1 PointTargeterSpec implementations.
 */

#include "Abilities/SeinTargeterSpec.h"
#include "Types/Vector.h"

// Note: ASeinTargeterPreview lives in the SeinARTSFramework module; CoreEntity
// only knows the abstract base class via forward declaration in the header.
// The concrete default preview class is resolved via GetDefaultPreviewClass(),
// which the framework module overrides on its preview-aware subclasses. For
// Phase 1 we leave the default null on the CoreEntity-side base — the
// framework-side subsystem falls back to a built-in preview if PreviewClass
// is unset.

FSoftClassPath USeinTargeterSpec::ResolvePreviewClass() const
{
	if (PreviewClass.IsValid())
	{
		return PreviewClass;
	}
	return GetDefaultPreviewClass();
}

USeinPointTargeterSpec::USeinPointTargeterSpec()
{
	// Sensible defaults for the simple-point case: single click, no strict
	// click-blocking. Designers tune per-ability via the editor.
	TargetCount = 1;
	bRejectClickWhenBlocked = false;
}

FSoftClassPath USeinPointTargeterSpec::GetDefaultPreviewClass() const
{
	// CoreEntity can't reference the framework-side ASeinPointTargeterPreview
	// directly without a circular dependency. The targeter subsystem (in the
	// framework module) handles fallback by spawning ASeinPointTargeterPreview
	// when the spec returns an empty path here. This keeps CoreEntity render-agnostic.
	return FSoftClassPath();
}

USeinPointFacingTargeterSpec::USeinPointFacingTargeterSpec()
{
	// Building-placement defaults: single capture, strict click-blocking
	// (you can't place a bunker in occupied cells), free rotation.
	// Set RotationStepDegrees > 0 in subclasses / instances if your project
	// wants cardinal/octagonal snapping (90 / 45 are common). Free-by-default
	// matches standard click-to-place UX.
	TargetCount = 1;
	bRejectClickWhenBlocked = true;
	RotationStepDegrees = 0;
}

FSoftClassPath USeinPointFacingTargeterSpec::GetDefaultPreviewClass() const
{
	// Same pattern as PointTargeterSpec — null path here, framework subsystem
	// falls back to ASeinPointFacingTargeterPreview when neither the spec
	// instance nor this default returns a class.
	return FSoftClassPath();
}

USeinLineTargeterSpec::USeinLineTargeterSpec()
{
	// Combat-line defaults: one segment, permissive click-through (server
	// rejects with reason feedback rather than eating the click). Trench-style
	// abilities flip CaptureMode to MultiClick and raise TargetCount.
	TargetCount = 1;
	bRejectClickWhenBlocked = false;
}

ESeinTargeterValidity USeinLineTargeterSpec::ValidateClient_Implementation(
	const FFixedVector& CursorWorld,
	const FFixedVector& AuxWorld) const
{
	// Advisory client UX only (float math is fine here — the base-class
	// contract documents ValidateClient as non-sim; the server re-validates).
	// Before a segment exists (no aux point yet) there is nothing to check.
	if (MaxSegmentLength <= FFixedPoint::Zero || AuxWorld.IsZero())
	{
		return ESeinTargeterValidity::Valid;
	}
	const float SegmentLength = static_cast<float>(
		FVector::Dist2D(CursorWorld.ToVector(), AuxWorld.ToVector()));
	return SegmentLength > MaxSegmentLength.ToFloat()
		? ESeinTargeterValidity::Blocked
		: ESeinTargeterValidity::Valid;
}

FSoftClassPath USeinLineTargeterSpec::GetDefaultPreviewClass() const
{
	// Null path — the framework subsystem falls back to
	// ASeinLineTargeterPreview, keeping CoreEntity render-agnostic.
	return FSoftClassPath();
}

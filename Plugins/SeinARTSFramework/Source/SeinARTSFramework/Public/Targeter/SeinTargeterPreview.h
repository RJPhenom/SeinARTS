/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTargeterPreview.h
 * @brief   Base render-side preview actor spawned by USeinTargeterSubsystem
 *          while the targeter is active.
 *
 *          Reads three things from the subsystem each tick:
 *            - cursor world position
 *            - active drag-anchor world position (for drag specs)
 *            - current validity tri-state (Valid/Warning/Blocked)
 *
 *          Renders accordingly. Subclasses specialize visualization (decal,
 *          AoE ring, building hologram, line corridor). Phase 1 ships
 *          ASeinPointTargeterPreview only.
 *
 *          Pure presentation — never mutates sim state. Spawned + destroyed
 *          by the targeter subsystem, never by ability code or sim ticks.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Abilities/SeinTargeterSpec.h"
#include "SeinTargeterPreview.generated.h"

class USeinTargeterSpec;

UCLASS(Abstract, NotPlaceable)
class SEINARTSFRAMEWORK_API ASeinTargeterPreview : public AActor
{
	GENERATED_BODY()

public:
	ASeinTargeterPreview();

	virtual void Tick(float DeltaSeconds) override;

	/** Initialize this preview with the spec that drove its spawn. Stores the
	 *  spec for subclasses to read declared parameters (radius, footprint, etc.)
	 *  during Tick / state updates. Called by the subsystem right after spawn. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Targeter")
	virtual void InitializePreview(USeinTargeterSpec* InSpec, float InAreaRadiusWorld);

	/** Updates per-frame state from the subsystem. Cursor is the live cursor
	 *  world position; DragAnchor is the press-down anchor when the user is
	 *  mid-drag (zero vector when no drag in progress). Validity tints the
	 *  preview red/yellow/normal. DragYawDegrees is the yaw the subsystem
	 *  WILL CAPTURE on confirm — already snapped if the spec has snapping
	 *  enabled, raw cursor yaw otherwise. Drag-aware previews use this for
	 *  actor rotation so visual + captured value stay in lockstep. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Targeter")
	void UpdatePreview(const FVector& CursorWorld, const FVector& DragAnchorWorld,
		ESeinTargeterValidity Validity, float DragYawDegrees);

	/** Called by the subsystem after each captured input cycle so multi-cycle
	 *  previews can accumulate committed visuals (placed trench segments,
	 *  earlier grenade markers). Start/End are the cycle's primary and aux
	 *  points; End equals Start for point captures. Base implementation is a
	 *  no-op. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Targeter")
	void NotifyPointCaptured(const FVector& StartWorld, const FVector& EndWorld);

protected:
	/** Spec that spawned this preview. Subclasses cast to their concrete type. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "SeinARTS|Targeter")
	TObjectPtr<USeinTargeterSpec> Spec;

	/** Cached AoE radius in world units (read from USeinAbility::AreaRadius
	 *  on subsystem activation; converted to float for render-side use).
	 *  Zero when the originating ability is not AoE. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "SeinARTS|Targeter")
	float AreaRadiusWorld = 0.0f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "SeinARTS|Targeter")
	FVector CurrentCursorWorld = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "SeinARTS|Targeter")
	FVector CurrentDragAnchorWorld = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "SeinARTS|Targeter")
	ESeinTargeterValidity CurrentValidity = ESeinTargeterValidity::Valid;

	/** Yaw (degrees) the subsystem will capture on confirm — already snapped if
	 *  RotationStepDegrees > 0, raw cursor direction otherwise. Drag-aware
	 *  previews use this directly for actor rotation so visual matches capture. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "SeinARTS|Targeter")
	float CurrentDragYawDegrees = 0.0f;

	/** Subclass hook — called from UpdatePreview after state is stored.
	 *  Default impl is a no-op; subclasses override to drive material params,
	 *  decal transforms, mesh colors, etc. BlueprintNativeEvent so designers
	 *  can override visualization in BP without C++. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Targeter")
	void OnPreviewUpdated();
	virtual void OnPreviewUpdated_Implementation() {}

	/** Subclass hook — called from NotifyPointCaptured after a cycle commits.
	 *  BlueprintNativeEvent so BP previews can accumulate visuals too. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Targeter")
	void OnPointCaptured(const FVector& StartWorld, const FVector& EndWorld);
	virtual void OnPointCaptured_Implementation(
		const FVector& StartWorld, const FVector& EndWorld) {}
};

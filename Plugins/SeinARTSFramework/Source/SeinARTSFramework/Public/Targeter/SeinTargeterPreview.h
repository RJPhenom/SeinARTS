/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinTargeterPreview.h
 * @author       RJ Macklem
 * @created      02 Jun 2026
 * @latest       12 Sep 2026
 * @brief        General Blueprint presentation API for local target capture sessions.
 *
 * Input capture and authoritative validation remain outside this actor. Optional
 * visual components render the shared context; Blueprint can supply custom visuals.
 *
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Abilities/SeinTargeterSpec.h"
#include "Targeter/SeinTargeterPreviewContext.h"
#include "SeinTargeterPreview.generated.h"

class USeinTargeterSpec;

UCLASS(Blueprintable, NotPlaceable)
class SEINARTSFRAMEWORK_API ASeinTargeterPreview : public AActor
{
	GENERATED_BODY()

public:
	ASeinTargeterPreview();

	virtual void Tick(float DeltaSeconds) override;

	/** Follow the confirmed target pose automatically. Disable when Blueprint owns the actor transform. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SeinARTS")
	bool bFollowResolvedTarget = true;

	/** Latest read-only target capture snapshot. Available before Preview Initialized. */
	UPROPERTY(BlueprintReadOnly, Transient, Category="SeinARTS")
	FSeinTargeterPreviewContext Context;

	/** Start presentation after deferred spawning and component initialization finish. */
	void BeginPreview(const FSeinTargeterPreviewContext& InContext);
	/** Deliver a complete snapshot without requiring Blueprint to derive capture geometry. */
	void Present(const FSeinTargeterPreviewContext& InContext);
	/** Deliver the final reason once, before the actor is destroyed. */
	void EndPreview(ESeinPreviewEndReason Reason, bool bNotify = true);

	/** Preserve anchor presence independently of its coordinates, including the world origin. */
	void SetCaptureHasAnchor(bool bHasAnchor) { bCaptureHasAnchor = bHasAnchor; }

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

	/** Called after each captured input cycle so multi-cycle
	 *  previews can accumulate committed visuals (placed trench segments,
	 *  earlier grenade markers). Start/End are the cycle's primary and aux
	 *  points; End equals Start for point captures. Base implementation is a
	 *  no-op. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Targeter")
	void NotifyPointCaptured(const FVector& StartWorld, const FVector& EndWorld);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** Runs once after context and visual helpers are initialized. Override to build custom visuals. */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Targeter")
	void OnPreviewInitialized();
	virtual void OnPreviewInitialized_Implementation() {}
	/** Runs initially and whenever validity or its reason changes. Helpers have already updated materials. */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Targeter")
	void OnValidityChanged(ESeinTargeterValidity PreviousValidity);
	virtual void OnValidityChanged_Implementation(ESeinTargeterValidity PreviousValidity) {}
	/** Runs once before teardown. Submitted means command submission, not successful gameplay execution. */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Targeter")
	void OnPreviewEnded(ESeinPreviewEndReason Reason);
	virtual void OnPreviewEnded_Implementation(ESeinPreviewEndReason Reason) {}

	/** Configure optional visual helpers. Legacy subclasses retain their serialized component setup here. */
	virtual void PrepareVisuals() {}
	bool bPresentationStarted = false;
	bool bPresentationEnded = false;
	bool bHasPresented = false;
	bool bCaptureHasAnchor = false;

	/** Spec that spawned this preview. Subclasses cast to their concrete type. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "SeinARTS")
	TObjectPtr<USeinTargeterSpec> Spec;

	/** Cached AoE radius in world units (read from USeinAbility::AreaRadius
	 *  on subsystem activation; converted to float for render-side use).
	 *  Zero when the originating ability is not AoE. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "SeinARTS")
	float AreaRadiusWorld = 0.0f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "SeinARTS")
	FVector CurrentCursorWorld = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "SeinARTS")
	FVector CurrentDragAnchorWorld = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "SeinARTS")
	ESeinTargeterValidity CurrentValidity = ESeinTargeterValidity::Valid;

	/** Yaw (degrees) the subsystem will capture on confirm — already snapped if
	 *  RotationStepDegrees > 0, raw cursor direction otherwise. Drag-aware
	 *  previews use this directly for actor rotation so visual matches capture. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "SeinARTS")
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

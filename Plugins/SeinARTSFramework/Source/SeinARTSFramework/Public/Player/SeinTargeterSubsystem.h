/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinTargeterSubsystem.h
 * @author       RJ Macklem
 * @created      02 Jun 2026
 * @latest       14 Aug 2026
 * @brief        Owns the per-local-player target capture state machine.
 *
 *          Survives map travel, scoped to one local player (split-screen safe).
 *          Owns the active targeter mode (spec + ability tag + capture progress)
 *          and the spawned preview actor. Driven by ASeinPlayerController input
 *          forwarding — when a targeter is active, RMB-down/up is consumed by
 *          the subsystem instead of triggering the right-click smart command,
 *          and LMB (Select) cancels the targeter.
 *
 *          Point specs capture one click per cycle. Point-plus-facing specs use
 *          the same state machine to capture a location and drag-facing. Line
 *          and corridor specs are not currently shipped.
 *
 *          On Confirm the subsystem hands the captured points to
 *          ASeinPlayerController::IssueTargetedAbility, which packs them into
 *          a Command_Type_BrokerOrder with a PredeterminedAbilityTag and
 *          submits via the lockstep wire. The targeter then resets to Idle.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Abilities/SeinTargeterTypes.h"
#include "Abilities/SeinTargeterSpec.h"
#include "SeinTargeterSubsystem.generated.h"

class ASeinTargeterPreview;
class ASeinPlayerController;
class USeinAbility;

/** Targeter state — drives PC input intercept logic and preview lifecycle. */
UENUM(BlueprintType)
enum class ESeinTargeterState : uint8
{
	/** Nothing active; PC input flows through normal smart-command path. */
	Idle,
	/** Targeter active, waiting for the next RMB to capture a target point.
	 *  PC suppresses smart-command on RMB and routes it to OnConfirmInput. */
	WaitingForCapture,
	/** RMB is held while a point-plus-facing spec captures its drag endpoint. */
	Dragging
};

/** Broadcast when targeter state changes. UI can listen to update cursor /
 *  action panel selection state. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSeinOnTargeterStateChanged, ESeinTargeterState, NewState);

UCLASS()
class SEINARTSFRAMEWORK_API USeinTargeterSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Destroy transient targeting state before module withdrawal. */
	void ReleaseModuleOwnedStateForModuleUnload();

	// ========== Public API ==========

	/** Activate the targeter for a given ability + owner entity.
	 *  - Spec: drives capture flow + preview class. Required.
	 *  - AbilityTag: stamped into the submitted command's PredeterminedAbilityTag.
	 *  - OwnerLeader: entity whose ability is being targeted (for range / LOS hints).
	 *  - AreaRadiusWorld: copied from USeinAbility::AreaRadius converted to world units.
	 *  Cancels any prior targeter session. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Targeter")
	void Activate(USeinTargeterSpec* InSpec, FGameplayTag InAbilityTag,
		FSeinEntityHandle InOwnerLeader, float InAreaRadiusWorld);

	/** Cancel without submitting. Despawns preview, resets state to Idle. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Targeter")
	void Cancel();

	/** PC input forwarding: confirm-input bound to the player's Command action
	 *  (RMB by default; remappable via input config). When a targeter is active,
	 *  the PC routes its RMB events here instead of issuing smart commands. */
	void OnConfirmPressed();
	void OnConfirmReleased();

	/** PC input forwarding: cancel-input bound to the player's Select action
	 *  (LMB by default). Cancels the active targeter. PC routes Select clicks
	 *  here only while the targeter is active; otherwise selection works normally. */
	void OnCancelInput();

	/** Per-frame cursor update. Called by ASeinPlayerController::Tick when the
	 *  targeter is active. Drives preview transform + tri-state validity. */
	void UpdateCursor(const FVector& CursorWorld);

	/** Returns the current drag-anchor world position when the state is Dragging,
	 *  otherwise zero vector. The preview reads this each tick to lock its
	 *  position while rotating with the cursor. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Targeter")
	FVector GetDragAnchor() const { return State == ESeinTargeterState::Dragging ? DragAnchorWorld : FVector::ZeroVector; }

	/** Returns the snapped yaw rotation (degrees) the spec is currently
	 *  reporting. Only meaningful while Dragging; zero otherwise. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Targeter")
	float GetSnappedYawDegrees() const { return SnappedYawDegrees; }

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Targeter")
	ESeinTargeterState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Targeter")
	bool IsActive() const { return State != ESeinTargeterState::Idle; }

	/** Number of points captured so far in the current session. UI can show
	 *  e.g. "2 / 3 grenades placed" for multi-target abilities. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Targeter")
	int32 GetCapturedCount() const { return CapturedPoints.Num(); }

	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|Targeter")
	FSeinOnTargeterStateChanged OnStateChanged;

private:
	/** Internal — fully reset to idle state, despawn preview, clear data. */
	void ResetToIdle();

	/** Capture one point at the current cursor + advance the cycle counter.
	 *  Used by both the click-to-capture path (point spec) and the drag-end
	 *  path (point-facing spec); the drag path passes a non-zero AuxLocation
	 *  and snapped rotation step. When CapturedPoints.Num() == Spec->TargetCount,
	 *  submit + reset. */
	void CapturePoint();

	/** Drag variant of CapturePoint — captures the locked drag anchor as Location,
	 *  the cursor (drag end) as AuxLocation, and the spec's quantized rotation
	 *  step as RotationStep. Called from OnConfirmReleased when in Dragging state. */
	void CaptureDragPoint();

	/** Submit the captured points via PC->IssueTargetedAbility. Called when the
	 *  capture loop has accumulated TargetCount points. */
	void Submit();

	/** Compute the snapped yaw degrees from anchor → cursor direction, quantized
	 *  to the spec's RotationStepDegrees. Returns the snapped yaw and the
	 *  matching step index (0..StepCount-1). */
	void ComputeDragRotation(float& OutSnappedYawDegrees, uint8& OutStepIndex) const;

	/** True when the active spec uses the shipped point-plus-facing drag flow. */
	bool IsDragSpec() const;

	/** Convert a world FVector to a deterministic FFixedVector for sim submission. */
	static FFixedVector ToFixed(const FVector& World);

	/** Get the owning PC. Cached on activate; re-resolved if cache stale. */
	ASeinPlayerController* GetPlayerController() const;

	void SetState(ESeinTargeterState NewState);

private:
	UPROPERTY(Transient)
	TObjectPtr<USeinTargeterSpec> Spec;

	UPROPERTY(Transient)
	TObjectPtr<ASeinTargeterPreview> Preview;

	/** Cached PC ref for fast access during Tick. Re-resolved if null.
	 *  Weak pointer (no UPROPERTY) — matches the existing CachedWorldSubsystem
	 *  pattern in ASeinPlayerController. */
	mutable TWeakObjectPtr<ASeinPlayerController> CachedPC;

	UPROPERTY(Transient)
	FGameplayTag PendingAbilityTag;

	UPROPERTY(Transient)
	FSeinEntityHandle PendingOwnerLeader;

	UPROPERTY(Transient)
	float PendingAreaRadiusWorld = 0.0f;

	/** Points captured so far in the active session. Submitted when its size
	 *  reaches Spec->TargetCount. */
	UPROPERTY(Transient)
	TArray<FSeinTargeterPoint> CapturedPoints;

	UPROPERTY(Transient)
	FVector LastCursorWorld = FVector::ZeroVector;

	UPROPERTY(Transient)
	ESeinTargeterState State = ESeinTargeterState::Idle;

	/** World-space anchor recorded on RMB-down for drag specs. The captured
	 *  point's Location is this anchor; cursor motion drives rotation only,
	 *  not position. Zero when not in the Dragging state. */
	UPROPERTY(Transient)
	FVector DragAnchorWorld = FVector::ZeroVector;

	/** Most recent snapped yaw (degrees) computed from anchor → cursor
	 *  direction. Updated each tick while Dragging. Pushed to the preview
	 *  via UpdateCursor for visual rotation tracking. */
	UPROPERTY(Transient)
	float SnappedYawDegrees = 0.0f;

	/** Most recent quantized rotation step (0..StepCount-1). Captured into
	 *  the FSeinTargeterPoint when the drag ends. */
	UPROPERTY(Transient)
	uint8 SnappedStepIndex = 0;
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPlayerController.h
 * @brief   RTS player controller with selection, smart command resolution,
 *          control groups, active focus cycling, and observer command logging.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinEntityHandle.h"
#include "GameplayTagContainer.h"
#include "SeinPlayerController.generated.h"

class ASeinActor;
class ASeinCameraPawn;
class USeinWorldSubsystem;
class USeinTargeterSubsystem;
class USeinOrderGesture;
struct FSeinTargeterPoint;

// ==================== Delegates ====================

/** Broadcast when the selection changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSeinOnSelectionChanged);

/** Broadcast when a command is issued (for VFX/audio feedback). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSeinOnCommandIssued, FGameplayTag, AbilityTag, FVector, WorldLocation);

/** Broadcast when an action slot hotkey is pressed (for UI action panel). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSeinOnActionSlotPressed, int32, SlotIndex);

/** Broadcast when the menu/escape key is pressed. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSeinOnMenuPressed);

/** Broadcast each PC tick with the cursor's traced world position and a flag
 *  indicating whether the trace hit anything. Optional subscription point for
 *  modules that need live cursor coordinates without polling — currently
 *  consumed by the cover module's destination preview subsystem to update the
 *  formation decal layout when the cursor moves. The delegate fires regardless
 *  of selection / targeter state; subscribers gate themselves. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSeinOnCursorUpdated, FVector, CursorWorld, bool, bValidTrace);

/**
 * RTS player controller.
 *
 * Handles:
 * - Unit selection (click, marquee box, control groups, active focus cycling)
 * - Smart command resolution (right-click → ability tag via the entity's DefaultCommands)
 * - Observer command logging (camera + selection snapshots for replays)
 * - Modifier key tracking (Shift, Ctrl, Alt)
 *
 * All interaction is render-side. Command drafts cross either the active
 * lockstep transport or the world's authenticated standalone ingress; the
 * controller never mutates simulation state directly.
 *
 * Input wiring is Blueprint-owned (the standard Enhanced Input pattern): a
 * Blueprint subclass adds its Input Mapping Context on BeginPlay and wires
 * Enhanced Input action events to the Handle* / Set*Held functions below.
 * C++ owns the behavior behind each function; the Blueprint graph owns which
 * action triggers it.
 */
UCLASS(Blueprintable)
class SEINARTSFRAMEWORK_API ASeinPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASeinPlayerController();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SeamlessTravelFrom(APlayerController* OldPC) override;

	// ========== Configuration ==========

	/** Keyboard pan speed multiplier (applied by Handle Camera Pan). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Camera", meta = (ClampMin = "0.0"))
	float KeyPanSpeed = 1.0f;

	/** Keyboard rotate speed multiplier (applied by Handle Camera Rotate). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Camera", meta = (ClampMin = "0.0"))
	float KeyRotateSpeed = 1.0f;

	/** Keyboard zoom speed multiplier (applied by Handle Camera Zoom Keyboard). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Camera", meta = (ClampMin = "0.0"))
	float KeyZoomSpeed = 1.0f;

	/** Mouse (middle-button drag) pan speed multiplier (applied by Handle Camera Mouse Pan). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Camera", meta = (ClampMin = "0.0"))
	float MousePanSpeed = 1.0f;

	/** Mouse orbit speed multiplier (applied by Handle Camera Orbit). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Camera", meta = (ClampMin = "0.0"))
	float MouseRotateSpeed = 1.0f;

	/** Mouse wheel zoom speed multiplier (applied by Handle Camera Zoom). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Camera", meta = (ClampMin = "0.0"))
	float MouseZoomSpeed = 1.0f;

	/** Trace channel for selection and command line traces. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Selection")
	TEnumAsByte<ECollisionChannel> SelectionTraceChannel = ECC_Visibility;

	/** Maximum trace distance for mouse-to-world projection. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Selection")
	float TraceDistance = 100000.0f;

	/** How often (in sim ticks) to log a camera update observer command. 0 = every tick. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Replay", meta = (ClampMin = "0"))
	int32 CameraLogInterval = 5;

	// ========== Identity ==========

	/** This controller's sim-side player ID. Assigned by GameMode (server-side)
	 *  and replicated to the owning client so client-side ownership checks
	 *  (selection, command stamping, "is this my unit?") work correctly.
	 *  Without replication, the client's PC sees SeinPlayerID = 0 (neutral)
	 *  and can't select / command its own units. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "SeinARTS|Player")
	FSeinPlayerID SeinPlayerID;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ========== Selection State ==========

	/** Currently selected actors (render-side, weak pointers to avoid dangling). */
	TArray<TWeakObjectPtr<ASeinActor>> SelectedActors;

	/**
	 * Active focus index within the selection.
	 * -1 = "All" (commands dispatch to entire selection).
	 * 0+ = index into SelectedActors (commands dispatch to only that unit).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Selection")
	int32 ActiveFocusIndex = -1;

	/** Actor currently under the mouse cursor (for hover highlight / tooltip). */
	TWeakObjectPtr<ASeinActor> HoveredActor;

	// ========== Events ==========

	/** Fired when the selection or active focus changes. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|Selection")
	FSeinOnSelectionChanged OnSelectionChanged;

	/** Fired when a command is issued (for move markers, attack lines, sound). */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|Command")
	FSeinOnCommandIssued OnCommandIssued;

	/** Fired when an action slot hotkey is pressed (0-11). UI action panel listens to this. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|Input")
	FSeinOnActionSlotPressed OnActionSlotPressed;

	/** Fired when menu/escape is pressed. UI listens for menu toggle or cancel. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|Input")
	FSeinOnMenuPressed OnMenuPressed;

	/** Fired each PC tick with the cursor's traced world position. Subscribed to
	 *  by the cover module's formation preview subsystem (and any other module
	 *  that wants live cursor coords without polling). Fires at the PC's tick
	 *  cadence. `bValidTrace` is false when the under-cursor trace missed (cursor
	 *  off the world) — subscribers should skip in that case. Sites without
	 *  any subscriber pay nothing — the multicast is cheap to fire empty. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|Selection")
	FSeinOnCursorUpdated OnCursorUpdated;

	// ========== Selection API ==========

	/**
	 * Set the current selection to a new list of actors.
	 * Handles deselecting old actors and selecting new ones.
	 * Logs a SelectionChanged observer command.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection")
	void SetSelection(const TArray<ASeinActor*>& NewSelection);

	/** Add actors to the current selection (Shift+click). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection")
	void AddToSelection(const TArray<ASeinActor*>& ActorsToAdd);

	/** Toggle an actor in/out of the selection (Ctrl+click). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection")
	void ToggleSelection(ASeinActor* Actor);

protected:
	/** Resolve squad-membership: any input actor whose entity carries
	 *  FSeinSquadMemberComponent is replaced with its squad's actor. Members are
	 *  never selectable directly — selecting a member always selects the
	 *  whole squad (select-the-squad semantics). Returns deduplicated list with nulls
	 *  stripped. Static so both PC selection methods + external callers can
	 *  use it. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection")
	static TArray<ASeinActor*> ResolveSelectionToSquads(const TArray<ASeinActor*>& Input);

public:

	/** Clear the entire selection. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection")
	void ClearSelection();

	/** Cycle active focus: -1 → 0 → 1 → ... → N-1 → -1 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection")
	void CycleFocus();

	/** Get the currently focused actor, or nullptr if focus is "All". */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Selection")
	ASeinActor* GetFocusedActor() const;

	/** Get the actor currently under the mouse cursor. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Selection")
	ASeinActor* GetHoveredActor() const { return HoveredActor.Get(); }

	/** Get the number of currently selected actors. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Selection")
	int32 GetSelectionCount() const { return SelectedActors.Num(); }

	/** Get valid selected actors (removes stale entries). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection")
	TArray<ASeinActor*> GetValidSelectedActors();

	// ========== Control Groups ==========

	/** Assign current selection to a control group (Ctrl+number). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection")
	void AssignControlGroup(int32 GroupIndex);

	/** Recall a control group (number key). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection")
	void RecallControlGroup(int32 GroupIndex);

	/** Get the entity handles in a control group (0-9). Returns empty if out of range. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Selection")
	TArray<FSeinEntityHandle> GetControlGroup(int32 GroupIndex) const;

	/** Get the number of entities in a control group. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Selection")
	int32 GetControlGroupCount(int32 GroupIndex) const;

	// ========== Command Resolution ==========

	/**
	 * Build context tags for a right-click action based on what's under the cursor.
	 * Override in BP to add custom context tags (e.g., Target.Garrisonable).
	 * @param HitActor - The actor under the cursor (nullptr = ground click)
	 * @param HitLocation - World location of the click
	 * @return Tag container describing the command context
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Command")
	FGameplayTagContainer BuildCommandContext(ASeinActor* HitActor, const FVector& HitLocation) const;

	/**
	 * Issue a smart command to all selected entities (or focused entity).
	 * Resolves per-entity ability tags via the entity's DefaultCommands list.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Command")
	void IssueSmartCommand(const FVector& WorldLocation, ASeinActor* TargetActor);

	/**
	 * Issue a smart command with an optional formation guide.
	 * @param WorldLocation - Command target / anchor location
	 * @param TargetActor   - Actor under cursor (nullptr = ground)
	 * @param bQueue        - Whether to queue rather than replace the current ability
	 * @param GuidePoints   - Gesture guide geometry in WORLD space (empty = simple
	 *                        click). Converted to fixed-point + nav-projected sim-side.
	 * @param FormationTag  - Gesture-nominated formation (invalid = resolver default).
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Command")
	void IssueSmartCommandEx(const FVector& WorldLocation, ASeinActor* TargetActor, bool bQueue,
		const TArray<FVector>& GuidePoints, FGameplayTag FormationTag);

	/** Compute the order the CURRENT drag/cursor state would produce — the SAME
	 *  gesture computation OnCommandReleased commits — exposed for the destination
	 *  preview so preview === commit. OutAnchor = drag start while dragging, else
	 *  CursorWorld; OutGuidePoints / OutFormationTag come from the active gesture. */
	void BuildPreviewOrder(FVector CursorWorld, FVector& OutAnchor,
		TArray<FVector>& OutGuidePoints, FGameplayTag& OutFormationTag) const;

	/** Current right-click queue modifier, shared with destination preview. */
	bool IsQueueModifierHeld() const { return bShiftHeld; }

	/**
	 * Issue a targeter-originated ability command. Called by USeinTargeterSubsystem
	 * after capturing the required number of points; packs them into a
	 * Command_Type_BrokerOrder with PredeterminedAbilityTag set so the broker
	 * resolver dispatches the ability without running per-member context resolution.
	 *
	 * @param AbilityTag    - The ability the player picked from the action slot.
	 * @param OwnerLeader   - Entity scoped to act on the ability (typically the
	 *                        squad leader or the focused unit at activation time).
	 * @param Points        - Captured target points; first entry's Location populates
	 *                        FSeinCommand::TargetLocation for legibility.
	 *
	 * Honors the current shift-modifier state for queue semantics, mirroring
	 * IssueSmartCommandEx's bQueueCommand wiring.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Command")
	void IssueTargetedAbility(FGameplayTag AbilityTag, FSeinEntityHandle OwnerLeader,
		const TArray<FSeinTargeterPoint>& Points);

	// ========== Marquee Selection (used by HUD) ==========

	/** Called by HUD when marquee box selection completes. */
	void ReceiveMarqueeSelection(const TArray<ASeinActor*>& ActorsInBox);

	/** Whether a marquee drag is currently active (for HUD rendering). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Selection")
	bool bIsMarqueeDragging = false;

	/** Screen-space start position of the marquee drag. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Selection")
	FVector2D MarqueeStart = FVector2D::ZeroVector;

	/** Screen-space current position of the marquee drag. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Selection")
	FVector2D MarqueeCurrent = FVector2D::ZeroVector;

	// ========== Drag Order State (used by HUD for formation line rendering) ==========

	/** Whether a command drag (RMB hold) is currently active. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command")
	bool bIsCommandDragging = false;

	/** World-space start point of the command drag. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command")
	FVector CommandDragStart = FVector::ZeroVector;

	/** World-space current endpoint of the command drag. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command")
	FVector CommandDragCurrent = FVector::ZeroVector;

	/** Distance-sampled world-space path captured during a command drag (start …
	 *  current). Fed to the order gesture to build the guide. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command")
	TArray<FVector> CommandDragPath;

	/** Minimum cursor travel (world cm) between captured drag-path points — even
	 *  spacing regardless of drag speed, no point spam on a slow drag. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Command")
	float CommandDragSampleDistance = 100.f;

	/** Pluggable interpreter for right-click / drag orders → guide + formation.
	 *  Null → the base USeinOrderGesture (click → blob, drag → line). Point at a
	 *  subclass to author custom drag semantics (spline, box, path-march, …). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Command")
	TSoftClassPtr<USeinOrderGesture> OrderGestureClass;

	/** Abort every in-progress input gesture and clear the modifier latches, committing
	 *  nothing: no selection change, no order issued, and any active targeter session is
	 *  cancelled. Call from project code whenever gameplay input is interrupted mid-gesture
	 *  — opening a pause or UMG menu, losing window focus, switching input mode — so no
	 *  click, drag, or modifier stays stuck held when input returns. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void ResetInputState();

protected:
	// ========== Input Entry Points ==========
	// Wired in the Blueprint subclass: each Enhanced Input action event calls the
	// matching function below (press/release pairs use the event's Started /
	// Completed pins; add the Canceled pin to the matching cancel handler when a
	// cancelable trigger is used). C++ owns the behavior; the Blueprint owns the wiring.

	/** Primary-select press (LMB down): targeter confirm, else arm click/marquee tracking. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandleSelectPressed();

	/** Primary-select release (LMB up): finish marquee, else resolve the single-click selection. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandleSelectReleased();

	/** Command press (RMB down): targeter cancel, else arm the command-drag tracking. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandleCommandPressed();

	/** Command release (RMB up): interpret the click/drag gesture and issue the smart command. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandleCommandReleased();

	/** Select cancel — wire the select action's Canceled pin here. Unlatches the click and
	 *  marquee state WITHOUT committing anything: no selection change, no marquee resolve.
	 *  Canceled fires instead of Completed when a cancelable trigger (Tap, Hold, Chorded
	 *  Action) ends without triggering; plain Down-trigger bindings never fire it, so this
	 *  wiring is only needed once such a trigger is in play. If the press had been consumed
	 *  as a targeter confirm, the targeter session is cancelled rather than confirmed. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandleSelectCanceled();

	/** Command cancel — wire the command action's Canceled pin here. Unlatches the
	 *  command-drag state WITHOUT issuing an order; the sampled drag path is discarded.
	 *  Canceled fires instead of Completed when a cancelable trigger (Tap, Hold, Chorded
	 *  Action) ends without triggering; plain Down-trigger bindings never fire it. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandleCommandCanceled();

	/** Keyboard camera pan (WASD / arrows). AxisValue scaled by Key Pan Speed. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandleKeyPan(FVector2D AxisValue);

	/** Keyboard camera rotate and tilt. X rotates around the pivot (Q/E) and Y tilts the
	 *  camera pitch between top-down and flat; both are scaled by Key Rotate Speed and
	 *  applied at the camera pawn's per-second rates (Rotation Speed / Tilt Speed). Bind
	 *  an Axis2D action; a yaw-only control scheme simply maps no keys onto the Y axis. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandleKeyRotate(FVector2D RotateValue);

	/** Mouse-wheel camera zoom. ZoomDelta scaled by Mouse Zoom Speed; the camera pawn
	 *  steps its Zoom Step per wheel tick. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandleMouseZoom(float ZoomDelta);

	/** Keyboard camera zoom (Z/X). ZoomDelta scaled by Key Zoom Speed; frame-rate
	 *  independent — the camera pawn zooms at its Zoom Rate in units per second. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandleKeyZoom(float ZoomDelta);

	/** Toggle follow-camera on the focused (or first selected) entity. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandleCameraFollow();

	/** Reset camera rotation to north-facing. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandleCameraReset();

	/** Middle-mouse camera pan. MouseDelta scaled by Mouse Pan Speed; ignored while
	 *  LMB/RMB are held (those own the mouse delta for marquee / formation drags). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandleMousePan(FVector2D MouseDelta);

	/** Mouse camera rotate (X = yaw orbit, Y = pitch tilt), per pixel of mouse movement:
	 *  MouseDelta scaled by Mouse Rotate Speed and the camera pawn's Orbit Sensitivity.
	 *  Ignored while LMB/RMB are held. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandleMouseRotate(FVector2D MouseDelta);

	/** Cycle active focus through the selection. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandleCycleFocus();

	/** Ping the location under the cursor (crosses the lockstep wire to all clients). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandlePing();

	/** Menu / cancel key: broadcasts On Menu Pressed for the UI layer. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandleMenu();

	/** Track the Shift modifier (wire Started → true, Completed → false). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void SetShiftHeld(bool bHeld);

	/** Track the Ctrl modifier (wire Started → true, Completed → false). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void SetCtrlHeld(bool bHeld);

	/** Track the Alt modifier (wire Started → true, Completed → false). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void SetAltHeld(bool bHeld);

	/** Action-slot hotkey (0-11): broadcasts On Action Slot Pressed for the UI action panel. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandleActionSlot(int32 SlotIndex);

	/** Control-group hotkey (0-9): assigns with Ctrl held, otherwise recalls. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandleControlGroup(int32 GroupIndex);

	/** Select all of your units at once — the classic select-army hotkey. Replaces the
	 *  current selection (adds to it while Shift is held), and does nothing when you own
	 *  nothing selectable, so the hotkey never empties an existing selection.
	 *
	 *  Viewport Only limits the sweep to units currently on screen; the default (false)
	 *  selects your whole army map-wide. Gathers with the same rules as marquee/click
	 *  selection (your own, selectable, live entities; squad members resolve to their
	 *  squad), so wire it straight to an Input Action's Started pin. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandleSelectAll(bool bViewportOnly = false);

	/** Select every one of your units of the same type as a reference unit — the classic
	 *  double-click / select-all-of-type gesture. The reference is the unit under the
	 *  cursor when it is yours (double-click wiring), else the focused unit, else the
	 *  first selected; with no reference it does nothing. "Type" is the unit's exact
	 *  Blueprint class, and squad members resolve to their squads, so hovering one rifle
	 *  soldier grabs every squad fielding that soldier type. Replaces the current
	 *  selection (adds while Shift is held).
	 *
	 *  Viewport Only (default true) limits the sweep to units currently on screen, the
	 *  usual double-click convention; pass false for a map-wide type sweep. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Input")
	void HandleSelectAllOfType(bool bViewportOnly = true);

	/** Resolve the active order-gesture instance (OrderGestureClass CDO, or the base
	 *  USeinOrderGesture CDO when unset). Never null. */
	USeinOrderGesture* ResolveOrderGesture() const;

	// ========== Internal Helpers ==========

	/** Reference unit anchoring HandleSelectAllOfType: the hovered actor when it is an
	 *  own entity, else the focused actor, else the first live selected actor. Null when
	 *  none of those exist. */
	ASeinActor* ResolveTypeReferenceActor() const;

	/** Gather this player's live, selectable, entity-backed actors via the actor bridge.
	 *  MatchClass (optional) keeps only that exact actor class; bViewportOnly keeps only
	 *  actors whose location projects inside the local viewport. */
	TArray<ASeinActor*> GatherOwnedSelectableActors(UClass* MatchClass, bool bViewportOnly);

	/** True when the world location projects inside the local viewport bounds. */
	bool IsWorldLocationOnScreen(const FVector& WorldLocation);

	/** Perform a line trace under the mouse cursor. */
	bool TraceUnderCursor(FHitResult& OutHit) const;

	/** Get the SeinActor from a hit result (if any). */
	ASeinActor* GetSeinActorFromHit(const FHitResult& Hit) const;

	/** Update hover state each frame. */
	void UpdateHover();

	/** Log a camera observer command (throttled by CameraLogInterval). */
	void LogCameraUpdate();

	/** Log a selection observer command. */
	void LogSelectionChanged();

	/** Purge stale (dead/destroyed) entries from the selection. */
	void PurgeStaleSelection();

	/** Notify selection visuals and fire delegate. */
	void NotifySelectionUpdated();

public:
	/** Get the world subsystem (cached). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS")
	USeinWorldSubsystem* GetWorldSubsystem() const;

	/** Get the local-player targeter subsystem (per-PC, survives travel).
	 *  Returns null on dedicated server / no local player path. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Targeter")
	USeinTargeterSubsystem* GetTargeterSubsystem() const;

	/**
	 * Resolve the world-space GROUND point under the mouse cursor by intersecting the
	 * cursor ray with the baked level-data height field — the same static-ground surface
	 * the navigation grid is derived from. Unlike a physics trace (TraceUnderCursor), this
	 * is immune to unit and prop meshes: hovering a unit or a tree still returns the ground
	 * XY under the cursor, not the mesh surface. Use this (NOT the selection trace) for
	 * MOVE / order destinations so the point feeds the formation resolver as a clean
	 * nav-ground input (root CLAUDE invariant #6: the destination is the raw ground under
	 * the cursor, resolved to a nav cell ONCE downstream). Falls back to the physics
	 * selection trace only when there is no baked level data under the cursor. Returns
	 * false solely when the cursor is off the world entirely.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Command",
		meta = (DisplayName = "Get Ground Point Under Cursor"))
	bool GetGroundPointUnderCursor(FVector& OutWorld) const;

	/** Read a resource value for this player. Surface for UMG bindings —
	 *  designers drag from a Player Controller pin and find this directly
	 *  in the binding picker rather than navigating UI subsystem internals.
	 *  Returns 0 if the world subsystem is unavailable or the resource isn't
	 *  in the player's catalog.
	 *
	 *  Float for display convenience — sim-side reads should still go through
	 *  USeinResourceBPFL::SeinGetResource for FFixedPoint precision. The
	 *  binding evaluates each frame so the value updates live as the player
	 *  earns / spends. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI",
		meta = (Categories = "SeinARTS.Resource"))
	float GetResource(FGameplayTag ResourceTag) const;

	/** Read this player's resource cap for a given tag. Pairs with GetResource
	 *  for "current / max" displays. Returns 0 if unavailable or not capped. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI",
		meta = (Categories = "SeinARTS.Resource"))
	float GetResourceCap(FGameplayTag ResourceTag) const;

protected:

	// ========== State ==========

	/** Control groups (up to 10). Each stores entity handles for persistence across actor respawns. */
	TArray<FSeinEntityHandle> ControlGroups[10];

	/** Whether LMB is held via IA_Select (as opposed to raw key check). */
	bool bSelectHeld = false;

	/** True when LMB-press was consumed by an active targeter (confirm).
	 *  Checked on LMB-release to skip the default selection-clear logic
	 *  so the targeter-confirm click doesn't also empty the selection. */
	bool bSelectPressConsumedByTargeter = false;

	/** Mirror flag for RMB — true when RMB-press was consumed as a
	 *  targeter-cancel. Skips the default move-to-on-release flow so the
	 *  cancel click doesn't also issue a move command on the unit. */
	bool bCommandPressConsumedByTargeter = false;

	/** Modifier key state. */
	bool bShiftHeld = false;
	bool bCtrlHeld = false;
	bool bAltHeld = false;

	/** Tick counter for camera log throttling. */
	int32 LastCameraLogTick = -1;

	/** Cached camera state for delta-based camera logging. */
	FVector LastLoggedCameraPos = FVector::ZeroVector;
	float LastLoggedCameraYaw = 0.0f;
	float LastLoggedCameraPitch = 0.0f;
	float LastLoggedCameraZoom = 0.0f;

	/** Cached world subsystem pointer. */
	mutable TWeakObjectPtr<USeinWorldSubsystem> CachedWorldSubsystem;

	/** Minimum drag distance (screen pixels) before a click becomes a marquee. */
	float MarqueeDragThreshold = 5.0f;

	/** Minimum drag distance (screen pixels) before RMB becomes a formation drag. */
	float CommandDragThreshold = 10.0f;

	/** Screen-space start of the RMB drag (for threshold check). */
	FVector2D CommandDragScreenStart = FVector2D::ZeroVector;

	/** Whether RMB is currently held. */
	bool bRMBHeld = false;

	/** Actor under cursor when RMB was pressed (for command resolution on release). */
	TWeakObjectPtr<ASeinActor> CommandTargetActor;

	/** Update command drag tracking during tick. */
	void UpdateCommandDrag();
};

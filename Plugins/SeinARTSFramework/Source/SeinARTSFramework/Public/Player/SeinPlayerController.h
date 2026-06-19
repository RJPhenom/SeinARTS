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
class USeinInputConfig;
class USeinWorldSubsystem;
class USeinTargeterSubsystem;
class USeinOrderGesture;
struct FInputActionValue;
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
 * All interaction is render-side. The sim is only touched through
 * USeinWorldSubsystem::EnqueueCommand().
 */
UCLASS(Blueprintable)
class SEINARTSFRAMEWORK_API ASeinPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASeinPlayerController();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupInputComponent() override;

	// ========== Configuration ==========

	/** Input configuration data asset. Must be assigned (e.g., on the CDO or GameMode). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input")
	TObjectPtr<USeinInputConfig> InputConfig;

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
	 *  whole squad (CoH semantics). Returns deduplicated list with nulls
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

protected:
	// ========== Input Handlers ==========

	void OnSelectPressed(const FInputActionValue& Value);
	void OnSelectReleased(const FInputActionValue& Value);
	void OnCommandStarted(const FInputActionValue& Value);
	void OnCommandReleased(const FInputActionValue& Value);
	void OnCameraPan(const FInputActionValue& Value);

	/** Resolve the active order-gesture instance (OrderGestureClass CDO, or the base
	 *  USeinOrderGesture CDO when unset). Never null. */
	USeinOrderGesture* ResolveOrderGesture() const;
	void OnCameraRotate(const FInputActionValue& Value);
	void OnCameraZoom(const FInputActionValue& Value);
	void OnCameraZoomKeyboard(const FInputActionValue& Value);
	void OnCameraFollowPressed(const FInputActionValue& Value);
	void OnCameraResetPressed(const FInputActionValue& Value);
	void OnCameraMMBPan(const FInputActionValue& Value);
	void OnCameraAltRotate(const FInputActionValue& Value);
	void OnCycleFocusPressed(const FInputActionValue& Value);
	void OnPingPressed(const FInputActionValue& Value);
	void OnModifierShift(const FInputActionValue& Value);
	void OnModifierCtrl(const FInputActionValue& Value);
	void OnModifierAlt(const FInputActionValue& Value);
	void OnMenuKeyPressed(const FInputActionValue& Value);

	// Action slot handlers (12 slots for ability/action panel hotkeys)
	void HandleActionSlot(int32 SlotIndex);
	void OnActionSlot0(const FInputActionValue& Value)  { HandleActionSlot(0); }
	void OnActionSlot1(const FInputActionValue& Value)  { HandleActionSlot(1); }
	void OnActionSlot2(const FInputActionValue& Value)  { HandleActionSlot(2); }
	void OnActionSlot3(const FInputActionValue& Value)  { HandleActionSlot(3); }
	void OnActionSlot4(const FInputActionValue& Value)  { HandleActionSlot(4); }
	void OnActionSlot5(const FInputActionValue& Value)  { HandleActionSlot(5); }
	void OnActionSlot6(const FInputActionValue& Value)  { HandleActionSlot(6); }
	void OnActionSlot7(const FInputActionValue& Value)  { HandleActionSlot(7); }
	void OnActionSlot8(const FInputActionValue& Value)  { HandleActionSlot(8); }
	void OnActionSlot9(const FInputActionValue& Value)  { HandleActionSlot(9); }
	void OnActionSlot10(const FInputActionValue& Value) { HandleActionSlot(10); }
	void OnActionSlot11(const FInputActionValue& Value) { HandleActionSlot(11); }

	// Control group handlers (one per group since BindAction requires member function pointers)
	void HandleControlGroup(int32 GroupIndex);
	void OnControlGroup0(const FInputActionValue& Value) { HandleControlGroup(0); }
	void OnControlGroup1(const FInputActionValue& Value) { HandleControlGroup(1); }
	void OnControlGroup2(const FInputActionValue& Value) { HandleControlGroup(2); }
	void OnControlGroup3(const FInputActionValue& Value) { HandleControlGroup(3); }
	void OnControlGroup4(const FInputActionValue& Value) { HandleControlGroup(4); }
	void OnControlGroup5(const FInputActionValue& Value) { HandleControlGroup(5); }
	void OnControlGroup6(const FInputActionValue& Value) { HandleControlGroup(6); }
	void OnControlGroup7(const FInputActionValue& Value) { HandleControlGroup(7); }
	void OnControlGroup8(const FInputActionValue& Value) { HandleControlGroup(8); }
	void OnControlGroup9(const FInputActionValue& Value) { HandleControlGroup(9); }

	// ========== Internal Helpers ==========

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

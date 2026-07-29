/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPlayerController.cpp
 * @brief   RTS player controller implementation.
 */

#include "Player/SeinPlayerController.h"
#include "Player/SeinCameraPawn.h"
#include "Player/SeinTargeterSubsystem.h"
#include "Player/SeinOrderGesture.h"
#include "Input/SeinInputConfig.h"
#include "Actor/SeinActor.h"
#include "Actor/SeinEntityComponent.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinSquadMemberComponent.h"
#include "Abilities/SeinTargeterTypes.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "SeinLevelData.h"
#include "SeinLevelDataSubsystem.h"
#include "Types/Vector.h"
#include "Input/SeinCommand.h"
#include "Lib/SeinResourceBPFL.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "SeinNetSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Net/UnrealNetwork.h"
#include "StructUtils/InstancedStruct.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "DrawDebugHelpers.h"

ASeinPlayerController::ASeinPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	DefaultMouseCursor = EMouseCursor::Default;

	// Load default input config from plugin content if not already set
	static ConstructorHelpers::FObjectFinder<USeinInputConfig> DefaultConfig(
		TEXT("/SeinARTSFramework/Input/DA_SeinDefaultInputConfig"));
	if (DefaultConfig.Succeeded())
	{
		InputConfig = DefaultConfig.Object;
	}
}

void ASeinPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Owner-only: this PC's slot only needs to be known to the controlling
	// client (other clients don't need to know what slot remote players got).
	DOREPLIFETIME_CONDITION(ASeinPlayerController, SeinPlayerID, COND_OwnerOnly);
}

void ASeinPlayerController::SeamlessTravelFrom(APlayerController* OldPC)
{
	Super::SeamlessTravelFrom(OldPC);
	if (const ASeinPlayerController* OldSeinPC = Cast<ASeinPlayerController>(OldPC))
	{
		// Gameplay slots are match identity, not connection/world identity. Keep
		// the exact slot when a destination map selects a different PC class CDO
		// and UE replaces the controller during seamless travel.
		SeinPlayerID = OldSeinPC->SeinPlayerID;
	}
}

void ASeinPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Force input mode to GameAndUI on every PC spawn (incl. post-travel from
	// a UMG menu that called `SetInputModeUIOnly`). Input modes are stored on
	// the LocalPlayer / GameViewportClient — both survive map travel, so the
	// new gameplay-map PC inherits the menu's UIOnly mode unless we reset it
	// here. Without this reset, marquee select / right-click / pings don't
	// reach the world after travelling out of the lobby menu.
	//
	// GameAndUI is the right default for an RTS: cursor visible, mouse hits
	// both world and UI, keyboard goes to game. Project HUDs that need
	// UIOnly transiently (e.g. modal dialogs) can override per-frame.
	if (IsLocalController())
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);
		bShowMouseCursor = true; // re-assert (constructor set it but can drift)
	}

	// Add input mapping context
	if (InputConfig && InputConfig->DefaultMappingContext)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(InputConfig->DefaultMappingContext, InputConfig->DefaultMappingPriority);
		}
	}
}

void ASeinPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateHover();
	UpdateCommandDrag();
	PurgeStaleSelection();
	LogCameraUpdate();

	// Under-cursor physics trace — drives the targeter preview (which may legitimately
	// target unit meshes, so it wants the physics hit, not the ground point).
	FHitResult CursorHit;
	const bool bValidCursorHit = TraceUnderCursor(CursorHit);

	// Targeter cursor update — only when active (subsystem early-outs idle).
	if (USeinTargeterSubsystem* Targeter = GetTargeterSubsystem())
	{
		if (Targeter->IsActive() && bValidCursorHit)
		{
			Targeter->UpdateCursor(CursorHit.ImpactPoint);
		}
	}

	// Cursor delegate — feeds the formation destination preview (subscribed by the
	// preview subsystem). Uses the analytic GROUND point (baked height field) rather
	// than the physics hit, so hovering a unit/prop mesh no longer drags the previewed
	// destination onto the mesh surface (root CLAUDE invariant #6: the preview anchor is
	// the raw nav-ground under the cursor). Subscribers skip on an invalid (off-world)
	// result. No subscribers = no cost — the ground resolve is skipped entirely.
	if (OnCursorUpdated.IsBound())
	{
		FVector CursorGround;
		const bool bValidGround = GetGroundPointUnderCursor(CursorGround);
		OnCursorUpdated.Broadcast(bValidGround ? CursorGround : FVector::ZeroVector, bValidGround);
	}
}

// ==================== Input Setup ====================

void ASeinPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!InputConfig)
	{
		UE_LOG(LogTemp, Warning, TEXT("SeinPlayerController: No InputConfig assigned. Input will not function."));
		return;
	}

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC)
	{
		UE_LOG(LogTemp, Error, TEXT("SeinPlayerController: InputComponent is not UEnhancedInputComponent. Check project input settings."));
		return;
	}

	// Selection
	if (InputConfig->IA_Select)
	{
		EIC->BindAction(InputConfig->IA_Select, ETriggerEvent::Started, this, &ASeinPlayerController::OnSelectPressed);
		EIC->BindAction(InputConfig->IA_Select, ETriggerEvent::Completed, this, &ASeinPlayerController::OnSelectReleased);
	}

	// Command (fire on release for drag order support)
	if (InputConfig->IA_Command)
	{
		EIC->BindAction(InputConfig->IA_Command, ETriggerEvent::Started, this, &ASeinPlayerController::OnCommandStarted);
		EIC->BindAction(InputConfig->IA_Command, ETriggerEvent::Completed, this, &ASeinPlayerController::OnCommandReleased);
	}

	// Camera — keyboard pan (WASD / arrows)
	if (InputConfig->IA_KeyPan)
	{
		EIC->BindAction(InputConfig->IA_KeyPan, ETriggerEvent::Triggered, this, &ASeinPlayerController::OnCameraPan);
	}
	// Camera — keyboard rotate (Q/E)
	if (InputConfig->IA_KeyRotate)
	{
		EIC->BindAction(InputConfig->IA_KeyRotate, ETriggerEvent::Triggered, this, &ASeinPlayerController::OnCameraRotate);
	}
	// Camera — mouse zoom (scroll wheel)
	if (InputConfig->IA_MouseZoom)
	{
		EIC->BindAction(InputConfig->IA_MouseZoom, ETriggerEvent::Triggered, this, &ASeinPlayerController::OnCameraZoom);
	}
	// Camera — keyboard zoom (Z/X)
	if (InputConfig->IA_KeyZoom)
	{
		EIC->BindAction(InputConfig->IA_KeyZoom, ETriggerEvent::Triggered, this, &ASeinPlayerController::OnCameraZoomKeyboard);
	}
	// Camera — follow (F)
	if (InputConfig->IA_FollowCamera)
	{
		EIC->BindAction(InputConfig->IA_FollowCamera, ETriggerEvent::Started, this, &ASeinPlayerController::OnCameraFollowPressed);
	}
	// Camera — reset rotation (Backspace)
	if (InputConfig->IA_ResetCamera)
	{
		EIC->BindAction(InputConfig->IA_ResetCamera, ETriggerEvent::Started, this, &ASeinPlayerController::OnCameraResetPressed);
	}
	// Camera — MMB pan (chorded in mapping context)
	if (InputConfig->IA_MousePan)
	{
		EIC->BindAction(InputConfig->IA_MousePan, ETriggerEvent::Triggered, this, &ASeinPlayerController::OnCameraMMBPan);
	}
	// Camera — Alt+MMB rotate (chorded in mapping context)
	if (InputConfig->IA_MouseRotate)
	{
		EIC->BindAction(InputConfig->IA_MouseRotate, ETriggerEvent::Triggered, this, &ASeinPlayerController::OnCameraAltRotate);
	}

	// Ping (Ctrl+MMB)
	if (InputConfig->IA_Ping)
	{
		EIC->BindAction(InputConfig->IA_Ping, ETriggerEvent::Started, this, &ASeinPlayerController::OnPingPressed);
	}

	// Focus cycling
	if (InputConfig->IA_CycleFocus)
	{
		EIC->BindAction(InputConfig->IA_CycleFocus, ETriggerEvent::Started, this, &ASeinPlayerController::OnCycleFocusPressed);
	}

	// Modifiers
	if (InputConfig->IA_ModifierShift)
	{
		EIC->BindAction(InputConfig->IA_ModifierShift, ETriggerEvent::Started, this, &ASeinPlayerController::OnModifierShift);
		EIC->BindAction(InputConfig->IA_ModifierShift, ETriggerEvent::Completed, this, &ASeinPlayerController::OnModifierShift);
	}
	if (InputConfig->IA_ModifierCtrl)
	{
		EIC->BindAction(InputConfig->IA_ModifierCtrl, ETriggerEvent::Started, this, &ASeinPlayerController::OnModifierCtrl);
		EIC->BindAction(InputConfig->IA_ModifierCtrl, ETriggerEvent::Completed, this, &ASeinPlayerController::OnModifierCtrl);
	}
	if (InputConfig->IA_ModifierAlt)
	{
		EIC->BindAction(InputConfig->IA_ModifierAlt, ETriggerEvent::Started, this, &ASeinPlayerController::OnModifierAlt);
		EIC->BindAction(InputConfig->IA_ModifierAlt, ETriggerEvent::Completed, this, &ASeinPlayerController::OnModifierAlt);
	}

	// Control groups (0-9) — bind to individual handler methods since BindAction requires member function pointers
	using HandlerFn = void (ASeinPlayerController::*)(const FInputActionValue&);
	static const HandlerFn ControlGroupHandlers[10] = {
		&ASeinPlayerController::OnControlGroup0, &ASeinPlayerController::OnControlGroup1,
		&ASeinPlayerController::OnControlGroup2, &ASeinPlayerController::OnControlGroup3,
		&ASeinPlayerController::OnControlGroup4, &ASeinPlayerController::OnControlGroup5,
		&ASeinPlayerController::OnControlGroup6, &ASeinPlayerController::OnControlGroup7,
		&ASeinPlayerController::OnControlGroup8, &ASeinPlayerController::OnControlGroup9,
	};

	for (int32 i = 0; i < InputConfig->IA_ControlGroups.Num() && i < 10; ++i)
	{
		if (InputConfig->IA_ControlGroups[i])
		{
			EIC->BindAction(InputConfig->IA_ControlGroups[i], ETriggerEvent::Started, this, ControlGroupHandlers[i]);
		}
	}

	// Action slot hotkeys (12 slots for ability/action panel)
	using ActionSlotFn = void (ASeinPlayerController::*)(const FInputActionValue&);
	static const ActionSlotFn ActionSlotHandlers[12] = {
		&ASeinPlayerController::OnActionSlot0,  &ASeinPlayerController::OnActionSlot1,
		&ASeinPlayerController::OnActionSlot2,  &ASeinPlayerController::OnActionSlot3,
		&ASeinPlayerController::OnActionSlot4,  &ASeinPlayerController::OnActionSlot5,
		&ASeinPlayerController::OnActionSlot6,  &ASeinPlayerController::OnActionSlot7,
		&ASeinPlayerController::OnActionSlot8,  &ASeinPlayerController::OnActionSlot9,
		&ASeinPlayerController::OnActionSlot10, &ASeinPlayerController::OnActionSlot11,
	};

	for (int32 i = 0; i < InputConfig->IA_ActionSlots.Num() && i < 12; ++i)
	{
		if (InputConfig->IA_ActionSlots[i])
		{
			EIC->BindAction(InputConfig->IA_ActionSlots[i], ETriggerEvent::Started, this, ActionSlotHandlers[i]);
		}
	}

	// Menu / Escape
	if (InputConfig->IA_Menu)
	{
		EIC->BindAction(InputConfig->IA_Menu, ETriggerEvent::Started, this, &ASeinPlayerController::OnMenuKeyPressed);
	}
}

// ==================== Input Handlers ====================

void ASeinPlayerController::OnSelectPressed(const FInputActionValue& Value)
{
	// LMB confirms an active targeter session (left-click places
	// the target). We intercept on press, route to the subsystem, and set a
	// "consumed" flag so the matching release doesn't run the default
	// selection-clear / marquee logic on the same click.
	if (USeinTargeterSubsystem* Targeter = GetTargeterSubsystem())
	{
		if (Targeter->IsActive())
		{
			Targeter->OnConfirmPressed();
			bSelectPressConsumedByTargeter = true;
			return;
		}
	}

	bSelectPressConsumedByTargeter = false;
	bSelectHeld = true;

	// Record start position for potential marquee
	float MouseX, MouseY;
	if (GetMousePosition(MouseX, MouseY))
	{
		MarqueeStart = FVector2D(MouseX, MouseY);
		MarqueeCurrent = MarqueeStart;
		bIsMarqueeDragging = false; // Not dragging yet — wait for threshold
	}
}

void ASeinPlayerController::OnSelectReleased(const FInputActionValue& Value)
{
	// If the press was consumed by a targeter-confirm, route the release
	// to the subsystem too (drag specs use it to capture the drag endpoint;
	// point specs are no-ops on release). Crucially, we then bail BEFORE
	// the default selection-clear logic runs so the same click doesn't
	// also empty the selection.
	if (bSelectPressConsumedByTargeter)
	{
		if (USeinTargeterSubsystem* Targeter = GetTargeterSubsystem())
		{
			Targeter->OnConfirmReleased();
		}
		bSelectPressConsumedByTargeter = false;
		return;
	}

	bSelectHeld = false;

	if (bIsMarqueeDragging)
	{
		// Marquee select — HUD handles the actor collection via ReceiveMarqueeSelection
		bIsMarqueeDragging = false;
		return;
	}

	// Single-click selection
	FHitResult Hit;
	if (!TraceUnderCursor(Hit))
	{
		if (!bShiftHeld && !bCtrlHeld)
		{
			ClearSelection();
		}
		return;
	}

	ASeinActor* HitActor = GetSeinActorFromHit(Hit);
	if (!HitActor)
	{
		if (!bShiftHeld && !bCtrlHeld)
		{
			ClearSelection();
		}
		return;
	}

	// Ownership check — only select own entities
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem();
	if (Subsystem && HitActor->HasValidEntity())
	{
		FSeinPlayerID OwnerID = Subsystem->GetEntityOwner(HitActor->GetEntityHandle());
		if (OwnerID != SeinPlayerID)
		{
			// Clicked an enemy — clear selection (unless modifier held)
			if (!bShiftHeld && !bCtrlHeld)
			{
				ClearSelection();
			}
			return;
		}
	}

	if (bCtrlHeld)
	{
		ToggleSelection(HitActor);
	}
	else if (bShiftHeld)
	{
		AddToSelection({HitActor});
	}
	else
	{
		SetSelection({HitActor});
	}
}

void ASeinPlayerController::OnCommandStarted(const FInputActionValue& Value)
{
	// When the targeter is active, RMB is the cancel input — back out of
	// the targeter session. We mark the press as consumed so the matching
	// release doesn't issue a default move-to on top of the cancel.
	if (USeinTargeterSubsystem* Targeter = GetTargeterSubsystem())
	{
		if (Targeter->IsActive())
		{
			Targeter->OnCancelInput();
			bCommandPressConsumedByTargeter = true;
			return;
		}
	}

	bCommandPressConsumedByTargeter = false;
	bRMBHeld = true;
	bIsCommandDragging = false;

	// Record start screen pos for drag threshold
	float MouseX, MouseY;
	if (GetMousePosition(MouseX, MouseY))
	{
		CommandDragScreenStart = FVector2D(MouseX, MouseY);
	}

	// Record the target actor (physics trace — must hit unit meshes) and the drag
	// anchor (GROUND point — must NOT snap to a hovered mesh) at press time.
	FHitResult Hit;
	CommandTargetActor = TraceUnderCursor(Hit) ? GetSeinActorFromHit(Hit) : nullptr;

	FVector Ground;
	if (GetGroundPointUnderCursor(Ground))
	{
		CommandDragStart = Ground;
		CommandDragCurrent = Ground;
		CommandDragPath.Reset();
		CommandDragPath.Add(Ground);
	}
	else
	{
		CommandDragStart = FVector::ZeroVector;
		CommandDragCurrent = FVector::ZeroVector;
		CommandDragPath.Reset();
	}
}

void ASeinPlayerController::OnCommandReleased(const FInputActionValue& Value)
{
	// If RMB-press was consumed as a targeter cancel, swallow the release
	// too — otherwise the default move-to-on-release fires on the same
	// click and the unit walks where the user just cancelled.
	if (bCommandPressConsumedByTargeter)
	{
		bCommandPressConsumedByTargeter = false;
		return;
	}

	bRMBHeld = false;

	// Final destination = GROUND point under the cursor (not a hovered mesh surface) so
	// the committed order matches the formation preview and lands the units correctly.
	FVector FinalLocation = CommandDragStart;
	ASeinActor* TargetActor = CommandTargetActor.Get();

	FVector Ground;
	if (GetGroundPointUnderCursor(Ground))
	{
		FinalLocation = Ground;
	}

	// If not dragging, refresh the target actor from whatever unit is under the cursor
	// at release time — this needs the physics trace (the ground point ignores meshes).
	if (!bIsCommandDragging)
	{
		FHitResult Hit;
		if (TraceUnderCursor(Hit))
		{
			TargetActor = GetSeinActorFromHit(Hit);
		}
	}

	if (SelectedActors.IsEmpty())
	{
		// Nothing selected — still fire the delegate for feedback (audio cue, etc.)
		OnCommandIssued.Broadcast(FGameplayTag(), FinalLocation);
		bIsCommandDragging = false;
		return;
	}

	// Interpret the gesture (render-side, issuing client only) into the order's guide
	// + nominated formation. The result is baked into the lockstep command, so every
	// client replays the same order. A click yields an empty guide → the resolver's
	// default formation (a blob).
	FSeinOrderGestureInput GestureInput;
	GestureInput.bIsDrag    = bIsCommandDragging;
	GestureInput.StartWorld = CommandDragStart;
	GestureInput.EndWorld   = FinalLocation;
	GestureInput.bShiftHeld = bShiftHeld;
	if (bIsCommandDragging)
	{
		// Terminate the sampled path at the true release point.
		if (CommandDragPath.Num() == 0 || !CommandDragPath.Last().Equals(FinalLocation))
		{
			CommandDragPath.Add(FinalLocation);
		}
		GestureInput.PathWorld = CommandDragPath;
	}

	USeinOrderGesture* Gesture = ResolveOrderGesture();
	const FSeinOrderGestureResult Order = Gesture->BuildOrder(GestureInput);

	// Anchor = the drag MIDPOINT for a drag (formations center on it; the guide still spans
	// start->end), else the click point. Centered anchoring is the general rule for drags.
	const FVector Anchor = bIsCommandDragging ? (CommandDragStart + FinalLocation) * 0.5f : FinalLocation;
	IssueSmartCommandEx(Anchor, TargetActor, bShiftHeld, Order.GuidePoints, Order.FormationTag);

	bIsCommandDragging = false;
}

void ASeinPlayerController::OnCameraPan(const FInputActionValue& Value)
{
	if (ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn()))
	{
		const float Speed = InputConfig ? InputConfig->KeyPanSpeed : 1.0f;
		CamPawn->HandlePanInput(Value.Get<FVector2D>() * Speed);
	}
}

void ASeinPlayerController::OnCameraRotate(const FInputActionValue& Value)
{
	if (ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn()))
	{
		const float Speed = InputConfig ? InputConfig->KeyRotateSpeed : 1.0f;
		CamPawn->HandleRotateInput(Value.Get<float>() * Speed);
	}
}

void ASeinPlayerController::OnCameraZoom(const FInputActionValue& Value)
{
	if (ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn()))
	{
		const float Speed = InputConfig ? InputConfig->MouseZoomSpeed : 1.0f;
		CamPawn->HandleZoomInput(Value.Get<float>() * Speed);
	}
}

void ASeinPlayerController::OnCameraZoomKeyboard(const FInputActionValue& Value)
{
	if (ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn()))
	{
		const float Speed = InputConfig ? InputConfig->KeyZoomSpeed : 1.0f;
		CamPawn->HandleZoomInput(Value.Get<float>() * Speed);
	}
}

void ASeinPlayerController::OnCameraFollowPressed(const FInputActionValue& Value)
{
	ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn());
	if (!CamPawn)
	{
		return;
	}

	if (CamPawn->IsFollowing())
	{
		// Toggle off — stop following
		CamPawn->StopFollowing();
		return;
	}

	// Follow the focused entity, or the first selected entity
	ASeinActor* Target = GetFocusedActor();
	if (!Target && SelectedActors.Num() > 0)
	{
		Target = SelectedActors[0].Get();
	}

	if (Target && Target->HasValidEntity())
	{
		CamPawn->FollowEntity(Target->GetEntityHandle());
	}
}

void ASeinPlayerController::OnCameraResetPressed(const FInputActionValue& Value)
{
	if (ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn()))
	{
		CamPawn->ResetRotation();
	}
}

void ASeinPlayerController::OnCameraMMBPan(const FInputActionValue& Value)
{
	// Block MMB pan while LMB (select) or RMB (command) are held —
	// those use mouse delta for marquee/formation drag, not camera control.
	if (bSelectHeld || bRMBHeld)
	{
		return;
	}

	if (ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn()))
	{
		const float Speed = InputConfig ? InputConfig->MousePanSpeed : 1.0f;
		CamPawn->HandleMMBPanInput(Value.Get<FVector2D>() * Speed);
	}
}

void ASeinPlayerController::OnCameraAltRotate(const FInputActionValue& Value)
{
	// Block orbit while LMB (select) or RMB (command) are held.
	if (bSelectHeld || bRMBHeld)
	{
		return;
	}

	if (ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn()))
	{
		const float Speed = InputConfig ? InputConfig->MouseRotateSpeed : 1.0f;
		// IA_MouseRotate is Axis2D: X = yaw, Y = pitch tilt
		CamPawn->HandleOrbitInput(Value.Get<FVector2D>() * Speed);
	}
}

void ASeinPlayerController::OnPingPressed(const FInputActionValue& Value)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem();
	if (!Subsystem)
	{
		return;
	}

	FHitResult Hit;
	if (!TraceUnderCursor(Hit))
	{
		return;
	}

	const FVector PingLocation = Hit.ImpactPoint;
	ASeinActor* HitActor = GetSeinActorFromHit(Hit);
	const FSeinEntityHandle PingTarget = (HitActor && HitActor->HasValidEntity())
		? HitActor->GetEntityHandle()
		: FSeinEntityHandle::Invalid();

	// Enqueue the sim command. Routed through USeinNetSubsystem so it crosses
	// the lockstep wire to every connected client (Standalone bypass keeps
	// single-player zero-overhead). The server stamps the authoritative
	// PlayerID from the source relay's slot, so we don't strictly need to
	// fill SeinPlayerID, but we do for the local-feedback-before-network case.
	const FFixedVector FixedLocation = FFixedVector::FromVector(PingLocation);
	FSeinCommand Cmd = FSeinCommand::MakePingCommand(SeinPlayerID, FixedLocation, PingTarget);
	Cmd.Tick = Subsystem->GetCurrentTick();

	if (USeinNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<USeinNetSubsystem>() : nullptr)
	{
		Net->SubmitLocalCommand(Cmd);
	}
	else
	{
		Subsystem->SubmitLocalCommandDraft(Cmd);
	}

	// --- Immediate visual feedback (render-side only) ---
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float PingDisplayTime = 3.0f;
	const FColor PingColor = FColor::Magenta;

	// Debug point at ping location
	DrawDebugPoint(World, PingLocation, 10.0f, PingColor, false, PingDisplayTime);

	// Build label
	FString PingLabel;
	if (HitActor)
	{
		PingLabel = FString::Printf(TEXT("Ping %s"), *HitActor->GetActorNameOrLabel());
	}
	else
	{
		PingLabel = FString::Printf(TEXT("Ping %.0f, %.0f, %.0f"),
			PingLocation.X, PingLocation.Y, PingLocation.Z);
	}

	// Debug text above the point
	const FVector TextLocation = PingLocation + FVector(0.0f, 0.0f, 50.0f);
	DrawDebugString(World, TextLocation, PingLabel, nullptr, PingColor, PingDisplayTime, true, 1.5f);
}

void ASeinPlayerController::OnCycleFocusPressed(const FInputActionValue& Value)
{
	CycleFocus();
}

void ASeinPlayerController::OnModifierShift(const FInputActionValue& Value)
{
	bShiftHeld = Value.Get<bool>();
}

void ASeinPlayerController::OnModifierCtrl(const FInputActionValue& Value)
{
	bCtrlHeld = Value.Get<bool>();
}

void ASeinPlayerController::OnModifierAlt(const FInputActionValue& Value)
{
	bAltHeld = Value.Get<bool>();
}

void ASeinPlayerController::HandleControlGroup(int32 GroupIndex)
{
	if (bCtrlHeld)
	{
		AssignControlGroup(GroupIndex);
	}
	else
	{
		RecallControlGroup(GroupIndex);
	}
}

// ==================== Action Slots & Menu ====================

void ASeinPlayerController::HandleActionSlot(int32 SlotIndex)
{
	OnActionSlotPressed.Broadcast(SlotIndex);
}

void ASeinPlayerController::OnMenuKeyPressed(const FInputActionValue& Value)
{
	OnMenuPressed.Broadcast();
}

// ==================== Selection ====================

TArray<ASeinActor*> ASeinPlayerController::ResolveSelectionToSquads(const TArray<ASeinActor*>& Input)
{
	TArray<ASeinActor*> Out;
	Out.Reserve(Input.Num());

	for (ASeinActor* Actor : Input)
	{
		if (!Actor) continue;

		// Default: pass the actor through unchanged.
		ASeinActor* Effective = Actor;

		// Squad-resolution: if this actor's entity is a squad member, swap to
		// the squad's actor. Members are never selectable directly.
		UWorld* World = Actor->GetWorld();
		USeinWorldSubsystem* Sim = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
		if (Sim)
		{
			const FSeinEntityHandle Handle = Actor->GetEntityHandle();
			if (const FSeinSquadMemberComponent* MemberData = Sim->GetComponent<FSeinSquadMemberComponent>(Handle))
			{
				if (MemberData->SquadEntity.IsValid())
				{
					if (USeinActorBridgeSubsystem* Bridge = World->GetSubsystem<USeinActorBridgeSubsystem>())
					{
						if (ASeinActor* SquadActor = Bridge->GetActorForEntity(MemberData->SquadEntity))
						{
							Effective = SquadActor;
						}
						else
						{
							// Squad entity exists but its actor isn't bridged yet (race
							// during spawn). Skip — the member-click is dropped, not the
							// edge case where we'd accidentally select the member.
							continue;
						}
					}
				}
			}
		}

		Out.AddUnique(Effective);
	}

	return Out;
}

void ASeinPlayerController::SetSelection(const TArray<ASeinActor*>& NewSelection)
{
	// Squad-resolve the input — clicking on a member swaps to its squad.
	const TArray<ASeinActor*> Resolved = ResolveSelectionToSquads(NewSelection);

	// Deselect old
	for (const TWeakObjectPtr<ASeinActor>& Weak : SelectedActors)
	{
		if (ASeinActor* Actor = Weak.Get())
		{
			// Selection-visual hooks: subscribe to USeinEntityComponent::OnVisualEvent
			// from a designer-authored render AC if you need per-actor selection
			// state on the unit BP. Framework no longer ships a per-actor
			// selection component.
		}
	}

	// Build new selection
	SelectedActors.Reset();
	for (ASeinActor* Actor : Resolved)
	{
		if (Actor && Actor->HasValidEntity())
		{
			SelectedActors.AddUnique(Actor);
		}
	}

	// Select new
	for (const TWeakObjectPtr<ASeinActor>& Weak : SelectedActors)
	{
		if (ASeinActor* Actor = Weak.Get())
		{
			// Per-actor selection visuals: drop a designer render AC subscribed
			// to USeinEntityComponent::OnVisualEvent if needed.
		}
	}

	// Reset focus to "All"
	ActiveFocusIndex = -1;

	NotifySelectionUpdated();
}

void ASeinPlayerController::AddToSelection(const TArray<ASeinActor*>& ActorsToAdd)
{
	// Squad-resolve before dedup logic.
	const TArray<ASeinActor*> Resolved = ResolveSelectionToSquads(ActorsToAdd);
	for (ASeinActor* Actor : Resolved)
	{
		if (!Actor || !Actor->HasValidEntity())
		{
			continue;
		}

		// Skip duplicates
		bool bAlreadySelected = false;
		for (const TWeakObjectPtr<ASeinActor>& Weak : SelectedActors)
		{
			if (Weak.Get() == Actor)
			{
				bAlreadySelected = true;
				break;
			}
		}

		if (!bAlreadySelected)
		{
			SelectedActors.Add(Actor);
			// Per-actor selection visuals: drop a designer render AC subscribed
			// to USeinEntityComponent::OnVisualEvent if needed.
		}
	}

	NotifySelectionUpdated();
}

void ASeinPlayerController::ToggleSelection(ASeinActor* InActor)
{
	if (!InActor)
	{
		return;
	}

	// Squad-resolve: clicking on a member toggles the squad's selection state.
	TArray<ASeinActor*> Single = { InActor };
	const TArray<ASeinActor*> Resolved = ResolveSelectionToSquads(Single);
	if (Resolved.Num() == 0) return;
	ASeinActor* Actor = Resolved[0];
	if (!Actor)
	{
		return;
	}

	// Check if already selected
	for (int32 i = SelectedActors.Num() - 1; i >= 0; --i)
	{
		if (SelectedActors[i].Get() == Actor)
		{
			// Deselect — selection-visual hooks live on designer render ACs now.
			SelectedActors.RemoveAt(i);

			// Reset focus if it pointed at or past the removed index
			if (ActiveFocusIndex >= SelectedActors.Num())
			{
				ActiveFocusIndex = -1;
			}

			NotifySelectionUpdated();
			return;
		}
	}

	// Not in selection — add it
	AddToSelection({Actor});
}

void ASeinPlayerController::ClearSelection()
{
	for (const TWeakObjectPtr<ASeinActor>& Weak : SelectedActors)
	{
		if (ASeinActor* Actor = Weak.Get())
		{
			// Selection-visual hooks: subscribe to USeinEntityComponent::OnVisualEvent
			// from a designer-authored render AC if you need per-actor selection
			// state on the unit BP. Framework no longer ships a per-actor
			// selection component.
		}
	}

	SelectedActors.Reset();
	ActiveFocusIndex = -1;

	NotifySelectionUpdated();
}

void ASeinPlayerController::CycleFocus()
{
	if (SelectedActors.IsEmpty())
	{
		ActiveFocusIndex = -1;
		return;
	}

	// Cycle: -1 → 0 → 1 → ... → N-1 → -1
	ActiveFocusIndex++;
	if (ActiveFocusIndex >= SelectedActors.Num())
	{
		ActiveFocusIndex = -1;
	}

	// Log focus change as observer command
	LogSelectionChanged();

	OnSelectionChanged.Broadcast();
}

ASeinActor* ASeinPlayerController::GetFocusedActor() const
{
	if (ActiveFocusIndex >= 0 && ActiveFocusIndex < SelectedActors.Num())
	{
		return SelectedActors[ActiveFocusIndex].Get();
	}
	return nullptr;
}

TArray<ASeinActor*> ASeinPlayerController::GetValidSelectedActors()
{
	TArray<ASeinActor*> Valid;
	Valid.Reserve(SelectedActors.Num());

	for (const TWeakObjectPtr<ASeinActor>& Weak : SelectedActors)
	{
		ASeinActor* Actor = Weak.Get();
		if (Actor && Actor->HasValidEntity())
		{
			Valid.Add(Actor);
		}
	}

	return Valid;
}

// ==================== Control Groups ====================

void ASeinPlayerController::AssignControlGroup(int32 GroupIndex)
{
	if (GroupIndex < 0 || GroupIndex >= 10)
	{
		return;
	}

	ControlGroups[GroupIndex].Reset();
	for (const TWeakObjectPtr<ASeinActor>& Weak : SelectedActors)
	{
		if (ASeinActor* Actor = Weak.Get())
		{
			if (Actor->HasValidEntity())
			{
				ControlGroups[GroupIndex].Add(Actor->GetEntityHandle());
			}
		}
	}
}

void ASeinPlayerController::RecallControlGroup(int32 GroupIndex)
{
	if (GroupIndex < 0 || GroupIndex >= 10)
	{
		return;
	}

	USeinWorldSubsystem* Subsystem = GetWorldSubsystem();
	if (!Subsystem)
	{
		return;
	}

	// Resolve entity handles back to actors
	TArray<ASeinActor*> Actors;
	for (const FSeinEntityHandle& Handle : ControlGroups[GroupIndex])
	{
		if (!Subsystem->IsEntityAlive(Handle))
		{
			continue;
		}

		// Find the actor for this handle by iterating world actors
		// (In production, the world subsystem would maintain a handle→actor map)
		for (TActorIterator<ASeinActor> It(GetWorld()); It; ++It)
		{
			if (It->GetEntityHandle() == Handle)
			{
				Actors.Add(*It);
				break;
			}
		}
	}

	if (Actors.Num() > 0)
	{
		SetSelection(Actors);
	}
}

TArray<FSeinEntityHandle> ASeinPlayerController::GetControlGroup(int32 GroupIndex) const
{
	if (GroupIndex < 0 || GroupIndex >= 10)
	{
		return TArray<FSeinEntityHandle>();
	}
	return ControlGroups[GroupIndex];
}

int32 ASeinPlayerController::GetControlGroupCount(int32 GroupIndex) const
{
	if (GroupIndex < 0 || GroupIndex >= 10)
	{
		return 0;
	}
	return ControlGroups[GroupIndex].Num();
}

// ==================== Marquee Selection ====================

void ASeinPlayerController::ReceiveMarqueeSelection(const TArray<ASeinActor*>& ActorsInBox)
{
	// Filter to owned entities
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem();
	TArray<ASeinActor*> OwnedActors;

	for (ASeinActor* Actor : ActorsInBox)
	{
		if (!Actor || !Actor->HasValidEntity())
		{
			continue;
		}

		if (Subsystem)
		{
			FSeinPlayerID OwnerID = Subsystem->GetEntityOwner(Actor->GetEntityHandle());
			if (OwnerID != SeinPlayerID)
			{
				continue;
			}
		}

		OwnedActors.Add(Actor);
	}

	if (bShiftHeld)
	{
		AddToSelection(OwnedActors);
	}
	else
	{
		SetSelection(OwnedActors);
	}
}

// ==================== Command Resolution ====================

FGameplayTagContainer ASeinPlayerController::BuildCommandContext_Implementation(
	ASeinActor* HitActor, const FVector& HitLocation) const
{
	FGameplayTagContainer Context;

	// Base context
	Context.AddTag(SeinARTSTags::Command_Context_RightClick);

	if (!HitActor || !HitActor->HasValidEntity())
	{
		// Ground click
		Context.AddTag(SeinARTSTags::Command_Context_Target_Ground);
		return Context;
	}

	// We have a target actor — determine relationship
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem();
	if (!Subsystem)
	{
		Context.AddTag(SeinARTSTags::Command_Context_Target_Ground);
		return Context;
	}

	const FSeinPlayerID TargetOwner = Subsystem->GetEntityOwner(HitActor->GetEntityHandle());

	if (TargetOwner == SeinPlayerID)
	{
		Context.AddTag(SeinARTSTags::Command_Context_Target_Friendly);
	}
	else if (TargetOwner.IsNeutral())
	{
		// Neutral entities (resources, capture points)
		Context.AddTag(SeinARTSTags::Command_Context_Target_Neutral);
	}
	else
	{
		Context.AddTag(SeinARTSTags::Command_Context_Target_Enemy);
	}

	// Add entity-specific context tags by checking the target's gameplay tags
	// (e.g., if the target has Unit.Building, add Command.Context.Target.Building)
	// This is extensible — designers can add custom tags via the tag component.

	return Context;
}

USeinOrderGesture* ASeinPlayerController::ResolveOrderGesture() const
{
	UClass* Cls = OrderGestureClass.IsNull() ? nullptr : OrderGestureClass.LoadSynchronous();
	if (!Cls || Cls->HasAnyClassFlags(CLASS_Abstract)) { Cls = USeinOrderGesture::StaticClass(); }
	return GetMutableDefault<USeinOrderGesture>(Cls);
}

void ASeinPlayerController::BuildPreviewOrder(FVector CursorWorld, FVector& OutAnchor,
	TArray<FVector>& OutGuidePoints, FGameplayTag& OutFormationTag) const
{
	// Mirror OnCommandReleased's gesture build so the preview matches the commit.
	FSeinOrderGestureInput GestureInput;
	GestureInput.bIsDrag    = bIsCommandDragging;
	GestureInput.StartWorld = bIsCommandDragging ? CommandDragStart : CursorWorld;
	GestureInput.EndWorld   = CursorWorld;
	GestureInput.bShiftHeld = bShiftHeld;
	if (bIsCommandDragging)
	{
		// Copy the live path + terminate at the current cursor (commit terminates at
		// the release point); we must not mutate the PC's CommandDragPath here.
		GestureInput.PathWorld = CommandDragPath;
		if (GestureInput.PathWorld.Num() == 0 || !GestureInput.PathWorld.Last().Equals(CursorWorld))
		{
			GestureInput.PathWorld.Add(CursorWorld);
		}
	}

	const FSeinOrderGestureResult Order = ResolveOrderGesture()->BuildOrder(GestureInput);
	OutAnchor       = bIsCommandDragging ? (CommandDragStart + CursorWorld) * 0.5f : CursorWorld;
	OutGuidePoints  = Order.GuidePoints;
	OutFormationTag = Order.FormationTag;
}

void ASeinPlayerController::IssueSmartCommand(const FVector& WorldLocation, ASeinActor* TargetActor)
{
	// Thin delegate to the Ex form — no queue, no formation guide.
	IssueSmartCommandEx(WorldLocation, TargetActor, /*bQueue=*/false, TArray<FVector>(), FGameplayTag());
}

void ASeinPlayerController::IssueSmartCommandEx(
	const FVector& WorldLocation, ASeinActor* TargetActor, bool bQueue,
	const TArray<FVector>& GuidePoints, FGameplayTag FormationTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem();
	if (!Subsystem)
	{
		return;
	}

	const FGameplayTagContainer Context = BuildCommandContext(TargetActor, WorldLocation);

	// Determine which entities receive commands
	TArray<ASeinActor*> CommandTargets;
	if (ActiveFocusIndex >= 0 && ActiveFocusIndex < SelectedActors.Num())
	{
		if (ASeinActor* Focused = SelectedActors[ActiveFocusIndex].Get())
		{
			if (Focused->HasValidEntity())
			{
				CommandTargets.Add(Focused);
			}
		}
	}
	else
	{
		CommandTargets = GetValidSelectedActors();
	}

	if (CommandTargets.IsEmpty())
	{
		return;
	}

	const FSeinEntityHandle TargetEntityHandle =
		(TargetActor && TargetActor->HasValidEntity())
			? TargetActor->GetEntityHandle()
			: FSeinEntityHandle::Invalid();

	const FFixedVector FixedLocation = FFixedVector::FromVector(WorldLocation);
	TArray<FFixedVector> FixedGuidePoints;
	FixedGuidePoints.Reserve(GuidePoints.Num());
	for (const FVector& GuidePoint : GuidePoints)
	{
		FixedGuidePoints.Add(FFixedVector::FromVector(GuidePoint));
	}

	// ONE unified path (DESIGN §5 line 325 "Wraps even size-1 selections"). The PC
	// emits a single BrokerOrder carrying the raw click context; the broker
	// spawns / reuses + its resolver does per-member smart-resolution sim-side.
	// No more leader/per-entity/single-unit branches — that drift is resolved.
	//
	// Heterogeneous selections keep working because the default resolver's
	// ResolveMemberAbility hook delegates to each member's own DefaultCommands
	// table (sim-side equivalent of the old per-entity loop).
	TArray<FSeinEntityHandle> MemberHandles;
	MemberHandles.Reserve(CommandTargets.Num());
	for (ASeinActor* Actor : CommandTargets)
	{
		if (Actor && Actor->HasValidEntity())
		{
			MemberHandles.Add(Actor->GetEntityHandle());
		}
	}
	if (MemberHandles.Num() == 0)
	{
		return;
	}

	FSeinBrokerOrderPayload Payload;
	Payload.CommandContext = Context;
	Payload.GuidePoints = FixedGuidePoints;
	Payload.FormationTag = FormationTag;

	FSeinCommand BrokerCmd;
	BrokerCmd.PlayerID = SeinPlayerID;
	BrokerCmd.CommandType = SeinARTSTags::Command_Type_BrokerOrder;
	BrokerCmd.TargetEntity = TargetEntityHandle;
	BrokerCmd.TargetLocation = FixedLocation;
	BrokerCmd.EntityList = MemberHandles;
	BrokerCmd.bQueueCommand = bQueue;
	BrokerCmd.Payload = FInstancedStruct::Make(Payload);
	BrokerCmd.Tick = Subsystem->GetCurrentTick();

	// Route through the lockstep wire — every connected client must see this
	// broker order or their sims diverge. Standalone bypass goes direct to
	// the world subsystem; networked path crosses the wire and the server
	// stamps the authoritative slot from the source relay.
	if (USeinNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<USeinNetSubsystem>() : nullptr)
	{
		Net->SubmitLocalCommand(BrokerCmd);
	}
	else
	{
		Subsystem->SubmitLocalCommandDraft(BrokerCmd);
	}

	// OnCommandIssued broadcasts a representative ability tag for VFX/audio —
	// the sim-side resolver picks the actual per-member ability, but UI wants
	// something immediate. Use the leader's resolved tag as a preview hint.
	// Non-critical; drives a render-side ping effect only.
	FGameplayTag PreviewTag;
	if (const FSeinAbilityComponent* LeaderAbilities =
		Subsystem->GetComponent<FSeinAbilityComponent>(MemberHandles[0]))
	{
		PreviewTag = LeaderAbilities->ResolveCommandContext(Context);
	}
	if (PreviewTag.IsValid())
	{
		OnCommandIssued.Broadcast(PreviewTag, WorldLocation);
	}
}

// ==================== Internal Helpers ====================

bool ASeinPlayerController::TraceUnderCursor(FHitResult& OutHit) const
{
	float MouseX, MouseY;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return false;
	}

	FVector WorldOrigin, WorldDirection;
	if (!DeprojectScreenPositionToWorld(MouseX, MouseY, WorldOrigin, WorldDirection))
	{
		return false;
	}

	const FVector TraceEnd = WorldOrigin + WorldDirection * TraceDistance;

	FCollisionQueryParams Params;
	Params.bTraceComplex = false;

	return GetWorld()->LineTraceSingleByChannel(OutHit, WorldOrigin, TraceEnd, SelectionTraceChannel, Params);
}

namespace
{
	// Cursor→ground height-field resolve tuning. Render-side only (float + camera) —
	// never sim state; the committed destination is re-projected deterministically at
	// the command boundary.
	constexpr int32 SeinGroundResolveMaxIterations = 4;
	constexpr float SeinGroundResolveToleranceUU  = 1.0f;   // cm

	/** Intersect a world ray with the horizontal plane Z = PlaneZ. Returns false when
	 *  the ray is parallel to the plane or the crossing is behind the ray origin. */
	static bool SeinIntersectRayGroundPlane(const FVector& Origin, const FVector& Dir,
		float PlaneZ, FVector& OutPoint)
	{
		if (FMath::IsNearlyZero(Dir.Z)) return false;
		const float T = (PlaneZ - Origin.Z) / Dir.Z;
		if (T < 0.f) return false;
		OutPoint = Origin + Dir * T;
		return true;
	}
}

bool ASeinPlayerController::GetGroundPointUnderCursor(FVector& OutWorld) const
{
	// Deproject the cursor to a world ray.
	float MouseX, MouseY;
	if (!GetMousePosition(MouseX, MouseY)) return false;

	FVector Origin, Dir;
	if (!DeprojectScreenPositionToWorld(MouseX, MouseY, Origin, Dir)) return false;

	// Seed the ground altitude from the physics selection trace. Only its Z is used
	// (a hovered unit/prop still stands near ground level); its XY is discarded. This
	// call also doubles as the graceful fallback when there is no baked level data.
	FHitResult SeedHit;
	if (!TraceUnderCursor(SeedHit)) return false;   // cursor off the world entirely

	// Analytic resolve against the baked level-data height field — the same static
	// ground the nav grid is derived from. Because it intersects a height field rather
	// than physics geometry, it is immune to unit/prop meshes: a ray grazing a unit or a
	// tree still lands on the ground XY under the cursor. Iterate intersect-ray-with-plane
	// → sample-height until the Z estimate settles (1–2 steps for an RTS camera).
	if (const USeinLevelData* LevelData = USeinLevelDataSubsystem::GetLevelDataForWorld(this))
	{
		float EstZ = SeedHit.ImpactPoint.Z;
		FVector Resolved = SeedHit.ImpactPoint;
		bool bResolved = false;

		for (int32 Iter = 0; Iter < SeinGroundResolveMaxIterations; ++Iter)
		{
			FVector Candidate;
			if (!SeinIntersectRayGroundPlane(Origin, Dir, EstZ, Candidate)) break;

			FFixedPoint SampledZ;
			if (!LevelData->GetSharedHeightAt(FFixedVector::FromVector(Candidate), SampledZ)) break; // off-grid XY

			const float GroundZ = SampledZ.ToFloat();
			Candidate.Z = GroundZ;
			Resolved = Candidate;
			bResolved = true;

			if (FMath::Abs(GroundZ - EstZ) <= SeinGroundResolveToleranceUU) break;   // settled
			EstZ = GroundZ;
		}

		if (bResolved)
		{
			OutWorld = Resolved;
			return true;
		}
	}

	// No baked ground under the cursor (no level data, or cursor off the baked grid):
	// fall back to the physics hit point — pre-fix behavior, reached only without a bake.
	OutWorld = SeedHit.ImpactPoint;
	return true;
}

ASeinActor* ASeinPlayerController::GetSeinActorFromHit(const FHitResult& Hit) const
{
	if (!Hit.GetActor())
	{
		return nullptr;
	}

	return Cast<ASeinActor>(Hit.GetActor());
}

void ASeinPlayerController::UpdateHover()
{
	FHitResult Hit;
	ASeinActor* NewHovered = nullptr;

	if (TraceUnderCursor(Hit))
	{
		NewHovered = GetSeinActorFromHit(Hit);
	}

	// Update marquee dragging — only when IA_Select actually fired (bSelectHeld),
	// not on raw LMB check (which fires even for Ctrl+LMB / SelectAllOfType).
	if (bSelectHeld)
	{
		float MouseX, MouseY;
		if (GetMousePosition(MouseX, MouseY))
		{
			MarqueeCurrent = FVector2D(MouseX, MouseY);

			if (!bIsMarqueeDragging)
			{
				const float DragDist = FVector2D::Distance(MarqueeStart, MarqueeCurrent);
				if (DragDist > MarqueeDragThreshold)
				{
					bIsMarqueeDragging = true;
				}
			}
		}
	}

	// Update hover state
	ASeinActor* OldHovered = HoveredActor.Get();
	if (NewHovered != OldHovered)
	{
		if (OldHovered)
		{
			// Hover-visual hook: subscribe to a render AC if needed.
		}

		HoveredActor = NewHovered;

		if (NewHovered)
		{
			// Hover-visual hook: subscribe to a render AC if needed.
		}
	}
}

void ASeinPlayerController::UpdateCommandDrag()
{
	if (!bRMBHeld)
	{
		return;
	}

	float MouseX, MouseY;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return;
	}

	// Check drag threshold
	if (!bIsCommandDragging)
	{
		const float DragDist = FVector2D::Distance(CommandDragScreenStart, FVector2D(MouseX, MouseY));
		if (DragDist > CommandDragThreshold)
		{
			bIsCommandDragging = true;
		}
	}

	// Update current drag position in world space
	if (bIsCommandDragging)
	{
		// GROUND point (not the physics hit) so the drag guide tracks the nav ground and
		// never jumps onto a hovered unit/prop mesh mid-drag.
		FVector Ground;
		if (GetGroundPointUnderCursor(Ground))
		{
			CommandDragCurrent = Ground;
			// Accumulate the drag path, sampled by cursor distance travelled so the
			// guide is evenly spaced regardless of drag speed (a slow drag doesn't spam
			// points). The gesture decides whether to use the whole path or endpoints.
			if (CommandDragPath.Num() == 0 ||
				FVector::Dist(CommandDragPath.Last(), CommandDragCurrent) >= CommandDragSampleDistance)
			{
				CommandDragPath.Add(CommandDragCurrent);
			}
		}
	}
}

void ASeinPlayerController::PurgeStaleSelection()
{
	bool bChanged = false;

	for (int32 i = SelectedActors.Num() - 1; i >= 0; --i)
	{
		ASeinActor* Actor = SelectedActors[i].Get();
		if (!Actor || !Actor->HasValidEntity())
		{
			SelectedActors.RemoveAt(i);
			bChanged = true;
		}
	}

	// Control-group cleanup (DESIGN §15). Dead entity handles linger as stale
	// entries in group arrays; drop them deterministically each tick so recall-
	// group and group-count BPFLs return accurate live counts. Check handle
	// validity against the sim pool — generation counters let us detect
	// recycled slots safely.
	if (USeinWorldSubsystem* Sub = GetWorldSubsystem())
	{
		for (int32 Group = 0; Group < 10; ++Group)
		{
			TArray<FSeinEntityHandle>& Handles = ControlGroups[Group];
			const int32 Before = Handles.Num();
			Handles.RemoveAll([Sub](const FSeinEntityHandle& H)
			{
				return !Sub->IsEntityAlive(H);
			});
			(void)Before; // reserved for telemetry if control-group-changed events land later
		}
	}

	if (bChanged)
	{
		// Clamp focus index
		if (ActiveFocusIndex >= SelectedActors.Num())
		{
			ActiveFocusIndex = -1;
		}
		NotifySelectionUpdated();
	}
}

void ASeinPlayerController::LogCameraUpdate()
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem();
	if (!Subsystem || !Subsystem->IsSimulationRunning())
	{
		return;
	}

	const int32 CurrentTick = Subsystem->GetCurrentTick();

	// Throttle: only log every N ticks
	if (CameraLogInterval > 0 && (CurrentTick - LastCameraLogTick) < CameraLogInterval)
	{
		return;
	}

	const ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn());
	if (!CamPawn)
	{
		return;
	}

	const FVector PivotPos = CamPawn->GetPivotLocation();
	const float Yaw = CamPawn->GetCameraYaw();
	const float Pitch = CamPawn->GetCameraPitch();
	const float Zoom = CamPawn->GetCurrentZoomDistance();

	// Skip if nothing changed significantly (position, rotation, or zoom)
	if (LastCameraLogTick >= 0)
	{
		const bool bPosMoved = FVector::DistSquared(PivotPos, LastLoggedCameraPos) > 100.0f;
		const bool bYawChanged = FMath::Abs(Yaw - LastLoggedCameraYaw) > 0.5f;
		const bool bPitchChanged = FMath::Abs(Pitch - LastLoggedCameraPitch) > 0.5f;
		const bool bZoomChanged = FMath::Abs(Zoom - LastLoggedCameraZoom) > 5.0f;

		if (!bPosMoved && !bYawChanged && !bPitchChanged && !bZoomChanged)
		{
			return;
		}
	}

	FSeinCommand Cmd = FSeinCommand::MakeCameraUpdateCommand(
		SeinPlayerID,
		FFixedVector::FromVector(PivotPos),
		FFixedPoint::FromFloat(Yaw),
		FFixedPoint::FromFloat(Zoom)
	);
	// Store pitch in AuxLocation.X (unused for CameraUpdate commands)
	Cmd.AuxLocation = FFixedVector(FFixedPoint::FromFloat(Pitch), FFixedPoint::Zero, FFixedPoint::Zero);
	Cmd.Tick = CurrentTick;

	// Route through the lockstep wire — observer commands (CameraUpdate /
	// SelectionChanged) are sim-skip per `IsObserverCommand()` so they don't
	// affect state hash, but propagating them through the per-turn stream
	// powers two features for free: live spectator/POV-switch (a
	// delayed observer mode reads the same wire) and complete
	// replays (server's ReplayWriter naturally captures all observer
	// streams alongside sim-mutating commands). Bandwidth cost is small —
	// CameraUpdate is throttled to CameraLogInterval ticks + skip-if-static,
	// SelectionChanged only fires on actual change. Standalone bypass keeps
	// single-player zero-overhead.
	if (USeinNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<USeinNetSubsystem>() : nullptr)
	{
		Net->SubmitLocalCommand(Cmd);
	}
	else
	{
		Subsystem->SubmitLocalCommandDraft(Cmd);
	}

	LastCameraLogTick = CurrentTick;
	LastLoggedCameraPos = PivotPos;
	LastLoggedCameraYaw = Yaw;
	LastLoggedCameraPitch = Pitch;
	LastLoggedCameraZoom = Zoom;
}

void ASeinPlayerController::LogSelectionChanged()
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem();
	if (!Subsystem || !Subsystem->IsSimulationRunning())
	{
		return;
	}

	// Build entity handle list from current selection
	TArray<FSeinEntityHandle> Handles;
	Handles.Reserve(SelectedActors.Num());
	for (const TWeakObjectPtr<ASeinActor>& Weak : SelectedActors)
	{
		if (ASeinActor* Actor = Weak.Get())
		{
			if (Actor->HasValidEntity())
			{
				Handles.Add(Actor->GetEntityHandle());
			}
		}
	}

	const FSeinCommand Cmd = FSeinCommand::MakeSelectionChangedCommand(SeinPlayerID, Handles, ActiveFocusIndex);
	// Route through the wire same as CameraUpdate (see comment above) —
	// powers spectator-mode selection-tracking + replay POV-switching.
	if (USeinNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<USeinNetSubsystem>() : nullptr)
	{
		Net->SubmitLocalCommand(Cmd);
	}
	else
	{
		Subsystem->SubmitLocalCommandDraft(Cmd);
	}
}

void ASeinPlayerController::NotifySelectionUpdated()
{
	LogSelectionChanged();
	OnSelectionChanged.Broadcast();
}

USeinWorldSubsystem* ASeinPlayerController::GetWorldSubsystem() const
{
	if (CachedWorldSubsystem.IsValid())
	{
		return CachedWorldSubsystem.Get();
	}

	if (UWorld* World = GetWorld())
	{
		USeinWorldSubsystem* Subsystem = World->GetSubsystem<USeinWorldSubsystem>();
		CachedWorldSubsystem = Subsystem;
		return Subsystem;
	}

	return nullptr;
}

USeinTargeterSubsystem* ASeinPlayerController::GetTargeterSubsystem() const
{
	if (const ULocalPlayer* LP = GetLocalPlayer())
	{
		return LP->GetSubsystem<USeinTargeterSubsystem>();
	}
	return nullptr;
}

float ASeinPlayerController::GetResource(FGameplayTag ResourceTag) const
{
	if (USeinWorldSubsystem* World = GetWorldSubsystem())
	{
		// SeinResourceBPFL keeps the framework's deterministic FFixedPoint
		// math; we convert to float at the UI binding boundary.
		return USeinResourceBPFL::SeinGetResource(World, SeinPlayerID, ResourceTag).ToFloat();
	}
	return 0.0f;
}

float ASeinPlayerController::GetResourceCap(FGameplayTag ResourceTag) const
{
	if (USeinWorldSubsystem* World = GetWorldSubsystem())
	{
		return USeinResourceBPFL::SeinGetResourceCap(World, SeinPlayerID, ResourceTag).ToFloat();
	}
	return 0.0f;
}

void ASeinPlayerController::IssueTargetedAbility(FGameplayTag AbilityTag,
	FSeinEntityHandle OwnerLeader, const TArray<FSeinTargeterPoint>& Points)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem();
	if (!Subsystem || !AbilityTag.IsValid())
	{
		return;
	}

	// Empty Points is a legal "fire immediately" case for Self / None / Passive
	// abilities — they don't need target placement, so the targeter was bypassed
	// and the action panel called us directly. Ability's OnActivate uses its
	// own state (owner position, etc.) and ignores TargetLocation.

	// Build the broker-order payload. Predetermined ability tag tells the
	// resolver to skip per-member context resolution and dispatch this exact
	// ability to the first capable member. Context container also carries the
	// AbilityTriggered tag for future resolver subclasses that want to vary
	// behavior based on origin (e.g. play a different VO line for player-
	// triggered vs auto-triggered abilities).
	FGameplayTagContainer Context;
	Context.AddTag(SeinARTSTags::Command_Context_AbilityTriggered);

	FSeinBrokerOrderPayload Payload;
	Payload.CommandContext = Context;
	Payload.PredeterminedAbilityTag = AbilityTag;
	Payload.TargeterPoints = Points;
	// FormationEnd left zero — Phase 1 PointTargeterSpec doesn't drag.

	// Build the member list: focused unit if focused, else full selection. The
	// targeter passed OwnerLeader as the activation target, but the broker is
	// member-aware (it cascades to all selected even if only one was the
	// "leader" for activation). This matches the IssueSmartCommandEx member-
	// gathering behavior so right-click and targeter give consistent crews.
	TArray<FSeinEntityHandle> MemberHandles;
	if (ActiveFocusIndex >= 0 && ActiveFocusIndex < SelectedActors.Num())
	{
		if (ASeinActor* Focused = SelectedActors[ActiveFocusIndex].Get())
		{
			if (Focused->HasValidEntity())
			{
				MemberHandles.Add(Focused->GetEntityHandle());
			}
		}
	}
	else
	{
		for (const TWeakObjectPtr<ASeinActor>& Weak : SelectedActors)
		{
			if (ASeinActor* A = Weak.Get())
			{
				if (A->HasValidEntity())
				{
					MemberHandles.Add(A->GetEntityHandle());
				}
			}
		}
	}

	// Belt-and-suspenders: ensure OwnerLeader is in the member list so the
	// resolver's first-capable-member walk reaches them even if selection
	// changed between targeter activation and confirm.
	if (OwnerLeader.IsValid() && !MemberHandles.Contains(OwnerLeader))
	{
		MemberHandles.Add(OwnerLeader);
	}
	if (MemberHandles.Num() == 0)
	{
		return;
	}

	const FFixedVector PrimaryLocation = Points.Num() > 0 ? Points[0].Location : FFixedVector();

	FSeinCommand BrokerCmd;
	BrokerCmd.PlayerID = SeinPlayerID;
	BrokerCmd.CommandType = SeinARTSTags::Command_Type_BrokerOrder;
	BrokerCmd.TargetEntity = FSeinEntityHandle::Invalid();
	BrokerCmd.TargetLocation = PrimaryLocation;
	BrokerCmd.EntityList = MemberHandles;
	BrokerCmd.bQueueCommand = bShiftHeld;
	BrokerCmd.Payload = FInstancedStruct::Make(Payload);
	BrokerCmd.Tick = Subsystem->GetCurrentTick();

	// Same lockstep-or-direct fork as IssueSmartCommandEx — networked games go
	// through the wire so every peer sees the order; standalone bypasses to
	// the world subsystem directly.
	if (USeinNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<USeinNetSubsystem>() : nullptr)
	{
		Net->SubmitLocalCommand(BrokerCmd);
	}
	else
	{
		Subsystem->SubmitLocalCommandDraft(BrokerCmd);
	}

	// Render-side feedback hook — the same delegate right-click uses. UI can
	// listen for command-issued audio/VFX without caring whether the trigger
	// came from a targeter or a smart command.
	OnCommandIssued.Broadcast(AbilityTag, PrimaryLocation.ToVector());
}

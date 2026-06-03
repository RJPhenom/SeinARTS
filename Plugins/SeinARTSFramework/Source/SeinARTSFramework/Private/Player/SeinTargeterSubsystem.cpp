/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTargeterSubsystem.cpp
 * @brief   Targeter state machine implementation. Phase 1 supports
 *          USeinPointTargeterSpec — single-click point capture, optional
 *          multi-target loop (TargetCount > 1).
 */

#include "Player/SeinTargeterSubsystem.h"
#include "Player/SeinPlayerController.h"
#include "Targeter/SeinTargeterPreview.h"
#include "Targeter/SeinPointTargeterPreview.h"
#include "Targeter/SeinPointFacingTargeterPreview.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Types/Vector.h"
#include "Math/UnrealMathUtility.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinTargeter, Log, All);

void USeinTargeterSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogSeinTargeter, Verbose, TEXT("USeinTargeterSubsystem initialized"));
}

void USeinTargeterSubsystem::Deinitialize()
{
	ResetToIdle();
	Super::Deinitialize();
}

void USeinTargeterSubsystem::Activate(USeinTargeterSpec* InSpec, FGameplayTag InAbilityTag,
	FSeinEntityHandle InOwnerLeader, float InAreaRadiusWorld)
{
	if (!InSpec)
	{
		UE_LOG(LogSeinTargeter, Warning,
			TEXT("Activate called with null spec for ability %s — ignoring."),
			*InAbilityTag.ToString());
		return;
	}
	if (!InAbilityTag.IsValid())
	{
		UE_LOG(LogSeinTargeter, Warning, TEXT("Activate called with invalid ability tag — ignoring."));
		return;
	}

	// Cancel any prior session — switching abilities mid-targeter is allowed
	// (player clicked a different action slot). Cleanly tears down the previous
	// preview before starting fresh.
	if (State != ESeinTargeterState::Idle)
	{
		UE_LOG(LogSeinTargeter, Verbose,
			TEXT("Activate replacing prior session (was capturing for %s, now %s)"),
			*PendingAbilityTag.ToString(), *InAbilityTag.ToString());
		ResetToIdle();
	}

	Spec = InSpec;
	PendingAbilityTag = InAbilityTag;
	PendingOwnerLeader = InOwnerLeader;
	PendingAreaRadiusWorld = InAreaRadiusWorld;
	CapturedPoints.Reset();
	CapturedPoints.Reserve(InSpec->TargetCount);

	// Spawn the preview actor. Spec gets first say (ResolvePreviewClass
	// returns its PreviewClass override or its declared default); fall back
	// to the framework's Phase 1 default ASeinPointTargeterPreview when both
	// are null. Spawn at the cursor's last known position so there's no
	// one-frame flash at origin.
	UWorld* World = GetWorld();
	if (World)
	{
		// Resolve soft class path to UClass*. Sync load is fine — preview classes
		// are tiny and the player just clicked an action slot expecting
		// instant feedback. UE caches the load. FSoftClassPath::TryLoadClass
		// returns null on missing class without warning logs.
		const FSoftClassPath SoftPreview = Spec->ResolvePreviewClass();
		UClass* PreviewClass = SoftPreview.IsValid()
			? SoftPreview.TryLoadClass<ASeinTargeterPreview>()
			: nullptr;
		if (!PreviewClass)
		{
			// Fallback to framework default when neither the spec instance nor
			// its subclass declared a preview. PointTargeterSpec ships with no
			// CoreEntity-side default to keep CoreEntity render-agnostic; the
			// fallback lives here in the framework module where the preview
			// class is reachable.
			PreviewClass = ASeinPointTargeterPreview::StaticClass();
		}

		// Default fallback for drag specs picks the point-facing preview when
		// nothing else is set, mirroring how PointTargeterSpec falls back to
		// ASeinPointTargeterPreview. Both fallbacks are framework-side because
		// CoreEntity can't reference these AActor subclasses directly.
		if (PreviewClass == ASeinPointTargeterPreview::StaticClass()
			&& Spec->IsA<USeinPointFacingTargeterSpec>())
		{
			PreviewClass = ASeinPointFacingTargeterPreview::StaticClass();
		}

		// SpawnActorDeferred (NOT SpawnActor) so we can call InitializePreview
		// to wire up Spec + AreaRadius BEFORE BeginPlay fires. With normal
		// SpawnActor, BeginPlay runs synchronously during the spawn call —
		// meaning the preview's BeginPlay logic that resolves mesh / decal size
		// from the Spec sees Spec=null (it's set on the line AFTER SpawnActor
		// returns). This silently misrenders the smoke decal at the default
		// fallback radius and silently shows no hologram for building placement
		// (no fallback to mask the bug). The deferred-spawn pattern guarantees
		// InitializePreview runs first, then FinishSpawning triggers BeginPlay
		// with Spec already set.
		const FTransform SpawnTransform(FRotator::ZeroRotator, LastCursorWorld);
		Preview = World->SpawnActorDeferred<ASeinTargeterPreview>(
			PreviewClass, SpawnTransform, /*Owner*/ nullptr, /*Instigator*/ nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Preview)
		{
			Preview->SetFlags(RF_Transient);
			Preview->InitializePreview(Spec, PendingAreaRadiusWorld);
			Preview->FinishSpawning(SpawnTransform);
		}
		else
		{
			UE_LOG(LogSeinTargeter, Warning,
				TEXT("Failed to spawn preview actor of class %s for ability %s"),
				*GetNameSafe(PreviewClass), *InAbilityTag.ToString());
		}
	}

	SetState(ESeinTargeterState::WaitingForCapture);
	UE_LOG(LogSeinTargeter, Verbose, TEXT("Activated targeter for %s (TargetCount=%d, AreaRadius=%.1f)"),
		*InAbilityTag.ToString(), Spec->TargetCount, InAreaRadiusWorld);
}

void USeinTargeterSubsystem::Cancel()
{
	if (State == ESeinTargeterState::Idle) return;
	UE_LOG(LogSeinTargeter, Verbose, TEXT("Cancelled targeter for %s after capturing %d/%d"),
		*PendingAbilityTag.ToString(), CapturedPoints.Num(),
		Spec ? Spec->TargetCount : 0);
	ResetToIdle();
}

void USeinTargeterSubsystem::OnConfirmPressed()
{
	if (State != ESeinTargeterState::WaitingForCapture) return;
	if (!Spec) { ResetToIdle(); return; }

	// Drag specs (Point + Facing, Line) lock the anchor on press and capture
	// on release. Non-drag specs (basic Point) capture immediately on press.
	if (IsDragSpec())
	{
		// Strict-click block check at press time — if the anchor cell is
		// already invalid (e.g. building can't even start placement here),
		// reject the press without entering Dragging. The cycle stays in
		// WaitingForCapture for the next attempt.
		if (Spec->bRejectClickWhenBlocked)
		{
			const FFixedVector CursorFixed = ToFixed(LastCursorWorld);
			const FFixedVector AuxFixed;
			const ESeinTargeterValidity Validity = Spec->ValidateClient(CursorFixed, AuxFixed);
			if (Validity == ESeinTargeterValidity::Blocked)
			{
				UE_LOG(LogSeinTargeter, Verbose,
					TEXT("Drag-press rejected (Blocked anchor) for %s; targeter stays active."),
					*PendingAbilityTag.ToString());
				return;
			}
		}

		DragAnchorWorld = LastCursorWorld;
		SnappedYawDegrees = 0.0f;
		SnappedStepIndex = 0;
		SetState(ESeinTargeterState::Dragging);
		UE_LOG(LogSeinTargeter, Verbose,
			TEXT("Drag started for %s at anchor (%.1f, %.1f, %.1f)"),
			*PendingAbilityTag.ToString(),
			DragAnchorWorld.X, DragAnchorWorld.Y, DragAnchorWorld.Z);
		return;
	}

	// Non-drag spec — same behavior as before: optional strict-click block,
	// then immediate capture.
	if (Spec->bRejectClickWhenBlocked)
	{
		const FFixedVector CursorFixed = ToFixed(LastCursorWorld);
		const FFixedVector AuxFixed;
		const ESeinTargeterValidity Validity = Spec->ValidateClient(CursorFixed, AuxFixed);
		if (Validity == ESeinTargeterValidity::Blocked)
		{
			UE_LOG(LogSeinTargeter, Verbose,
				TEXT("Confirm rejected (Blocked) for %s; targeter stays active."),
				*PendingAbilityTag.ToString());
			return;
		}
	}

	CapturePoint();
}

void USeinTargeterSubsystem::OnConfirmReleased()
{
	if (State != ESeinTargeterState::Dragging) return;
	if (!Spec) { ResetToIdle(); return; }

	// Validate the drag-end placement at release time — anchor was passed at
	// press, but a slow drag could now point at a Blocked direction (e.g.
	// the rotated footprint extends into a wall). Honor the spec's strict
	// click-blocking flag here too.
	if (Spec->bRejectClickWhenBlocked)
	{
		const FFixedVector AnchorFixed = ToFixed(DragAnchorWorld);
		const FFixedVector CursorFixed = ToFixed(LastCursorWorld);
		const ESeinTargeterValidity Validity = Spec->ValidateClient(AnchorFixed, CursorFixed);
		if (Validity == ESeinTargeterValidity::Blocked)
		{
			// Rejected — return to WaitingForCapture so the player can retry.
			// Anchor is cleared; the next press starts a new drag.
			UE_LOG(LogSeinTargeter, Verbose,
				TEXT("Drag-release rejected (Blocked) for %s; resetting drag."),
				*PendingAbilityTag.ToString());
			DragAnchorWorld = FVector::ZeroVector;
			SnappedYawDegrees = 0.0f;
			SnappedStepIndex = 0;
			SetState(ESeinTargeterState::WaitingForCapture);
			return;
		}
	}

	CaptureDragPoint();
}

void USeinTargeterSubsystem::OnCancelInput()
{
	if (State == ESeinTargeterState::Idle) return;
	Cancel();
}

void USeinTargeterSubsystem::UpdateCursor(const FVector& CursorWorld)
{
	LastCursorWorld = CursorWorld;
	if (!Preview || !Spec) return;

	// Drag state recomputes rotation each tick from anchor → cursor; preview
	// reads the snapped yaw via the DragAnchor pushed below.
	if (State == ESeinTargeterState::Dragging)
	{
		ComputeDragRotation(SnappedYawDegrees, SnappedStepIndex);
	}

	// Validation context differs by state:
	//   - WaitingForCapture: validate at the cursor position (anchor candidate)
	//   - Dragging: validate at the locked anchor with cursor as drag end
	const FFixedVector PrimaryFixed = (State == ESeinTargeterState::Dragging)
		? ToFixed(DragAnchorWorld) : ToFixed(CursorWorld);
	const FFixedVector AuxFixed = (State == ESeinTargeterState::Dragging)
		? ToFixed(CursorWorld) : FFixedVector();
	const ESeinTargeterValidity Validity = Spec->ValidateClient(PrimaryFixed, AuxFixed);

	const FVector AnchorForPreview = State == ESeinTargeterState::Dragging
		? DragAnchorWorld : FVector::ZeroVector;
	Preview->UpdatePreview(CursorWorld, AnchorForPreview, Validity, SnappedYawDegrees);
}

void USeinTargeterSubsystem::CapturePoint()
{
	if (!Spec) { ResetToIdle(); return; }

	FSeinTargeterPoint Point;
	Point.Location = ToFixed(LastCursorWorld);
	// Phase 1 PointTargeterSpec leaves AuxLocation + RotationStep at defaults.
	CapturedPoints.Add(Point);

	UE_LOG(LogSeinTargeter, Verbose,
		TEXT("Captured point %d/%d for %s at (%.1f, %.1f, %.1f)"),
		CapturedPoints.Num(), Spec->TargetCount, *PendingAbilityTag.ToString(),
		LastCursorWorld.X, LastCursorWorld.Y, LastCursorWorld.Z);

	if (CapturedPoints.Num() >= Spec->TargetCount)
	{
		Submit();
	}
}

void USeinTargeterSubsystem::CaptureDragPoint()
{
	if (!Spec) { ResetToIdle(); return; }

	// Recompute rotation one final time at release in case UpdateCursor was
	// not called in the same tick. Cheap; ensures the captured RotationStep
	// matches what the preview was showing.
	ComputeDragRotation(SnappedYawDegrees, SnappedStepIndex);

	FSeinTargeterPoint Point;
	Point.Location = ToFixed(DragAnchorWorld);
	Point.AuxLocation = ToFixed(LastCursorWorld);
	Point.RotationStep = SnappedStepIndex;
	Point.YawDegrees = FFixedPoint::FromFloat(SnappedYawDegrees);
	CapturedPoints.Add(Point);

	UE_LOG(LogSeinTargeter, Verbose,
		TEXT("Captured drag point %d/%d for %s at anchor (%.1f, %.1f, %.1f) yaw=%.1f° step=%d"),
		CapturedPoints.Num(), Spec->TargetCount, *PendingAbilityTag.ToString(),
		DragAnchorWorld.X, DragAnchorWorld.Y, DragAnchorWorld.Z,
		SnappedYawDegrees, SnappedStepIndex);

	// Reset drag state for the next cycle (in multi-target abilities) — anchor
	// must clear so the next press starts fresh, regardless of whether we
	// transition back to WaitingForCapture or directly to Submit.
	DragAnchorWorld = FVector::ZeroVector;
	SnappedYawDegrees = 0.0f;
	SnappedStepIndex = 0;

	if (CapturedPoints.Num() >= Spec->TargetCount)
	{
		Submit();
	}
	else
	{
		// More cycles to go — back to waiting for the next press.
		SetState(ESeinTargeterState::WaitingForCapture);
	}
}

void USeinTargeterSubsystem::ComputeDragRotation(float& OutSnappedYawDegrees, uint8& OutStepIndex) const
{
	OutSnappedYawDegrees = 0.0f;
	OutStepIndex = 0;

	const USeinPointFacingTargeterSpec* PFSpec = Cast<USeinPointFacingTargeterSpec>(Spec);
	if (!PFSpec) return;

	const FVector ToCursor = LastCursorWorld - DragAnchorWorld;
	if (ToCursor.IsNearlyZero())
	{
		// Cursor still at anchor — rotation undefined; keep zero. Avoids a
		// jitter when the player presses without dragging at all.
		return;
	}

	// Yaw of direction vector in degrees. UE FRotator returns yaw in [-180, 180];
	// normalize to [0, 360) for clean step indexing + uniform downstream usage.
	float RawYaw = ToCursor.Rotation().Yaw;
	if (RawYaw < 0.0f) RawYaw += 360.0f;

	// RotationStepDegrees == 0 → free rotation, no snap. StepIndex stays 0;
	// captured yaw is the raw cursor-direction yaw. Hologram preview reads
	// this same value, so visual + capture stay in lockstep.
	const int32 StepDeg = PFSpec->RotationStepDegrees;
	if (StepDeg <= 0)
	{
		OutSnappedYawDegrees = RawYaw;
		OutStepIndex = 0;
		return;
	}

	// Snapped path: round to nearest step.
	const int32 StepCount = FMath::Max(1, 360 / StepDeg);
	const float StepIndexFloat = RawYaw / static_cast<float>(StepDeg);
	const int32 StepIndex = FMath::RoundToInt(StepIndexFloat) % StepCount;

	OutStepIndex = static_cast<uint8>(StepIndex);
	OutSnappedYawDegrees = static_cast<float>(StepIndex * StepDeg);
}

bool USeinTargeterSubsystem::IsDragSpec() const
{
	// Phase 3: only USeinPointFacingTargeterSpec is a drag spec. Phase 4's
	// USeinLineTargeterSpec will be added here when it lands.
	return Spec && Spec->IsA<USeinPointFacingTargeterSpec>();
}

void USeinTargeterSubsystem::Submit()
{
	ASeinPlayerController* PC = GetPlayerController();
	if (!PC)
	{
		UE_LOG(LogSeinTargeter, Warning, TEXT("Submit: no PC available, dropping captured points."));
		ResetToIdle();
		return;
	}

	UE_LOG(LogSeinTargeter, Verbose,
		TEXT("Submitting %d points for ability %s (owner=%s)"),
		CapturedPoints.Num(), *PendingAbilityTag.ToString(),
		*PendingOwnerLeader.ToString());

	// Hand off to the PC, which packs the broker order command + submits via
	// the lockstep wire. PC method declared in SeinPlayerController.h.
	PC->IssueTargetedAbility(PendingAbilityTag, PendingOwnerLeader, CapturedPoints);

	ResetToIdle();
}

ASeinPlayerController* USeinTargeterSubsystem::GetPlayerController() const
{
	if (CachedPC.IsValid()) return CachedPC.Get();

	const ULocalPlayer* LP = GetLocalPlayer();
	if (!LP) return nullptr;
	APlayerController* GenericPC = LP->GetPlayerController(GetWorld());
	ASeinPlayerController* SeinPC = Cast<ASeinPlayerController>(GenericPC);
	if (SeinPC)
	{
		CachedPC = SeinPC;
	}
	return SeinPC;
}

void USeinTargeterSubsystem::SetState(ESeinTargeterState NewState)
{
	if (State == NewState) return;
	State = NewState;
	OnStateChanged.Broadcast(NewState);
}

void USeinTargeterSubsystem::ResetToIdle()
{
	if (Preview)
	{
		Preview->Destroy();
		Preview = nullptr;
	}
	Spec = nullptr;
	PendingAbilityTag = FGameplayTag();
	PendingOwnerLeader = FSeinEntityHandle::Invalid();
	PendingAreaRadiusWorld = 0.0f;
	CapturedPoints.Reset();
	DragAnchorWorld = FVector::ZeroVector;
	SnappedYawDegrees = 0.0f;
	SnappedStepIndex = 0;
	SetState(ESeinTargeterState::Idle);
}

FFixedVector USeinTargeterSubsystem::ToFixed(const FVector& World)
{
	return FFixedVector::FromVector(World);
}

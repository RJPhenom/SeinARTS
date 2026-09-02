/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCameraPawn.cpp
 * @brief   RTS camera pawn implementation.
 */

#include "Player/SeinCameraPawn.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Actor/SeinActor.h"
#include "Actor/SeinEntityBridgeComponent.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Data/SeinCameraSnapshotData.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

ASeinCameraPawn::ASeinCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// Pivot (root) — moves on the ground plane
	CameraPivot = CreateDefaultSubobject<USceneComponent>(TEXT("CameraPivot"));
	SetRootComponent(CameraPivot);

	// Spring arm — provides zoom distance and pitch
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(CameraPivot);
	SpringArm->bDoCollisionTest = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 15.0f;
	SpringArm->TargetArmLength = DefaultZoomDistance;
	SpringArm->SetRelativeRotation(FRotator(CameraPitch, 0.0f, 0.0f));

	// Camera
	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	CameraComponent->SetupAttachment(SpringArm);
}

void ASeinCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	TargetZoomDistance = DefaultZoomDistance;
	CurrentPitch = CameraPitch;
	SpringArm->TargetArmLength = DefaultZoomDistance;
	SpringArm->SetRelativeRotation(FRotator(CurrentPitch, 0.0f, 0.0f));

	// Start grounded so the camera doesn't begin floating at the spawn Z over uneven terrain.
	UpdateGroundFollow(0.0f, /*bSnapImmediate*/ true);
}

void ASeinCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Follow target takes priority over manual pan
	if (FollowTarget.IsValid())
	{
		UpdateFollowTarget(DeltaSeconds);
	}
	else
	{
		// Edge scroll
		if (bEnableEdgeScroll)
		{
			UpdateEdgeScroll(DeltaSeconds);
		}

		// Apply accumulated pan input (from WASD + edge scroll)
		if (!PendingPanInput.IsNearlyZero())
		{
			// Pan relative to camera yaw
			const float Yaw = CameraPivot->GetComponentRotation().Yaw;
			const FRotator YawRotation(0.0f, Yaw, 0.0f);
			const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
			const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

			const FVector PanDelta = (Forward * PendingPanInput.Y + Right * PendingPanInput.X) * PanSpeed * DeltaSeconds;
			CameraPivot->AddWorldOffset(PanDelta);
		}
	}

	PendingPanInput = FVector2D::ZeroVector;

	// Interpolate zoom
	const float CurrentArm = SpringArm->TargetArmLength;
	if (!FMath::IsNearlyEqual(CurrentArm, TargetZoomDistance, 1.0f))
	{
		SpringArm->TargetArmLength = FMath::FInterpTo(CurrentArm, TargetZoomDistance, DeltaSeconds, ZoomInterpSpeed);
	}

	// Clamp to bounds
	ClampToBounds();

	// Ground-follow: ease the pivot Z onto the terrain at its (final, clamped) XY. Handles both
	// smooth terrain tracking while panning and the eased altitude transition after a recenter snap.
	UpdateGroundFollow(DeltaSeconds);
}

// ==================== Input Handlers ====================

void ASeinCameraPawn::HandlePanInput(const FVector2D& AxisValue)
{
	if (!AxisValue.IsNearlyZero())
	{
		// Any manual input cancels follow
		if (FollowTarget.IsValid())
		{
			StopFollowing();
		}
		PendingPanInput += AxisValue;
	}
}

void ASeinCameraPawn::HandleRotateInput(float YawDelta)
{
	if (!FMath::IsNearlyZero(YawDelta))
	{
		if (FollowTarget.IsValid())
		{
			StopFollowing();
		}
		CameraPivot->AddWorldRotation(FRotator(0.0f, YawDelta * RotationSpeed * GetWorld()->GetDeltaSeconds(), 0.0f));
	}
}

void ASeinCameraPawn::HandleTiltInput(float TiltDelta)
{
	if (FMath::IsNearlyZero(TiltDelta) || !SpringArm)
	{
		return;
	}

	if (FollowTarget.IsValid())
	{
		StopFollowing();
	}

	// Same clamp + arm update as the orbit input's Y axis, at a frame-rate-independent
	// keyboard rate instead of a per-pixel mouse sensitivity.
	CurrentPitch = FMath::Clamp(
		CurrentPitch + TiltDelta * TiltSpeed * GetWorld()->GetDeltaSeconds(),
		PitchMin,
		PitchMax);

	FRotator ArmRotation = SpringArm->GetRelativeRotation();
	ArmRotation.Pitch = CurrentPitch;
	SpringArm->SetRelativeRotation(ArmRotation);
}

void ASeinCameraPawn::HandleZoomInput(float ZoomDelta)
{
	if (!FMath::IsNearlyZero(ZoomDelta))
	{
		TargetZoomDistance = FMath::Clamp(TargetZoomDistance - ZoomDelta * ZoomStep, ZoomMin, ZoomMax);
	}
}

void ASeinCameraPawn::HandleKeyZoomInput(float ZoomDelta)
{
	if (!FMath::IsNearlyZero(ZoomDelta))
	{
		// Rate-based: a held key is a per-frame ±1, so without DeltaSeconds the zoom
		// speed would scale with frame rate (the wheel's per-tick ZoomStep is immune —
		// detents are discrete events).
		TargetZoomDistance = FMath::Clamp(
			TargetZoomDistance - ZoomDelta * ZoomRate * GetWorld()->GetDeltaSeconds(),
			ZoomMin,
			ZoomMax);
	}
}

void ASeinCameraPawn::ResetRotation()
{
	if (CameraPivot)
	{
		FRotator Current = CameraPivot->GetComponentRotation();
		Current.Yaw = 0.0f;
		CameraPivot->SetWorldRotation(Current);
	}
}

void ASeinCameraPawn::HandleMMBPanInput(const FVector2D& MouseDelta)
{
	if (MouseDelta.IsNearlyZero())
	{
		return;
	}

	if (FollowTarget.IsValid())
	{
		StopFollowing();
	}

	// Grab-the-ground style: both axes inverted so dragging in any direction
	// moves the world under the cursor, like pushing a map on a table.
	// Scale by a sensitivity factor relative to zoom distance.
	const float ZoomFactor = SpringArm ? (SpringArm->TargetArmLength / 1000.0f) : 1.0f;
	const float Sensitivity = 1.5f * ZoomFactor;

	const float Yaw = CameraPivot ? CameraPivot->GetComponentRotation().Yaw : 0.0f;
	const FRotator YawRotation(0.0f, Yaw, 0.0f);
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	// Both axes negated: drag right → camera left, drag up → camera back
	const FVector PanDelta = (Right * -MouseDelta.X + Forward * -MouseDelta.Y) * Sensitivity;
	CameraPivot->AddWorldOffset(PanDelta);
}

void ASeinCameraPawn::HandleOrbitInput(const FVector2D& MouseDelta)
{
	if (MouseDelta.IsNearlyZero())
	{
		return;
	}

	if (FollowTarget.IsValid())
	{
		StopFollowing();
	}

	// Mouse X → yaw orbit, per pixel like the pitch axis. No DeltaSeconds: a mouse
	// delta already encodes its magnitude, and time-scaling it made the same drag
	// rotate differently at different frame rates.
	if (!FMath::IsNearlyZero(MouseDelta.X))
	{
		CameraPivot->AddWorldRotation(FRotator(0.0f, MouseDelta.X * OrbitSensitivity, 0.0f));
	}

	// Mouse Y → pitch tilt (drag up = more flat/parallel to ground = increase pitch toward 0)
	if (!FMath::IsNearlyZero(MouseDelta.Y) && SpringArm)
	{
		CurrentPitch = FMath::Clamp(
			CurrentPitch + MouseDelta.Y * OrbitSensitivity,
			PitchMin,
			PitchMax
		);

		FRotator ArmRotation = SpringArm->GetRelativeRotation();
		ArmRotation.Pitch = CurrentPitch;
		SpringArm->SetRelativeRotation(ArmRotation);
	}
}

// ==================== Edge Scroll ====================

void ASeinCameraPawn::UpdateEdgeScroll(float DeltaSeconds)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}

	float MouseX, MouseY;
	if (!PC->GetMousePosition(MouseX, MouseY))
	{
		return;
	}

	int32 ViewportSizeX, ViewportSizeY;
	PC->GetViewportSize(ViewportSizeX, ViewportSizeY);

	if (ViewportSizeX <= 0 || ViewportSizeY <= 0)
	{
		return;
	}

	FVector2D EdgePan = FVector2D::ZeroVector;

	if (MouseX < EdgeScrollMargin)
	{
		EdgePan.X = -1.0f * (1.0f - MouseX / EdgeScrollMargin);
	}
	else if (MouseX > ViewportSizeX - EdgeScrollMargin)
	{
		EdgePan.X = 1.0f * (1.0f - (ViewportSizeX - MouseX) / EdgeScrollMargin);
	}

	if (MouseY < EdgeScrollMargin)
	{
		EdgePan.Y = 1.0f * (1.0f - MouseY / EdgeScrollMargin);
	}
	else if (MouseY > ViewportSizeY - EdgeScrollMargin)
	{
		EdgePan.Y = -1.0f * (1.0f - (ViewportSizeY - MouseY) / EdgeScrollMargin);
	}

	if (!EdgePan.IsNearlyZero())
	{
		PendingPanInput += EdgePan * EdgeScrollSpeedMultiplier;
	}
}

// ==================== Follow Target ====================

void ASeinCameraPawn::FollowEntity(FSeinEntityHandle Entity)
{
	FollowTarget = Entity;
}

void ASeinCameraPawn::StopFollowing()
{
	FollowTarget = FSeinEntityHandle::Invalid();
}

void ASeinCameraPawn::UpdateFollowTarget(float DeltaSeconds)
{
	USeinWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<USeinWorldSubsystem>();
	if (!Subsystem || !Subsystem->IsEntityAlive(FollowTarget))
	{
		StopFollowing();
		return;
	}

	// Get entity position from the sim via the entity pool
	const FSeinEntity* Entity = Subsystem->GetEntity(FollowTarget);
	if (!Entity)
	{
		StopFollowing();
		return;
	}

	// Smoothly move pivot to entity's position (XY only, keep current Z)
	const FVector EntityPos = Entity->Transform.GetLocation().ToVector();
	FVector CurrentPos = CameraPivot->GetComponentLocation();
	CurrentPos.X = FMath::FInterpTo(CurrentPos.X, EntityPos.X, DeltaSeconds, 8.0f);
	CurrentPos.Y = FMath::FInterpTo(CurrentPos.Y, EntityPos.Y, DeltaSeconds, 8.0f);
	CameraPivot->SetWorldLocation(CurrentPos);
}

// ==================== Bounds ====================

void ASeinCameraPawn::ClampToBounds()
{
	if (!bEnableBounds)
	{
		return;
	}

	FVector Pos = CameraPivot->GetComponentLocation();
	Pos.X = FMath::Clamp(Pos.X, WorldBounds.Min.X, WorldBounds.Max.X);
	Pos.Y = FMath::Clamp(Pos.Y, WorldBounds.Min.Y, WorldBounds.Max.Y);
	CameraPivot->SetWorldLocation(Pos);
}

// ==================== Ground Follow ====================

bool ASeinCameraPawn::TraceGroundHeight(const FVector2D& WorldXY, float& OutGroundZ) const
{
	UWorld* World = GetWorld();
	if (!World || !CameraPivot)
	{
		return false;
	}

	const float CenterZ = CameraPivot->GetComponentLocation().Z;
	const FVector Start(WorldXY.X, WorldXY.Y, CenterZ + GroundTraceExtent);
	const FVector End  (WorldXY.X, WorldXY.Y, CenterZ - GroundTraceExtent);

	FCollisionQueryParams QP(SCENE_QUERY_STAT(SeinCameraGround), /*bTraceComplex*/ false);
	QP.AddIgnoredActor(this);

	// Object-type query against static world only → ignores dynamic units/pawns, so the camera
	// follows terrain + static geometry rather than bobbing over unit tops.
	const FCollisionObjectQueryParams ObjQP(GroundTraceObjectType);

	FHitResult Hit;
	if (World->LineTraceSingleByObjectType(Hit, Start, End, ObjQP, QP))
	{
		OutGroundZ = Hit.ImpactPoint.Z;
		return true;
	}
	return false;
}

void ASeinCameraPawn::UpdateGroundFollow(float DeltaSeconds, bool bSnapImmediate)
{
	if (!bGroundFollow || !CameraPivot)
	{
		return;
	}

	const FVector Loc = CameraPivot->GetComponentLocation();
	float GroundZ = 0.0f;
	if (!TraceGroundHeight(FVector2D(Loc.X, Loc.Y), GroundZ))
	{
		return; // no ground (hole / off-map) → hold current Z
	}

	const float NewZ = bSnapImmediate
		? GroundZ
		: FMath::FInterpTo(Loc.Z, GroundZ, DeltaSeconds, GroundFollowInterpSpeed);
	CameraPivot->SetWorldLocation(FVector(Loc.X, Loc.Y, NewZ));
}

void ASeinCameraPawn::FocusOnWorldPoint(FVector WorldPoint)
{
	if (!CameraPivot)
	{
		return;
	}

	// A manual recenter cancels any active follow.
	if (FollowTarget.IsValid())
	{
		StopFollowing();
	}

	// Move the ground focus in XY immediately; leave Z to ground-follow so it eases to the
	// terrain height at the new spot (smooth altitude transition) rather than popping.
	FVector Loc = CameraPivot->GetComponentLocation();
	Loc.X = WorldPoint.X;
	Loc.Y = WorldPoint.Y;
	CameraPivot->SetWorldLocation(Loc);
}

// ==================== Accessors ====================

FVector ASeinCameraPawn::GetPivotLocation() const
{
	return CameraPivot ? CameraPivot->GetComponentLocation() : FVector::ZeroVector;
}

float ASeinCameraPawn::GetCameraYaw() const
{
	return CameraPivot ? CameraPivot->GetComponentRotation().Yaw : 0.0f;
}

float ASeinCameraPawn::GetCurrentZoomDistance() const
{
	return SpringArm ? SpringArm->TargetArmLength : 0.0f;
}

float ASeinCameraPawn::GetCameraPitch() const
{
	return CurrentPitch;
}

void ASeinCameraPawn::CaptureCameraState_Implementation(FSeinCameraSnapshotData& OutData)
{
	OutData.bHasState     = true;
	OutData.PivotLocation = GetPivotLocation();
	OutData.Yaw           = GetCameraYaw();
	OutData.Pitch         = GetCameraPitch();
	OutData.ZoomDistance  = GetCurrentZoomDistance();
}

void ASeinCameraPawn::RestoreCameraState_Implementation(const FSeinCameraSnapshotData& Data)
{
	if (!Data.bHasState) return;
	SetCameraState(Data.PivotLocation, Data.Yaw, Data.Pitch, Data.ZoomDistance);
}

void ASeinCameraPawn::SetCameraState(FVector PivotLocation, float Yaw, float Pitch, float ZoomDistance)
{
	// Snap actor (drives pivot location). The pawn's root is the pivot; spring
	// arm hangs off it.
	SetActorLocation(PivotLocation);
	if (CameraPivot)
	{
		FRotator PivotRot = CameraPivot->GetComponentRotation();
		PivotRot.Yaw = Yaw;
		CameraPivot->SetWorldRotation(PivotRot);
	}
	CurrentPitch = Pitch;
	if (SpringArm)
	{
		FRotator ArmRot = SpringArm->GetRelativeRotation();
		ArmRot.Pitch = Pitch;
		SpringArm->SetRelativeRotation(ArmRot);
		// Snap target arm length immediately + the live arm length so the
		// zoom interp on the next tick doesn't lerp from the old value.
		SpringArm->TargetArmLength = ZoomDistance;
	}
	TargetZoomDistance = ZoomDistance;
}

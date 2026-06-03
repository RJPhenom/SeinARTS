/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDebugDrawCull.cpp
 * @brief   Implementation of debug-viz camera cull + per-frame entity cap.
 */

#include "Debug/SeinDebugDrawCull.h"

#if UE_ENABLE_DEBUG_DRAWING

#include "Settings/PluginSettings.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif

namespace UE::SeinARTSMovement::DebugDraw
{

// Per-frame entity-cap state. Game-thread only — both consumer sites
// (sim tick + TSTicker) live on the game thread, so a plain static is
// fine. Resets when GFrameCounter advances; that means a frame with zero
// budget calls leaves the counters at the prior frame's values, which is
// harmless (the next call sees a frame change and resets).
namespace
{
	uint64 GLastResetFrame = 0;
	int32  GReservedThisFrame = 0;
}

FCameraView GetActiveCameraView(UWorld* World)
{
	FCameraView View;
	if (!World)
	{
		return View;
	}

	// PIE / shipping path: local player camera. Covers the common case
	// where the user is actively playing — debug viz tracks the player's
	// view, exactly what's wanted for "I'm watching this fight, only show
	// debug for units near where I'm looking."
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (APlayerCameraManager* PCM = PC->PlayerCameraManager)
		{
			View.Location = PCM->GetCameraLocation();
			View.Forward = PCM->GetCameraRotation().Vector();
			View.HalfFOVRadians = FMath::DegreesToRadians(PCM->GetFOVAngle() * 0.5f);
			View.bValid = true;
			return View;
		}
	}

#if WITH_EDITOR
	// Editor path: first level viewport pointed at this world. Hit when
	// the user has the editor open without PIE — rare for the per-tick
	// debug sites (no sim → no active moves, no avoidance), but kept for
	// completeness so editor-time tooling that calls into this helper
	// doesn't fail-open globally.
	if (GEditor)
	{
		for (FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
		{
			if (Vp && Vp->GetWorld() == World)
			{
				View.Location = Vp->GetViewLocation();
				View.Forward = Vp->GetViewRotation().Vector();
				View.HalfFOVRadians = FMath::DegreesToRadians(Vp->ViewFOV * 0.5f);
				View.bValid = true;
				return View;
			}
		}
	}
#endif

	return View;
}

bool PassesCameraCull(const FCameraView& View, const FVector& WorldLocation)
{
	// Fail open: if the camera couldn't be resolved, draw everything.
	// Better to over-draw briefly than to lose viz silently when world
	// state is weird (test maps, editor utility windows, headless tools).
	if (!View.bValid)
	{
		return true;
	}

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (!Settings)
	{
		return true;
	}

	const FVector ToPoint = WorldLocation - View.Location;
	const float DistSq = ToPoint.SizeSquared();
	const float MaxDist = Settings->DebugDrawMaxDistance;
	const float MaxDistSq = MaxDist * MaxDist;
	if (DistSq > MaxDistSq)
	{
		return false;
	}

	if (Settings->bDebugDrawFrustumCullEnabled && View.HalfFOVRadians > 0.0f)
	{
		// Cone-frustum cull. Project candidate onto camera forward axis;
		// negative = behind camera; positive = in front. Then test
		// off-axis distance against a cone whose radius at this depth is
		// (depth × tan(halfFOV)) × 1.3. The 1.3× pad keeps near-edge units
		// from popping in/out as the camera pans — still culls everything
		// well off-screen, which is the dominant cost.
		const float ProjForward = FVector::DotProduct(ToPoint, View.Forward);
		if (ProjForward < 0.0f)
		{
			return false;
		}
		const float ConeRadius = ProjForward * FMath::Tan(View.HalfFOVRadians) * 1.3f;
		const float ConeRadiusSq = ConeRadius * ConeRadius;
		const float OffAxisSq = DistSq - ProjForward * ProjForward;
		if (OffAxisSq > ConeRadiusSq)
		{
			return false;
		}
	}

	return true;
}

bool TryReserveBudget()
{
	if (GFrameCounter != GLastResetFrame)
	{
		GLastResetFrame = GFrameCounter;
		GReservedThisFrame = 0;
	}

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const int32 Cap = Settings ? Settings->DebugDrawMaxEntities : 50;
	if (GReservedThisFrame >= Cap)
	{
		return false;
	}

	++GReservedThisFrame;
	return true;
}

bool ShouldDrawAndReserve(UWorld* World, const FVector& WorldLocation)
{
	const FCameraView View = GetActiveCameraView(World);
	if (!PassesCameraCull(View, WorldLocation))
	{
		return false;
	}
	return TryReserveBudget();
}

FVector ComputeFootprintOriginAlong(
	const FVector& AgentPos,
	const FVector& Direction,
	float FootprintRadius,
	float ZLift)
{
	FVector Result = AgentPos;
	Result.Z += ZLift;

	// Degenerate direction → no offset, fall back to center+Z (caller's
	// arrows will still draw but origin will be center-anchored; preferable
	// to a NaN from normalizing a zero vector).
	const float DirLenSq = Direction.SizeSquared();
	if (DirLenSq <= UE_KINDA_SMALL_NUMBER || FootprintRadius <= 0.0f)
	{
		return Result;
	}

	const FVector DirNorm = Direction / FMath::Sqrt(DirLenSq);
	Result.X += DirNorm.X * FootprintRadius;
	Result.Y += DirNorm.Y * FootprintRadius;
	// Z component of Direction intentionally ignored — steering vectors
	// are planar (XY); the ZLift handles the above-terrain offset.
	return Result;
}

} // namespace UE::SeinARTSMovement::DebugDraw

#endif // UE_ENABLE_DEBUG_DRAWING

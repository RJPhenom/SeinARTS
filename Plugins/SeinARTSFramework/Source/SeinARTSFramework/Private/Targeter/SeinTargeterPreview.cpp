/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTargeterPreview.cpp
 * @brief   Targeter preview base class implementation. Pure presentation —
 *          state pushed in by USeinTargeterSubsystem each tick, no sim
 *          interaction.
 */

#include "Targeter/SeinTargeterPreview.h"

ASeinTargeterPreview::ASeinTargeterPreview()
{
	PrimaryActorTick.bCanEverTick = true;
	// Subsystem drives state via UpdatePreview; we don't need world ticks for
	// state tracking, but BP subclasses may want their own per-frame logic.
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = false; // Render-side only.
	SetActorTickEnabled(true);
}

void ASeinTargeterPreview::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// State is pushed via UpdatePreview; nothing to do here at the base level.
	// Subclasses with continuous animation override Tick or use their own
	// timeline / material parameter logic.
}

void ASeinTargeterPreview::InitializePreview(USeinTargeterSpec* InSpec, float InAreaRadiusWorld)
{
	Spec = InSpec;
	AreaRadiusWorld = InAreaRadiusWorld;
}

void ASeinTargeterPreview::UpdatePreview(const FVector& CursorWorld, const FVector& DragAnchorWorld,
	ESeinTargeterValidity Validity, float DragYawDegrees)
{
	CurrentCursorWorld = CursorWorld;
	CurrentDragAnchorWorld = DragAnchorWorld;
	CurrentValidity = Validity;
	CurrentDragYawDegrees = DragYawDegrees;
	// Move the actor to the cursor by default — most preview shapes are
	// cursor-centered. Subclasses that need different transforms (line specs
	// anchor at the drag origin, building specs snap to footprint cells) can
	// override OnPreviewUpdated and set their own transform.
	SetActorLocation(CursorWorld);
	OnPreviewUpdated();
}

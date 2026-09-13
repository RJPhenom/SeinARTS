/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTargeterPreview.cpp
 * @brief   Targeter preview base class implementation. Pure presentation —
 *          state pushed in by USeinTargeterSubsystem each tick, no sim
 *          interaction.
 */

#include "Targeter/SeinTargeterPreview.h"
#include "Targeter/SeinTargeterVisualComponent.h"
#include "Components/SceneComponent.h"

ASeinTargeterPreview::ASeinTargeterPreview()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("PreviewRoot"));
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
	if (bFollowResolvedTarget) SetActorLocation(CursorWorld);
	OnPreviewUpdated();
}

void ASeinTargeterPreview::NotifyPointCaptured(
	const FVector& StartWorld, const FVector& EndWorld)
{
	OnPointCaptured(StartWorld, EndWorld);
}

void ASeinTargeterPreview::BeginPreview(const FSeinTargeterPreviewContext& InContext)
{
	if (bPresentationStarted || bPresentationEnded) return;
	Context = InContext;
	PrepareVisuals();
	TInlineComponentArray<USeinTargeterVisualComponent*> Visuals(this);
	for (auto* Visual : Visuals) Visual->InitializeVisual(Context);
	bPresentationStarted = true;
	OnPreviewInitialized();
	if (!bPresentationEnded) Present(InContext);
}

void ASeinTargeterPreview::Present(const FSeinTargeterPreviewContext& InContext)
{
	if (bPresentationEnded) return;
	const auto Previous = Context.Validity;
	const bool Changed = !bHasPresented || Previous != InContext.Validity
		|| !Context.ValidityReason.EqualTo(InContext.ValidityReason);
	Context = InContext;
	bCaptureHasAnchor = Context.bHasAnchor;
	CurrentCursorWorld = Context.CursorWorld;
	CurrentDragAnchorWorld = Context.AnchorWorld;
	CurrentValidity = Context.Validity;
	CurrentDragYawDegrees = Context.ResolvedTarget.Rotator().Yaw;
	if (bFollowResolvedTarget) SetActorLocationAndRotation(Context.ResolvedTarget.GetLocation(), Context.ResolvedTarget.GetRotation());
	TInlineComponentArray<USeinTargeterVisualComponent*> Visuals(this);
	for (auto* Visual : Visuals) Visual->UpdateVisual(Context);
	bHasPresented = true;
	if (Changed) OnValidityChanged(Previous);
	if (!bPresentationEnded) OnPreviewUpdated();
}

void ASeinTargeterPreview::EndPreview(ESeinPreviewEndReason Reason, bool bNotify)
{
	if (bPresentationEnded) return;
	bPresentationEnded = true;
	if (bNotify) OnPreviewEnded(Reason);
}

void ASeinTargeterPreview::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bPresentationStarted) EndPreview(ESeinPreviewEndReason::Unavailable);
	Super::EndPlay(EndPlayReason);
}

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinEntityWidget.cpp
 * @author       RJ Macklem
 * @created      11 Sep 2026
 * @latest       11 Sep 2026
 * @brief        Projects authoritative game data into a reusable progress display.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#include "Widget/SeinEntityWidget.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Lib/SeinConstructionBPFL.h"
void USeinEntityWidget::SetEntityContext(FSeinEntityHandle Entity)
{
	TOptional<USeinWorldSubsystem::FReadOnlyObserverScope> ObserverScope;
	if (auto* World = GetWorld())
		if (auto* Sim = World->GetSubsystem<USeinWorldSubsystem>()) ObserverScope.Emplace(*Sim);
	if (BoundEntity != Entity)
	{
		BoundEntity = Entity;
		OnEntityContextChanged(Entity);
	}
	RefreshEntityPresentation();
}
void USeinEntityWidget::RefreshEntityPresentation()
{
	TOptional<USeinWorldSubsystem::FReadOnlyObserverScope> ObserverScope;
	if (auto* World = GetWorld())
		if (auto* Sim = World->GetSubsystem<USeinWorldSubsystem>()) ObserverScope.Emplace(*Sim);
	const auto* World = GetWorld() ? GetWorld()->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	const bool bValid = World && World->IsEntityAlive(BoundEntity);
	const auto Entity = BoundEntity;
	const FSeinProgressDisplay Display = bValid ? GetProgressDisplay() : FSeinProgressDisplay();
	if (!IsValid(this) || BoundEntity != Entity) return;
	// A transparent, non-interactive display still ticks, so changing eligibility can show it again.
	SetVisibility(Display.bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::HitTestInvisible);
	SetRenderOpacity(Display.bVisible ? 1.f : 0.f);
	if (WidgetTree)
	{
		if (auto* Bar = Cast<UProgressBar>(WidgetTree->FindWidget(ProgressBarName)))
			Bar->SetPercent(FMath::IsFinite(Display.Percent) ? FMath::Clamp(Display.Percent, 0.f, 1.f) : 0.f);
		if (auto* Label = Cast<UTextBlock>(WidgetTree->FindWidget(ProgressLabelName)))
			Label->SetText(Display.Label);
	}
}
void USeinEntityWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	RefreshEntityPresentation();
}
FSeinProgressDisplay USeinConstructionWorkProgressWidget::GetProgressDisplay_Implementation() const
{
	FSeinProgressDisplay Display;
	const auto Job = USeinConstructionBPFL::SeinGetConstructionStatus(this, BoundEntity);
	const auto Work = USeinConstructionBPFL::SeinGetConstructionWorkStatus(this, BoundEntity);
	Display.bVisible = Job.bValid && Work.bValid && Job.State != ESeinConstructionState::Complete;
	Display.Percent = Work.Percent.ToFloat();
	if (Job.State == ESeinConstructionState::Queued) Display.Label = NSLOCTEXT("SeinConstruction", "Queued", "Waiting for builder");
	else if (Job.State == ESeinConstructionState::Paused) Display.Label = NSLOCTEXT("SeinConstruction", "Paused", "Paused");
	else Display.Label = NSLOCTEXT("SeinConstruction", "Building", "Building");
	return Display;
}

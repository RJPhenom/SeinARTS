/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinConstructionRenderComponent.cpp
 * @author       RJ Macklem
 * @created      11 Sep 2026
 * @latest       11 Sep 2026
 * @brief        Maps construction lifecycle onto explicitly configured visual groups.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#include "Components/ActorComponents/SeinConstructionRenderComponent.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"
#include "Lib/SeinConstructionBPFL.h"

USeinConstructionRenderComponent::USeinConstructionRenderComponent()
{
	FSeinPresentationGroup Finished; Finished.Name = FinishedGroup; Finished.bInitiallyVisible = true;
	FSeinPresentationGroup Construction; Construction.Name = ConstructionGroup;
	Groups = {Finished, Construction};
}
void USeinConstructionRenderComponent::ApplyState(ESeinConstructionState State)
{
	const uint32 Revision = GetBindingRevision();
	const bool bComplete = State == ESeinConstructionState::Complete;
	SetPresentationGroupVisible(FinishedGroup, bComplete);
	if (!IsBindingEnding() && Revision == GetBindingRevision()) SetPresentationGroupVisible(ConstructionGroup, !bComplete);
}
void USeinConstructionRenderComponent::BindingWillChange()
{
	bHasState = false;
	Super::BindingWillChange();
}
void USeinConstructionRenderComponent::RefreshPresentation_Implementation()
{
	if (!GetEntityHandle().IsValid())
	{
		bHasState = false;
		Super::RefreshPresentation_Implementation();
		return;
	}
	const auto Status = USeinConstructionBPFL::SeinGetConstructionStatus(this, GetEntityHandle());
	// Snapshot refresh establishes appearance. Transition delegates are reserved for ordered visual events.
	CurrentState = Status.State;
	CurrentStage = Status.Stage;
	bHasState = true;
	ApplyState(CurrentState);
}
void USeinConstructionRenderComponent::HandleBoundVisualEvent(const FSeinVisualEvent& Event)
{
	TOptional<USeinWorldSubsystem::FReadOnlyObserverScope> ObserverScope;
	if (auto* World = GetWorld())
		if (auto* Sim = World->GetSubsystem<USeinWorldSubsystem>()) ObserverScope.Emplace(*Sim);
	const uint32 Revision = GetBindingRevision();
	if (IsBindingEnding()) return;
	if (Event.Type == ESeinVisualEventType::ConstructionStateChanged)
	{
		CurrentState = Event.NewConstructionState;
		CurrentStage = Event.NewConstructionStage;
		bHasState = true;
		if (CurrentState != ESeinConstructionState::Complete) ApplyState(CurrentState);
		if (IsBindingEnding() || Revision != GetBindingRevision()) return;
		if (Event.OldConstructionState != Event.NewConstructionState)
			OnConstructionStateChanged.Broadcast(Event.OldConstructionState, Event.NewConstructionState);
		if (IsBindingEnding() || Revision != GetBindingRevision()) return;
		if (Event.OldConstructionStage != Event.NewConstructionStage)
			OnConstructionStageChanged.Broadcast(Event.OldConstructionStage, Event.NewConstructionStage);
		if (IsBindingEnding() || Revision != GetBindingRevision()) return;
		if (Event.NewConstructionState == ESeinConstructionState::Complete) ApplyState(Event.NewConstructionState);
	}
	if (!IsBindingEnding() && Revision == GetBindingRevision()) Super::HandleBoundVisualEvent(Event);
}

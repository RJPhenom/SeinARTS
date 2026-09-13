/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinEntityWidgetComponent.cpp
 * @author       RJ Macklem
 * @created      11 Sep 2026
 * @latest       11 Sep 2026
 * @brief        Initializes and clears widget context across binding, replacement and teardown.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#include "Widget/SeinEntityWidgetComponent.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"
#include "Widget/SeinEntityWidget.h"
#include "Actor/SeinActor.h"
#include "Components/ActorComponents/SeinEntityBindingComponent.h"
USeinEntityWidgetComponent::USeinEntityWidgetComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}
FSeinEntityHandle USeinEntityWidgetComponent::GetEntityHandle() const
{
	return Binding ? Binding->GetEntityHandle() : FSeinEntityHandle::Invalid();
}
ASeinActor* USeinEntityWidgetComponent::GetEntityActor() const
{
	return Binding ? Binding->GetEntityActor() : nullptr;
}
void USeinEntityWidgetComponent::InitializeWidgetContext()
{
	RefreshWidgetContextInternal(true);
}
void USeinEntityWidgetComponent::BindToEntityActor(ASeinActor* Actor)
{
	if (bEnding || (Actor && Actor->GetWorld() != GetWorld())) return;
	++ContextRevision;
	bExplicit = true;
	ExplicitActor = Actor;
	if (Binding) Binding->BindToEntityActor(Actor);
	RefreshWidgetContext();
}
void USeinEntityWidgetComponent::BeginPlay()
{
	Super::BeginPlay();
	ASeinActor* Actor = bExplicit ? ExplicitActor.Get() : Cast<ASeinActor>(GetOwner());
	if (!bExplicit && !Actor)
	{
		if (auto* Existing = GetOwner()->FindComponentByClass<USeinEntityBindingComponent>())
			Actor = Existing->GetEntityActor();
	}
	Binding = NewObject<USeinEntityBindingComponent>(GetOwner());
	GetOwner()->AddInstanceComponent(Binding);
	Binding->BindToEntityActor(Actor);
	Binding->OnPresentationRefresh.AddDynamic(this, &USeinEntityWidgetComponent::RefreshWidgetContext);
	Binding->RegisterComponent();
	RefreshWidgetContext();
}
void USeinEntityWidgetComponent::RefreshWidgetContext()
{
	RefreshWidgetContextInternal(false);
}
void USeinEntityWidgetComponent::RefreshWidgetContextInternal(bool bBroadcast)
{
	if (bEnding || !HasBegunPlay()) return;
	TOptional<USeinWorldSubsystem::FReadOnlyObserverScope> ObserverScope;
	if (auto* World = GetWorld())
		if (auto* Sim = World->GetSubsystem<USeinWorldSubsystem>()) ObserverScope.Emplace(*Sim);
	const uint32 Revision = ++ContextRevision;
	InitWidget();
	if (bEnding || Revision != ContextRevision) return;
	UUserWidget* DisplayWidget = GetUserWidgetObject();
	const auto Entity = GetEntityHandle();
	UUserWidget* OldWidget = LastWidget.Get();
	const bool bChanged = OldWidget != DisplayWidget || LastEntity != Entity;
	LastWidget = DisplayWidget;
	LastEntity = Entity;
	if (OldWidget != DisplayWidget)
	{
		if (auto* Old = Cast<USeinEntityWidget>(OldWidget)) Old->SetEntityContext(FSeinEntityHandle::Invalid());
	}
	if (bEnding || Revision != ContextRevision || DisplayWidget != GetUserWidgetObject()) return;
	if (auto* Bound = Cast<USeinEntityWidget>(DisplayWidget)) Bound->SetEntityContext(Entity);
	if (bEnding || Revision != ContextRevision || DisplayWidget != GetUserWidgetObject()) return;
	if (bChanged || bBroadcast) OnWidgetEntityContext.Broadcast(DisplayWidget, Entity);
}
void USeinEntityWidgetComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function)
{
	// World widgets can be hidden or offscreen. Their context and eligibility must still refresh.
	RefreshWidgetContext();
	Super::TickComponent(DeltaTime, TickType, Function);
}
void USeinEntityWidgetComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	bEnding = true;
	++ContextRevision;
	TOptional<USeinWorldSubsystem::FReadOnlyObserverScope> ObserverScope;
	if (auto* World = GetWorld())
		if (auto* Sim = World->GetSubsystem<USeinWorldSubsystem>()) ObserverScope.Emplace(*Sim);
	LastEntity = FSeinEntityHandle::Invalid();
	LastWidget.Reset();
	ExplicitActor.Reset();
	if (Binding)
	{
		Binding->OnPresentationRefresh.RemoveDynamic(this, &USeinEntityWidgetComponent::RefreshWidgetContext);
		Binding->DestroyComponent();
		Binding = nullptr;
	}
	if (auto* DisplayWidget = Cast<USeinEntityWidget>(GetUserWidgetObject())) DisplayWidget->SetEntityContext(FSeinEntityHandle::Invalid());
	OnWidgetEntityContext.Broadcast(GetUserWidgetObject(), FSeinEntityHandle::Invalid());
	Super::EndPlay(Reason);
}

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinEntityBindingComponent.cpp
 * @author       RJ Macklem
 * @created      11 Sep 2026
 * @latest       11 Sep 2026
 * @brief        Manages render-only entity binding across initialization, restore and teardown.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#include "Components/ActorComponents/SeinEntityBindingComponent.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"
#include "Actor/SeinActor.h"
#include "Actor/SeinEntityBridgeComponent.h"

USeinEntityBindingComponent::USeinEntityBindingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}
void USeinEntityBindingComponent::Unsubscribe()
{
	if (auto* Source = Bridge.Get())
	{
		Source->OnVisualEvent.RemoveDynamic(this, &USeinEntityBindingComponent::HandleBoundVisualEvent);
		Source->OnPresentationRefresh.RemoveDynamic(this, &USeinEntityBindingComponent::RefreshEntityBinding);
	}
	Bridge.Reset();
}
void USeinEntityBindingComponent::BindToEntityActor(ASeinActor* Actor)
{
	TOptional<USeinWorldSubsystem::FReadOnlyObserverScope> ObserverScope;
	if (auto* World = GetWorld())
		if (auto* Sim = World->GetSubsystem<USeinWorldSubsystem>()) ObserverScope.Emplace(*Sim);
	if (bEnding || (Actor && Actor->GetWorld() != GetWorld())) return;
	bExplicitBinding = true;
	if (BoundActor.Get() != Actor)
	{
		const uint32 Revision = ++BindingRevision;
		BindingWillChange();
		if (bEnding || Revision != BindingRevision) return;
		Unsubscribe();
		BoundActor = Actor;
	}
	if (!Bridge.IsValid() && IsValid(Actor))
	{
		Bridge = Actor->FindComponentByClass<USeinEntityBridgeComponent>();
		if (auto* Source = Bridge.Get())
		{
			Source->OnVisualEvent.AddUniqueDynamic(this, &USeinEntityBindingComponent::HandleBoundVisualEvent);
			Source->OnPresentationRefresh.AddUniqueDynamic(this, &USeinEntityBindingComponent::RefreshEntityBinding);
		}
	}
	RefreshEntityBinding();
}
FSeinEntityHandle USeinEntityBindingComponent::GetEntityHandle() const
{
	const auto* Source = Bridge.Get();
	return Source && Source->HasValidEntity() ? Source->GetEntityHandle() : FSeinEntityHandle::Invalid();
}
ASeinActor* USeinEntityBindingComponent::GetEntityActor() const
{
	return GetEntityHandle().IsValid() ? BoundActor.Get() : nullptr;
}
void USeinEntityBindingComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!bExplicitBinding)
	{
		ASeinActor* EntityActor = Cast<ASeinActor>(GetOwner());
		if (!EntityActor)
		{
			// Blueprint construction scripts may add bindings after the creator supplies native context.
			TArray<USeinEntityBindingComponent*> Siblings;
			GetOwner()->GetComponents(Siblings);
			for (const auto* Sibling : Siblings)
			{
				if (Sibling != this && Sibling->bExplicitBinding && Sibling->BoundActor.IsValid())
				{
					EntityActor = Sibling->BoundActor.Get();
					break;
				}
			}
		}
		BindToEntityActor(EntityActor);
	}
	else RefreshEntityBinding();
}
void USeinEntityBindingComponent::RefreshEntityBinding()
{
	TOptional<USeinWorldSubsystem::FReadOnlyObserverScope> ObserverScope;
	if (auto* World = GetWorld())
		if (auto* Sim = World->GetSubsystem<USeinWorldSubsystem>()) ObserverScope.Emplace(*Sim);
	if (bEnding || !HasBegunPlay()) return;
	const uint32 Revision = BindingRevision;
	const FSeinEntityHandle Entity = GetEntityHandle();
	if (Entity != LastEntity)
	{
		LastEntity = Entity;
		OnEntityBindingChanged.Broadcast(Entity);
	}
	if (bEnding || Revision != BindingRevision) return;
	RefreshPresentation();
	if (!bEnding && Revision == BindingRevision) OnPresentationRefresh.Broadcast();
}
void USeinEntityBindingComponent::HandleBoundVisualEvent(const FSeinVisualEvent& Event)
{
	TOptional<USeinWorldSubsystem::FReadOnlyObserverScope> ObserverScope;
	if (auto* World = GetWorld())
		if (auto* Sim = World->GetSubsystem<USeinWorldSubsystem>()) ObserverScope.Emplace(*Sim);
	if (bEnding || !HasBegunPlay()) return;
	const uint32 Revision = BindingRevision;
	OnBoundVisualEvent.Broadcast(Event);
	if (!bEnding && Revision == BindingRevision && Event.Type == ESeinVisualEventType::EntityDestroyed)
		BindToEntityActor(nullptr);
}
void USeinEntityBindingComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	TOptional<USeinWorldSubsystem::FReadOnlyObserverScope> ObserverScope;
	if (auto* World = GetWorld())
		if (auto* Sim = World->GetSubsystem<USeinWorldSubsystem>()) ObserverScope.Emplace(*Sim);
	bEnding = true;
	BindingWillChange();
	Unsubscribe();
	BoundActor.Reset();
	LastEntity = FSeinEntityHandle::Invalid();
	Super::EndPlay(Reason);
}

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinEntityPresentationComponent.cpp
 * @author       RJ Macklem
 * @created      11 Sep 2026
 * @latest       11 Sep 2026
 * @brief        Owns explicit group visibility and bound actor lifetime.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#include "Components/ActorComponents/SeinEntityPresentationComponent.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"
#include "Actor/SeinActor.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"

bool USeinEntityPresentationComponent::InitializeGroups()
{
	if (bInitialized) return true;
	TSet<FName> Names;
	TSet<USceneComponent*> Members;
	// Validate all membership before changing any component.
	for (const auto& Group : Groups)
	{
		if (Group.Name.IsNone() || Names.Contains(Group.Name)) return false;
		Names.Add(Group.Name);
		for (const auto& Reference : Group.Components)
		{
			auto* Component = Cast<USceneComponent>(Reference.GetComponent(GetOwner()));
			if (!Component || Component->GetOwner() != GetOwner() || Members.Contains(Component)) return false;
			Members.Add(Component);
		}
	}
	for (const auto& Group : Groups)
	{
		auto& State = Runtime.Add(Group.Name);
		for (const auto& Reference : Group.Components)
		{
			auto* Component = CastChecked<USceneComponent>(Reference.GetComponent(GetOwner()));
			State.Members.Add({Component, Component->IsVisible(), Component->bHiddenInGame != 0});
		}
	}
	bInitialized = true;
	return true;
}
bool USeinEntityPresentationComponent::SetPresentationGroupVisible(FName GroupName, bool bVisible)
{
	TOptional<USeinWorldSubsystem::FReadOnlyObserverScope> ObserverScope;
	if (auto* World = GetWorld())
		if (auto* Sim = World->GetSubsystem<USeinWorldSubsystem>()) ObserverScope.Emplace(*Sim);
	if (IsBindingEnding() || !HasBegunPlay() || !InitializeGroups()) return false;
	auto* State = Runtime.Find(GroupName);
	const auto* Definition = Groups.FindByPredicate([&](const auto& Group) { return Group.Name == GroupName; });
	if (!State || !Definition) return false;
	State->bVisible = bVisible;
	for (const auto& Member : State->Members)
	{
		if (auto* Component = Member.Component.Get())
		{
			Component->SetVisibility(bVisible, false);
			Component->SetHiddenInGame(!bVisible, false);
		}
	}
	if (!bVisible)
	{
		const TWeakObjectPtr<AActor> Actor = State->Actor;
		State->Actor.Reset();
		if (Actor.IsValid()) Actor->Destroy();
		return true;
	}
	if (State->bSpawning || State->Actor.IsValid() || !Definition->ActorClass || !GetEntityHandle().IsValid()) return true;
	const uint32 Revision = PresentationRevision;
	const auto Entity = GetEntityHandle();
	const auto ActorClass = Definition->ActorClass;
	const FTransform Transform = GetOwner()->GetActorTransform();
	State->bSpawning = true;
	AActor* Spawned = GetWorld()->SpawnActorDeferred<AActor>(ActorClass, Transform, GetEntityActor(),
		nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	State = Runtime.Find(GroupName);
	if (State && Revision == PresentationRevision) State->bSpawning = false;
	if (!Spawned) return false;
	if (IsBindingEnding() || Revision != PresentationRevision || !State || !State->bVisible)
	{
		Spawned->Destroy();
		return false;
	}
	// Own the pending actor before construction scripts and BeginPlay can re-enter this group.
	State->Actor = Spawned;
	// Reuse native context when present; ordinary Get Component By Class must see ready context.
	TArray<USeinEntityBindingComponent*> InitialBindings;
	Spawned->GetComponents(InitialBindings);
	if (InitialBindings.IsEmpty())
	{
		auto* Context = NewObject<USeinEntityBindingComponent>(Spawned, MakeUniqueObjectName(Spawned, USeinEntityBindingComponent::StaticClass(), TEXT("EntityContext")));
		Spawned->AddInstanceComponent(Context);
		Context->RegisterComponent();
		InitialBindings.Add(Context);
	}
	for (auto* Binding : InitialBindings) Binding->BindToEntityActor(GetEntityActor());
	Spawned->FinishSpawning(Transform);
	State = Runtime.Find(GroupName);
	if (!IsValid(Spawned)) return false;
	if (IsBindingEnding() || Revision != PresentationRevision || Entity != GetEntityHandle() || !State || !State->bVisible)
	{
		Spawned->Destroy();
		return false;
	}
	State->Actor = Spawned;
	if (auto* Root = Spawned->GetRootComponent())
	{
		if (auto* Parent = GetOwner()->GetRootComponent())
		{
			if (Parent->Mobility == EComponentMobility::Movable) Root->SetMobility(EComponentMobility::Movable);
			Root->AttachToComponent(Parent, FAttachmentTransformRules::KeepWorldTransform);
		}
	}
	// Bind Blueprint-added components created during the construction script as well.
	TArray<USeinEntityBindingComponent*> Bindings;
	Spawned->GetComponents(Bindings);
	for (auto* Binding : Bindings)
	{
		if (!IsValid(Spawned) || IsBindingEnding() || Revision != PresentationRevision) break;
		if (IsValid(Binding) && !InitialBindings.Contains(Binding)) Binding->BindToEntityActor(GetEntityActor());
	}
	return true;
}
bool USeinEntityPresentationComponent::IsPresentationGroupVisible(FName Group) const
{
	const auto* State = Runtime.Find(Group);
	return State && State->bVisible;
}
AActor* USeinEntityPresentationComponent::GetPresentationGroupActor(FName Group) const
{
	const auto* State = Runtime.Find(Group);
	return State ? State->Actor.Get() : nullptr;
}
void USeinEntityPresentationComponent::ReleaseGroups()
{
	++PresentationRevision;
	TMap<FName, FGroupRuntime> Old = MoveTemp(Runtime);
	Runtime.Reset();
	bInitialized = false;
	for (auto& Pair : Old)
	{
		for (const auto& Member : Pair.Value.Members)
		{
			if (auto* Component = Member.Component.Get())
			{
				Component->SetVisibility(Member.bVisible, false);
				Component->SetHiddenInGame(Member.bHidden, false);
			}
		}
	}
	// Restore all borrowed components before arbitrary actor EndPlay callbacks can rebind us.
	for (auto& Pair : Old)
		if (auto* Actor = Pair.Value.Actor.Get()) Actor->Destroy();
}
void USeinEntityPresentationComponent::BindingWillChange()
{
	ReleaseGroups();
	Super::BindingWillChange();
}
void USeinEntityPresentationComponent::RefreshPresentation_Implementation()
{
	if (!GetEntityHandle().IsValid()) { ReleaseGroups(); return; }
	const bool bWasInitialized = bInitialized;
	if (!InitializeGroups()) return;
	const uint32 Revision = PresentationRevision;
	const auto Definitions = Groups;
	for (const auto& Group : Definitions)
	{
		if (Revision != PresentationRevision || IsBindingEnding()) return;
		SetPresentationGroupVisible(Group.Name, bWasInitialized ? IsPresentationGroupVisible(Group.Name) : Group.bInitiallyVisible);
	}
}

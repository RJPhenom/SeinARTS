// SeinARTS Framework test fixtures. Construction event receivers and completion effects.
#pragma once
#include "CoreMinimal.h"
#include "Effects/SeinEffect.h"
#include "Components/SeinConstructionTypes.h"
#include "Actor/SeinActor.h"
#include "Components/SceneComponent.h"
#include "Components/ActorComponents/SeinConstructionRenderComponent.h"
#include "Lib/SeinConstructionBPFL.h"
#include "SeinConstructionTestTypes.generated.h"

UCLASS()
class USeinConstructionTestEffect : public USeinEffect
{
	GENERATED_BODY()
public:
	USeinConstructionTestEffect()
	{
		DurationMode = ESeinEffectDurationMode::Persistent;
		StackingRule = ESeinEffectStackingRule::Independent;
	}
};

UCLASS()
class USeinConstructionEventProbe : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TObjectPtr<USeinConstructionRenderComponent> Renderer;
	UPROPERTY()
	TObjectPtr<ASeinActor> RebindTarget;
	int32 RefreshCount = 0;
	int32 EventCount = 0;
	FFixedPoint RefreshedWork;
	UFUNCTION()
	void Refreshed()
	{
		++RefreshCount;
		if (Renderer) RefreshedWork = USeinConstructionBPFL::SeinGetConstructionWorkStatus(Renderer, Renderer->GetEntityHandle()).Progress;
	}
	UFUNCTION()
	void VisualEvent(const FSeinVisualEvent& Event) { ++EventCount; }
	TArray<ESeinConstructionState> OldStates;
	TArray<ESeinConstructionState> NewStates;
	TArray<FGameplayTag> OldStages;
	TArray<FGameplayTag> NewStages;

	UFUNCTION()
	void StateChanged(ESeinConstructionState OldState, ESeinConstructionState NewState)
	{
		OldStates.Add(OldState);
		NewStates.Add(NewState);
		if (Renderer && RebindTarget && NewState == ESeinConstructionState::Complete) Renderer->BindToEntityActor(RebindTarget);
	}
	UFUNCTION()
	void StageChanged(FGameplayTag OldStage, FGameplayTag NewStage)
	{
		OldStages.Add(OldStage);
		NewStages.Add(NewStage);
	}
};


/** Exercises arbitrary-actor context and recursive group refresh during BeginPlay. */
UCLASS()
class ASeinConstructionManagedTestActor : public AActor
{
	GENERATED_BODY()
public:
	ASeinConstructionManagedTestActor()
	{
		SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
		CreateDefaultSubobject<USeinEntityBindingComponent>(TEXT("EntityContext"));
	}
	bool bContextReadyAtBeginPlay = false;
	FSeinEntityHandle EntityAtBeginPlay;
	virtual void PostInitializeComponents() override
	{
		Super::PostInitializeComponents();
		// CQTest initializes actors without starting the whole game world.
		if (!HasActorBegunPlay()) DispatchBeginPlay();
	}
	virtual void BeginPlay() override
	{
		Super::BeginPlay();
		if (auto* Binding = FindComponentByClass<USeinEntityBindingComponent>()) EntityAtBeginPlay = Binding->GetEntityHandle();
		bContextReadyAtBeginPlay = EntityAtBeginPlay.IsValid();
		if (auto* Renderer = GetOwner()->FindComponentByClass<USeinConstructionRenderComponent>())
			Renderer->SetPresentationGroupVisible(Renderer->ConstructionGroup, true);
	}
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinEntityPresentationComponent.h
 * @author       RJ Macklem
 * @created      11 Sep 2026
 * @latest       11 Sep 2026
 * @brief        Controls explicitly assigned visual groups and optional managed actors.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#pragma once
#include "Components/ActorComponents/SeinEntityBindingComponent.h"
#include "Engine/EngineTypes.h"
#include "SeinEntityPresentationComponent.generated.h"
class USceneComponent;
/** Explicit visual membership. Widgets or other components outside this list are never changed. */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinPresentationGroup
{
	GENERATED_BODY()
	/** Name used by Set Presentation Group Visible. Names must be unique within this component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS")
	FName Name;
	/** Scene components controlled by this group. Each component may belong to only one group. Children are not inferred. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS", meta = (UseComponentPicker, AllowedClasses = "/Script/Engine.SceneComponent"))
	TArray<FComponentReference> Components;
	/** Initial visibility before a Blueprint or native presentation adapter applies its state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS")
	bool bInitiallyVisible = false;
	/** Optional actor created while this group is visible. Any actor class is supported.
	 * Its Owner is the entity actor. An Entity Binding component provides context before BeginPlay.
	 * It is destroyed when the group hides, the binding changes, the entity dies or this component ends play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS")
	TSubclassOf<AActor> ActorClass;
};
/** Explicit visual groups on a stable actor. Does not decide gameplay state or hide unrelated components. */
UCLASS(Blueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSCOREENTITY_API USeinEntityPresentationComponent : public USeinEntityBindingComponent
{
	GENERATED_BODY()
public:
	/** Visual groups belonging to this actor. Configure references explicitly; no mesh discovery runs at play time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS")
	TArray<FSeinPresentationGroup> Groups;
	/** Show or hide exactly this group's components and optional actor. Returns false for missing or overlapping groups. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Presentation")
	bool SetPresentationGroupVisible(FName Group, bool bVisible);
	/** Whether a configured group is currently visible. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Presentation")
	bool IsPresentationGroupVisible(FName Group) const;
	/** Managed actor currently displayed for this group, or None. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Presentation")
	AActor* GetPresentationGroupActor(FName Group) const;
	virtual void RefreshPresentation_Implementation() override;
protected:
	virtual void BindingWillChange() override;
private:
	struct FMember
	{
		TWeakObjectPtr<USceneComponent> Component;
		bool bVisible = false;
		bool bHidden = false;
	};
	struct FGroupRuntime
	{
		TArray<FMember> Members;
		TWeakObjectPtr<AActor> Actor;
		bool bSpawning = false;
		bool bVisible = false;
	};
	TMap<FName, FGroupRuntime> Runtime;
	bool bInitialized = false;
	uint32 PresentationRevision = 0;
	bool InitializeGroups();
	void ReleaseGroups();
};

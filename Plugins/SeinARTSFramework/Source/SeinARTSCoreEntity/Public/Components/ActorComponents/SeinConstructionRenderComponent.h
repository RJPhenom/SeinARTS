/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinConstructionRenderComponent.h
 * @author       RJ Macklem
 * @created      11 Sep 2026
 * @latest       11 Sep 2026
 * @brief        Optional construction adapter for explicit entity presentation groups.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#pragma once
#include "Components/ActorComponents/SeinEntityPresentationComponent.h"
#include "Components/SeinConstructionTypes.h"
#include "SeinConstructionRenderComponent.generated.h"
class UStaticMesh;
class USkeletalMesh;
class UMaterialInterface;

/** Legacy asset representation, retained only for editor migration into explicit groups. */
UENUM()
enum class ESeinConstructionPlacementVisualType : uint8 { None, StaticMesh, SkeletalMesh, BlueprintActor };
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSeinConstructionStateChanged, ESeinConstructionState, OldState, ESeinConstructionState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSeinConstructionStageChanged, FGameplayTag, OldStage, FGameplayTag, NewStage);

/** Optional mapping from construction lifecycle to explicit visual groups. Never discovers or hides all meshes. */
UCLASS(Blueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent, DisplayName = "SeinARTS Construction Renderer"))
class SEINARTSCOREENTITY_API USeinConstructionRenderComponent : public USeinEntityPresentationComponent
{
	GENERATED_BODY()
public:
	USeinConstructionRenderComponent();
	/** Group shown when construction is complete or the entity has no construction data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS")
	FName FinishedGroup = TEXT("Finished");
	/** Group shown while a construction job is unfinished. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS")
	FName ConstructionGroup = TEXT("Construction");
	/** Ordered lifecycle transitions. Initial binding synchronizes silently; completion fires before its group hides. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS")
	FSeinConstructionStateChanged OnConstructionStateChanged;
	/** Ordered designer-stage transitions. Stage tags do not prescribe a mesh or animation. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS")
	FSeinConstructionStageChanged OnConstructionStageChanged;
	/** Refresh the group mapping from the bound entity, including after restoring a save. */
	virtual void RefreshPresentation_Implementation() override;
	/** Legacy refresh alias. New presentation code uses Refresh Entity Binding. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Construction", meta = (DeprecatedFunction, DeprecationMessage = "Use Refresh Entity Binding."))
	void RefreshConstructionState() { RefreshEntityBinding(); }
	void HandleVisualEvent(const FSeinVisualEvent& Event) { HandleBoundVisualEvent(Event); }

	// Serialized migration input only. Runtime presentation uses Groups exclusively.
	UPROPERTY()
	ESeinConstructionPlacementVisualType PlacementVisualType = ESeinConstructionPlacementVisualType::None;
	UPROPERTY()
	TObjectPtr<UStaticMesh> PlacementStaticMesh;
	UPROPERTY()
	TObjectPtr<USkeletalMesh> PlacementSkeletalMesh;
	UPROPERTY()
	TSubclassOf<AActor> PlacementBlueprint;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GroundStampDecal;
	UPROPERTY()
	FVector GroundStampDecalSize = FVector(256);
protected:
	virtual void HandleBoundVisualEvent(const FSeinVisualEvent& Event) override;
	virtual void BindingWillChange() override;
private:
	bool bHasState = false;
	ESeinConstructionState CurrentState = ESeinConstructionState::Complete;
	FGameplayTag CurrentStage;
	void ApplyState(ESeinConstructionState State);
};

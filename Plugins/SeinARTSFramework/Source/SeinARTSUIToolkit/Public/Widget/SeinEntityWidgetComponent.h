/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinEntityWidgetComponent.h
 * @author       RJ Macklem
 * @created      11 Sep 2026
 * @latest       11 Sep 2026
 * @brief        Binds a world widget to explicit entity context independently of visual actor hierarchy.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#pragma once
#include "Components/WidgetComponent.h"
#include "Core/SeinEntityHandle.h"
#include "SeinEntityWidgetComponent.generated.h"
class ASeinActor;
class USeinEntityBindingComponent;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSeinWidgetEntityContext, UUserWidget*, Widget, FSeinEntityHandle, Entity);
/** World widget with explicit entity context. Supports stable entity actors and optional managed visual actors. */
UCLASS(Blueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSUITOOLKIT_API USeinEntityWidgetComponent : public UWidgetComponent
{
	GENERATED_BODY()
public:
	USeinEntityWidgetComponent();
	/** Supply the entity actor explicitly, including before BeginPlay. None clears the widget. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI")
	void BindToEntityActor(ASeinActor* Actor);
	/** Current living entity, including for listeners attached after BeginPlay. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI")
	FSeinEntityHandle GetEntityHandle() const;
	/** Current entity actor, independent of widget or attachment hierarchy. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI")
	ASeinActor* GetEntityActor() const;
	/** Read and broadcast the current context again for a newly attached widget listener. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI")
	void InitializeWidgetContext();
	/** Receives widget context for arbitrary widget classes. Entity Widget subclasses are initialized automatically. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS")
	FSeinWidgetEntityContext OnWidgetEntityContext;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function) override;
private:
	UPROPERTY(Transient)
	TObjectPtr<USeinEntityBindingComponent> Binding;
	UPROPERTY(Transient)
	TWeakObjectPtr<ASeinActor> ExplicitActor;
	UPROPERTY(Transient)
	TWeakObjectPtr<UUserWidget> LastWidget;
	FSeinEntityHandle LastEntity;
	bool bExplicit = false;
	bool bEnding = false;
	uint32 ContextRevision = 0;
	void RefreshWidgetContextInternal(bool bBroadcast);
	UFUNCTION()
	void RefreshWidgetContext();
};

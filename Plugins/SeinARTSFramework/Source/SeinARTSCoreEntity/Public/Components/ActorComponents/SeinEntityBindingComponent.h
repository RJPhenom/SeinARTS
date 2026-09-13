/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinEntityBindingComponent.h
 * @author       RJ Macklem
 * @created      11 Sep 2026
 * @latest       11 Sep 2026
 * @brief        Provides explicit entity context and refresh notifications to arbitrary presentation actors.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/SeinEntityHandle.h"
#include "Events/SeinVisualEvent.h"
#include "SeinEntityBindingComponent.generated.h"
class ASeinActor;
class USeinEntityBridgeComponent;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSeinEntityBindingChanged, FSeinEntityHandle, Entity);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSeinEntityPresentationRefresh);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSeinBoundVisualEvent, const FSeinVisualEvent&, Event);

/** Entity context for any actor. A SeinActor binds to itself; other actors are bound explicitly by their creator. */
UCLASS(Blueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSCOREENTITY_API USeinEntityBindingComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	USeinEntityBindingComponent();
	/** Bind to an entity actor, or pass None to clear the context. Safe before BeginPlay and for pooled visuals. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Presentation")
	void BindToEntityActor(ASeinActor* Actor);
	/** Current living entity. An unbound or destroyed entity returns an invalid handle. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Presentation")
	FSeinEntityHandle GetEntityHandle() const;
	/** Actor that represents the bound entity. This does not depend on attachment hierarchy. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Presentation")
	ASeinActor* GetEntityActor() const;
	/** Re-read current entity data. Also called on initial binding and snapshot restoration, even when phase is unchanged. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Presentation")
	void RefreshEntityBinding();
	/** Context became ready, changed, or was cleared. Invalid Entity means release entity-specific UI data. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS")
	FSeinEntityBindingChanged OnEntityBindingChanged;
	/** Refresh presentation from current authoritative data. This is not a replay of gameplay transitions. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS")
	FSeinEntityPresentationRefresh OnPresentationRefresh;
	/** Ordered visual notifications for the bound entity. These are presentation callbacks, not simulation commands. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS")
	FSeinBoundVisualEvent OnBoundVisualEvent;
	/** Update visuals from current data, including initial binding, unbinding and save restoration. Default does nothing. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Presentation")
	void RefreshPresentation();
	virtual void RefreshPresentation_Implementation() {}
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
protected:
	/** Native presentation adapters may consume ordered events before forwarding them to Blueprint listeners. */
	UFUNCTION()
	virtual void HandleBoundVisualEvent(const FSeinVisualEvent& Event);
	virtual void BindingWillChange() {}
	bool IsBindingEnding() const { return bEnding; }
	uint32 GetBindingRevision() const { return BindingRevision; }
private:
	UPROPERTY(Transient)
	TWeakObjectPtr<ASeinActor> BoundActor;
	UPROPERTY(Transient)
	TWeakObjectPtr<USeinEntityBridgeComponent> Bridge;
	FSeinEntityHandle LastEntity;
	bool bExplicitBinding = false;
	bool bEnding = false;
	uint32 BindingRevision = 0;
	void Unsubscribe();
};

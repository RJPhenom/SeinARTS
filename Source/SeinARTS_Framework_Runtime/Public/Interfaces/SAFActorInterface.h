#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Structs/SAFOrder.h"
#include "Assets/SAFAsset.h"
#include "SAFActorInterface.generated.h"

class ASAFActor;
class ASAFFormationManager;

UINTERFACE(Blueprintable) 
class SEINARTS_FRAMEWORK_RUNTIME_API USAFActorInterface : public UInterface { GENERATED_BODY() };

/**
 * SAFActorInterface
 * 
 * The SAFActorInterface is the primary means of communication with assets in the
 * SeinARTS Framework. Implement this interface on a class to give it methods to 
 * seed with and get elements of a SAFAsset.
 */
class SEINARTS_FRAMEWORK_RUNTIME_API ISAFActorInterface {
	GENERATED_BODY()

public:

	// Asset API
	// ==================================================================================================
	/** Gets the data asset on an instance. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	USAFAsset* GetAsset() const;

	/** Sets the data asset on an instance and reinitializes. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	void SetAsset(USAFAsset* InAsset);

	/** Initializes this instance with the asset seed data. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	void InitAsset(USAFAsset* InAsset = nullptr, ASAFPlayerState* InOwner = nullptr);	

	// Ownership
	// ==================================================================================================
	/** Sets the owning player */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	void SetOwningPlayer(ASAFPlayerState* InOwner);

	/** Gets the owning player */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	ASAFPlayerState* GetOwningPlayer() const;

	// Identity
	// ==================================================================================================
	/** Gets asset display name. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	FText GetDisplayName() const;

	/** Gets data asset tooltip. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	FText GetTooltip() const;

	/** Gets asset icon, usually for UI purposes. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	UTexture2D* GetIcon() const;

	/** Gets asset portrait, usually for UI purposes. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	UTexture2D* GetPortrait() const;

	// Instance Getter
	// ==================================================================================================
	/** The desired runtime actor class this asset seeds (must be ASAFActor). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	TSubclassOf<AActor> GetInstanceClass() const;

	// Selection
	// ==================================================================================================
	/** Returns true if this is currently selectable. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	bool GetSelectable() const;

	/** Set the selectability state. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	void SetSelectable(bool bNewSelectable);

	/** Returns true if this is able to be selected via marquee. 
	 * (as opposed to click-select only). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	bool GetMultiSelectable() const;

	/** Set the multi-selectability state. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	void SetMultiSelectable(bool bNewMultiSelectable);

	/** Call to mark the asset as Selected. (marquee draw has been finalized/LMB released). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	bool Select(UPARAM(ref) AActor*& OutSelectedActor);

	/** Call to mark the asset as QueueSelected. (marquee is being drawn). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	bool QueueSelect(UPARAM(ref) AActor*& OutQueueSelectedActor);

	/** Call to mark the asset as deselected. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	void Deselect();

	/** Call to mark the asset as no-longer QueSelected (marquee area no longer overlaps it 
	 * during marquee draw). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	void DequeueSelect();

	// Pinging
	// ==================================================================================================
	/** Returns true if this is currently pingable. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	bool GetPingable() const;

	/** Set the pingability state. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	void SetPingable(bool bNewPingable);

	// Placement
	// ==================================================================================================
	/** Called when the SAFPlayerController attempts to place this when it is queued for 
	 * placement */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	void Place(FVector Location, FRotator Rotation);

	/** Called when the SAFPlayerController queues this for placement. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Asset Interface")
	void QueuePlace();

};

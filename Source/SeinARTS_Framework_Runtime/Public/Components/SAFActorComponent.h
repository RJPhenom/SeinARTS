#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interfaces/SAFActorInterface.h"
#include "SAFActorComponent.generated.h"

class ASAFPlayerState;
class USAFAsset;

// Events
// ===================================================================================================
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSAFSelectionChanged, bool, bIsSelected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSAFQueueSelectionChanged, bool, bIsQueueSelected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSAFOrderReceived, FSAFOrder, Order);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSAFOrderCompleted, AActor*, Actor, FSAFOrder, Order);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSAFPlaced, FVector, Location, FRotator, Rotation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSAFQueue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSAFSpawn);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSAFRestored);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSAFAttacked);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSAFDeath);

/**
 * USAFActorComponent
 * 
 * A composition-first replacement for SAFActor’s “framework state”.
 * Attach to any AActor/APawn to make it a selectable, ownable SAF object.
 * Implements SAFActorInterface so code can bind to component or host.
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(BlueprintSpawnableComponent))
class SEINARTS_FRAMEWORK_RUNTIME_API USAFActorComponent : public UActorComponent, public ISAFActorInterface {

	GENERATED_BODY()

public:

	USAFActorComponent();
	
	/** Asset reference (seed).
	 * 
	 * What asset is this actor referencing to provide its identity? A SeinARTS Framework actor component 
	 * always looks for an asset assignment to seed its full identity properties (name, UI icons, tooltip...).
	 * 
	 * You should be setting this in either:
	 * 	1) In-editor, for actors who start in-world on level load (be sure to also assign team and player); or
	 * 	2) In the Initter, passing in a TSoftObjectPtr asset reference to seed this component as.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Replicated, Category="SeinARTS|Data")
	TSoftObjectPtr<USAFAsset> Asset = nullptr;

	/** Owning player (server-auth, replicated) 
	 * 
	 * The owning player is going to be critical for deciding selection (and formation, if units are involved)
	 * outcomes. This should never truly be set manually, but should be either:
	 * 
	 * 1) Resolved by the Initter, reading PlayerID and TeamID and finding the matching ASAFPlayerState; or
	 * 2) Passed in to the Initter directly, as an arg.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, ReplicatedUsing=OnRep_OwningPlayer, Category="SeinARTS|Data")
	TObjectPtr<ASAFPlayerState> OwningPlayer = nullptr;

	// Asset Interface
	// =================================================================================================================================
	virtual USAFAsset*			GetAsset_Implementation() const;
	virtual void				SetAsset_Implementation(USAFAsset* InAsset);
	virtual void				InitAsset_Implementation(USAFAsset* InAsset, ASAFPlayerState* InOwner);

	virtual ASAFPlayerState*	GetOwningPlayer_Implementation() const override 			{ return OwningPlayer; }
	virtual void				SetOwningPlayer_Implementation(ASAFPlayerState* InOwner);

	virtual FText				GetDisplayName_Implementation() const;
	virtual UTexture2D*			GetIcon_Implementation() const;
	virtual UTexture2D*			GetPortrait_Implementation() const;

	virtual bool				GetSelectable_Implementation() const 						{ return bSelectable; }
	virtual void				SetSelectable_Implementation(bool bNewSelectable) 			{ bSelectable = bNewSelectable; }
	virtual bool				GetMultiSelectable_Implementation() const 					{ return bMultiSelectable; }
	virtual void				SetMultiSelectable_Implementation(bool bNewMultiSelectable) { bMultiSelectable = bNewMultiSelectable; }
	virtual bool				Select_Implementation(AActor*& OutSelectedActor);
	virtual bool				QueueSelect_Implementation(AActor*& OutQueueSelectedActor);
	virtual void				Deselect_Implementation();
	virtual void				DequeueSelect_Implementation();

	virtual bool				GetPingable_Implementation() const 							{ return bPingable; }
	virtual void				SetPingable_Implementation(bool bNewPingable) 				{ bPingable = bNewPingable; }

	virtual void				Place_Implementation(FVector Location, FRotator Rotation);
	virtual void				QueuePlace_Implementation() {}

	// Blueprint API Helper
	// =============================================================================
	UFUNCTION(BlueprintCallable, Category="SeinARTS|ActorComponent")
	static USAFActorComponent* FindSeinARTSComponentOn(AActor* Actor);

	// Team / Player Design-time Settings
	// =====================================================================================================================
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="SeinARTS|Data") 				int32 InitTeamID 		= 0;
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="SeinARTS|Data") 				int32 InitPlayerID 		= 0;

	// Boolean Flags
	// =====================================================================================================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="SeinARTS|Selection") 	bool bSelectable 		= true;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SeinARTS|Selection") 			bool bMultiSelectable 	= true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Selection") 				bool bIsQueueSelected 	= false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Selection") 				bool bIsSelected 		= false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="SeinARTS|Pings") 		bool bPingable 			= true;

	// Events
	// =============================================================================
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Selection")
	FOnSAFSelectionChanged OnSelectionChanged;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Selection")
	FOnSAFQueueSelectionChanged OnQueueSelectionChanged;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Orders")
	FOnSAFOrderReceived	OnOrderReceived;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Orders")
	FOnSAFOrderCompleted OnOrderCompleted;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Placement")
	FOnSAFPlaced OnPlaced;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Lifecycle")
	FOnSAFQueue OnQueue;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Lifecycle")
	FOnSAFSpawn OnSpawn;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Lifecycle")
	FOnSAFRestored OnRestored;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Lifecycle")
	FOnSAFAttacked OnAttacked;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Lifecycle")
	FOnSAFDeath OnDeath;

	// Replication
	// =================================================================================================
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:

	UFUNCTION() void OnRep_OwningPlayer();
};

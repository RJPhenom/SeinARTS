#pragma once

#include "CoreMinimal.h"
#include "Classes/SAFFormationManager.h"
#include "Interfaces/SAFActorInterface.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerState.h"
#include "Resolvers/SAFAssetResolver.h"
#include "Resolvers/SAFSpacingResolver.h"
#include "Structs/SAFOrder.h"
#include "SAFActor.generated.h"

class ASAFPlayerState;
class ASAFFormationManager;
class USAFAssetData;

// Event Delegates
// =======================================================================================
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectionChanged, bool, bIsSelected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQueueSelectionChanged, bool, bIsQueueSelected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnOrderReceived, FSAFOrder, Order);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnOrderCompleted, AActor*, Actor, FSAFOrder, Order);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPlaced, FVector, Location, FRotator, Rotation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnQueue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSpawn);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRestored);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAttacked);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDeath);

/**
 * SAFActor
 * 
 * Abstract class which all concrete classes inherit from under the SeinARTS Framework. 
 * Implements the SAFActorInterface by default.
 * 
 * For any object that represents a SAFAsset, and should be able to report on its SAFAsset 
 * identity, you should be subclassing it with this base class.
 */
UCLASS(Abstract, ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Framework Class"))
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFActor : public AActor, 
  public ISAFActorInterface {

	GENERATED_BODY()

public:

	ASAFActor();

	// Event Bindings
	// =======================================================================================
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Selection")
	FOnSelectionChanged OnSelectionChanged;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Selection")
	FOnSelectionChanged OnQueueSelectionChanged;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Orders")
	FOnOrderReceived OnOrderReceived;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Orders")
	FOnOrderCompleted OnOrderCompleted;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Placement")
	FOnPlaced OnPlaced;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Lifecycle")
	FOnQueue OnQueue;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Lifecycle")
	FOnSpawn OnSpawn;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Lifecycle")
	FOnRestored OnRestored;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Lifecycle")
	FOnAttacked OnAttacked;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Lifecycle")
	FOnDeath OnDeath;

	// Asset Interface / API
	// ===============================================================================================================================================================
	virtual USAFAsset*      		GetAsset_Implementation() const 												 { return SAFAssetResolver::ResolveAsset(Asset); }
	virtual void            		SetAsset_Implementation(USAFAsset* InAsset);
	virtual void            		InitFromAsset_Implementation(USAFAsset* InAsset, ASAFPlayerState* InOwner, bool bReinitialize);

	virtual ASAFPlayerState*		GetOwningPlayer_Implementation() const 																{ return OwningPlayer.Get(); }
	virtual void            		SetOwningPlayer_Implementation(ASAFPlayerState* InOwner);

	virtual FText           		GetDisplayName_Implementation() const;
	virtual UTexture2D*     		GetIcon_Implementation() const;
	virtual UTexture2D*     		GetPortrait_Implementation() const;															

	virtual bool            		GetSelectable_Implementation() const 																		{ return bSelectable; }
	virtual void            		SetSelectable_Implementation(bool bNewSelectable) 												  { bSelectable = bNewSelectable; }
	virtual bool            		GetMultiSelectable_Implementation() const 															   { return bMultiSelectable; }
	virtual void            		SetMultiSelectable_Implementation(bool bNewMultiSelectable) 							{ bMultiSelectable = bNewMultiSelectable; }
	virtual bool            		Select_Implementation(AActor*& OutSelectedActor);
	virtual bool            		QueueSelect_Implementation(AActor*& OutQueueSelectedActor);
	virtual void            		Deselect_Implementation();
	virtual void            		DequeueSelect_Implementation();

	virtual bool            		GetPingable_Implementation() const 																			  { return bPingable; }
	virtual void            		SetPingable_Implementation(bool bNewPingable) 														  { bPingable = bNewPingable; }

	virtual void            		Place_Implementation(FVector Location, FRotator Rotation);
	virtual void            		QueuePlace_Implementation() {}

	/** The SAFAsset this class was seeded from. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Replicated, Category="SeinARTS|Data")
	TSoftObjectPtr<USAFAsset> Asset = nullptr;

	// Ownership
	// =========================================================================================================================
	/** Use to assign the desired team of this instance for actors placed at design-time. 
	 * 	Examples: 
	 * 		Team 1 / Player 1 assigns to Players[0] on Teams[0] in the GameState Teams array 
	 * 		Team 1 / Player 0 will cause this instance to be assigned neutral, same as 0 / 0 
	 * 		Team 0 / Player 1 will cause this instance to be assigned neutral, same as 0 / 0 
	 * 		Team 0 / Player 0 is a neutral team/player  (Unit will be 'unowned'). */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="SeinARTS|Data")
	int32 InitTeamID = 0;

	/** Use to assign the desired owner for actors placed at design-time. 
	 * Note: this is NOT the same as the PlayerID of the APlayerState of the owner as it is indexed in the Players array.
	 * This is for design purposes, so designers can create levels with placed actors starting as "Team 1 Player 1, 
	 * Team 1 Player 2, Team 2 Player1, ..." -> this will then run a resolver in InitUnit to grab the correct player from 
	 * the SAFGameState's Team's 'Player[i]'. See the property in SAFGameState for a better understanding. 
	 * 	Examples: 
	 * 		Team 1 / Player 1 assigns to Players[0] on Teams[0] in the GameState Teams array 
	 * 		Team 1 / Player 0 will cause this instance to be assigned neutral, same as 0 / 0 
	 * 		Team 0 / Player 1 will cause this instance to be assigned neutral, same as 0 / 0 
	 * 		Team 0 / Player 0 is a neutral team/player  (Unit will be 'unowned'). */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="SeinARTS|Data")
	int32 InitPlayerID = 0;

	/** Owning player (set on spawn by PC, replicated for game logic) */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, ReplicatedUsing=OnRep_OwningPlayer, Category="SeinARTS|Data")
	TObjectPtr<ASAFPlayerState> OwningPlayer = nullptr;
	
	// Selection
	// =======================================================================================
	/** Toggles if the actor is able to be selected under the default selection flow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="SeinARTS|Selection")
	bool bSelectable = true;

	/** Controls if this actor can be selected alongside other actors, or only single-selected.
	Useful for distinguishing assets eligible for marquee select (units) against ones that
	are not (structures, environment assets). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SeinARTS|Selection")
	bool bMultiSelectable = true;

	/** If the actor is queued for selection (i.e. under the marquee, but not yet selected) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Selection")
	bool bIsQueueSelected = false;

	/** If the actor is actively selected (not just queued) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Selection")
	bool bIsSelected = false;
	
	// Pings
	// =======================================================================================
	/** Toggles if the actor is able to be pinged under the default ping flow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="SeinARTS|Pings")
	bool bPingable = true;

	// Replication
	// =======================================================================================
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const;

protected:

	/** Flags if an init has been applied. 
	 * If you want to re-initialize, pass 
	 * bReinitialize=true to the InitAsset 
	 * call. */ bool bInitialized = false;

	UFUNCTION()	void OnRep_OwningPlayer();

};
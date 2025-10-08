#pragma once

#include "CoreMinimal.h"
#include "Assets/Units/SAFSquadMemberAsset.h"
#include "Classes/Units/SAFSquad.h"
#include "Enums/SAFCoverTypes.h"
#include "GameFramework/Character.h"
#include "Interfaces/SAFActorInterface.h"
#include "Interfaces/Units/SAFSquadMemberInterface.h"
#include "Interfaces/SAFCoverInterface.h"
#include "Resolvers/SAFAssetResolver.h"
#include "SAFSquadMember.generated.h"

class UCapsuleComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class USAFInfantryMovementComponent;
class ASAFSquad;
class ASAFCover;
class ASAFPlayerState;
class USAFSquadMemberAsset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSquadChanged, AActor*, NewSquad);

/* 
* ASAFSquadMember 
*
* The class that represents a single member of a SAFSquad.
* (Converted to APawn + USAFInfantryMovement.)
*/
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS SquadMember Unit"))
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFSquadMember : public ACharacter, 
	public ISAFActorInterface, 
	public ISAFSquadMemberInterface, 
	public ISAFCoverInterface {
	GENERATED_BODY()

public:

	ASAFSquadMember();
	USAFSquadMemberAsset* GetSquadMemberAsset() { return SAFAssetResolver::ResolveAsset<USAFSquadMemberAsset>(SquadMemberAsset); }

	// Components
	// ==================================================================================================
	/** Capsule component for infantry units */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SeinARTS|Components")
	TObjectPtr<UCapsuleComponent> InfantryCapsuleComponent = nullptr;

	/** Skeletal mesh component for infantry units */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SeinARTS|Components")
	TObjectPtr<USkeletalMeshComponent> InfantryMeshComponent = nullptr;

	/** Movement component for infantry units */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SeinARTS|Components")
	TObjectPtr<USAFInfantryMovementComponent> InfantryMovementComponent = nullptr;

	/** Event OnSquadChanged binding */
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Selection")
	FOnSquadChanged OnSquadChanged;

	// Asset Interface Overrides
	// ====================================================================================================
	virtual USAFAsset* 				GetAsset_Implementation() const;
	virtual void 					InitAsset_Implementation(USAFAsset* InAsset, ASAFPlayerState* InOwner);

	virtual ASAFPlayerState* 		GetOwningPlayer_Implementation() const;

	virtual bool 					GetMultiSelectable_Implementation() const;
	virtual void 					SetMultiSelectable_Implementation(bool bNewMultiSelectable);
	virtual bool 					Select_Implementation(AActor*& OutSelectedActor);
	virtual bool 					QueueSelect_Implementation(AActor*& OutQueueSelectedActor);
	virtual void 					Deselect_Implementation();
	virtual void 					DequeueSelect_Implementation();

	// Cover Interface
	// ======================================================================================================================
	virtual void 					EnterCover_Implementation(AActor* CoverObject, ESAFCoverType CoverType) 		override;
	virtual void 					ExitCover_Implementation(AActor* CoverObject, ESAFCoverType CoverType) 			override;

	// Squad Member Interface
	// ======================================================================================================================
	virtual void 					InitSquadMember_Implementation(USAFSquadMemberAsset* InAsset, ASAFSquad* InSquad);
	virtual ASAFSquad* 				GetSquad_Implementation() const { return Squad.Get(); }														
	virtual void 					SetSquad_Implementation(ASAFSquad* InSquad);
	virtual bool 					HasSquad_Implementation() const { return Squad && !Squad->IsActorBeingDestroyed(); }	
	virtual USAFSquadMemberAsset* 	GetSquadMemberAsset_Implementation() const { return SquadMemberAsset.Get(); }

	// Squad Member Properties
	// ==================================================================================================
	/** Per-instance data asset that defines this member's default properties */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing=OnRep_SquadMemberAsset, Category="SeinARTS|Data")
	TObjectPtr<USAFSquadMemberAsset> SquadMemberAsset = nullptr;	

	/** Owning/Managing Squad reference. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing=OnRep_Squad, Category="SeinARTS|Squad")
	TObjectPtr<ASAFSquad> Squad;

	/** Container for all actors this SquadMember is currently inside the cover collider of. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category="SeinARTS|Cover")
	TArray<TObjectPtr<AActor>> CoverActors; 

protected:

	virtual void BeginPlay() override;
	virtual void PostInitializeComponents() override; 

	// Internal Helpers
	// =======================================
	void ApplyVisuals();

	// Replication
	// ==================================================================================================
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UFUNCTION()	void OnRep_Squad();
	UFUNCTION()	void OnRep_SquadMemberAsset();

};

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "Assets/Units/SAFUnitAsset.h"
#include "Classes/SAFActor.h"
#include "Interfaces/Units/SAFUnitInterface.h"
#include "GameFramework/Actor.h"
#include "Structs/SAFOrder.h"
#include "Structs/SAFResources.h"
#include "SAFUnit.generated.h"

class APawn;
class UActorChannel;
class UAbilitySystemComponent;
class UAttributeSet;
class UGameplayEffect;
class USAFProductionComponent;
class USphereComponent;
class FOutBunch;
struct FReplicationFlags;

/**
 * SAFUnit
 * 
 * Abstract Unit class which all concrete unit subclasses inherit from under the
 * SeinARTS Framework. Implements the SAFUnitInterface by default.
 * 
 * Units in the SeinARTS Framework communicate using interfaces and event dispatchers, 
 * some of which are setup as defaults in this class. Wiring can be done in Blueprints.
 * Blueprints with default wirings have been provided to you in the plugin contents folder. 
 * For quick prototyping, it is recommended to subclass these blueprints for quick iteration. 
 * For support, please reach out via the Phenom Studios discord server.
 */
UCLASS(Abstract, ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Unit"))
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFUnit : public ASAFActor,
	public ISAFUnitInterface,
	public IAbilitySystemInterface {

	GENERATED_BODY()

public:

	ASAFUnit();
	USAFUnitAsset* GetUnitAsset() { return Cast<USAFUnitAsset>(SAFAssetResolver::ResolveAsset(Asset)); }
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystem; }

	// Asset Interface Overrides
	// ==========================================================================================================================================
	virtual void 					InitFromAsset_Implementation(USAFAsset* InAsset, ASAFPlayerState* InOwner, bool bReinitialize) 		override;
	virtual void 					SetAsset_Implementation(USAFAsset* InAsset) 											 			override;

	// Unit Interface / API
	// ==========================================================================================================================================
	virtual void 					AttachToPawn_Implementation(APawn* Pawn);
	virtual void 					DetachFromPawn_Implementation();
	virtual void 					OnAttachedPawnDestroyed_Implementation(AActor* DestroyedPawn);

	virtual ASAFFormationManager*	GetFormation_Implementation() const										   { return CurrentFormation.Get(); }
	virtual void            		SetFormation_Implementation(ASAFFormationManager* InFormation)			  { CurrentFormation = InFormation; }
	virtual float           		GetFormationSpacing_Implementation() const;	

	virtual bool            		GetOrderable_Implementation() const													   { return bOrderable; }
	virtual void            		SetOrderable_Implementation(bool bNewOrderable)								  { bOrderable = bNewOrderable; }
	virtual bool            		Order_Implementation(FSAFOrder Order);
	virtual bool            		NotifyOrderCompleted_Implementation();
	virtual void            		GetOrderTags_Implementation(TArray<FGameplayTag>& OutTags) const;

	/** Proxy to bind delegates */
	UFUNCTION() void OnAttachedPawnDestroyedProxy(AActor* DestroyedPawn);

	/** A reference to this Unit Actor's pawn representation. In the SeinARTS Framework some units represented by and attached 
	 * to a pawn rather than using its own mesh 	component and interface. This is to keep some units lightweight on the server.
	 * (e.g. Buildings, terrain objects, etc...) */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, ReplicatedUsing=OnRep_AttachedPawn, Category="SeinARTS|Movement")
	TObjectPtr<APawn> AttachedPawn = nullptr;

	/** Toggles if the actor is able to be ordered under the default order flow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="SeinARTS|Orders")
	bool bOrderable = true;

	/** Stores the current order */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="SeinARTS|Orders")
	FSAFOrder CurrentOrder;
	
	/** Toggles the actor able to be selected under the default selection flow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_CurrentFormation, Category="SeinARTS|Formation")
	TObjectPtr<ASAFFormationManager> CurrentFormation;

	/** The radius around this actor has that should be clear when positioning other actors
	in a formation. (e.g. if this and the other actor are both set to 50.f, then the actors
	will be 100.f apart from each other ***in addition to their bounds extent***) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SeinARTS|Movement")
	float FormationSpacing = 50.f;

	// Production Component
	// ==================================================================================================
	/** A reference to the SAFProductionComponent on this unit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="SeinARTS|Abilities")
	TObjectPtr<USAFProductionComponent> ProductionComponent = nullptr;

protected:

	virtual bool ReplicateSubobjects(class UActorChannel* Channel, class FOutBunch* Bunch, FReplicationFlags* RepFlags) override;
	virtual void GetSubobjectsWithStableNamesForNetworking(TArray<UObject*>& Objs) override;
	virtual void OnSubobjectCreatedFromReplication(UObject* NewSubobject) override;

	// GAS Helpers / API
	// ==================================================================================================
	/** A reference to the ASC on this unit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SeinARTS|Abilities")
	TObjectPtr<UAbilitySystemComponent> AbilitySystem = nullptr;

	/** Initializes the ability system component */
	void InitAbilitySystem();

	/** Gives abilities on begin play based on the assigned asset*/
	void GiveAbilitiesFromAsset();

	/** Gives attributes from the SAFAttributes set on begin play, based on the assigned 
	 * AttributeRow set on the asset. */
	void GiveAttributesFromAsset();

	/** Gives tags on begin play based on the assigned asset */
	void GiveTagsFromAsset();

	/** Applies GAS startup effects */
	void ApplyStartupEffects();

	/** Private helper for initializing SAFUnitAttributes. */
	void InitDefaultAttributes();

	/** Private helper for intializing designer attributes. */
	void InitDesignerAttributes();
		
	// Replication
	// ==================================================================================================
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UFUNCTION() void OnRep_AttachedPawn();
	UFUNCTION()	void OnRep_CurrentFormation();

};

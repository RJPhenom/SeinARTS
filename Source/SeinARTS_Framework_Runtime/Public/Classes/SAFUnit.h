#pragma once

#include "CoreMinimal.h"
#include "Classes/SAFActor.h"
#include "AbilitySystemInterface.h"
#include "Assets/SAFUnitAsset.h"
#include "Enums/SAFCoverTypes.h"
#include "Interfaces/SAFUnitInterface.h"
#include "GameFramework/Actor.h"
#include "Structs/SAFOrder.h"
#include "Structs/SAFResourceBundle.h"
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
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Unit"))
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFUnit : public ASAFActor,
	public ISAFUnitInterface,
	public IAbilitySystemInterface {

	GENERATED_BODY()

public:

	ASAFUnit();
	USAFUnitAsset* GetUnitAsset() const { return Cast<USAFUnitAsset>(SAFAssetResolver::ResolveAsset(Asset)); }
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystem; }

	/** Gets the technology component for this unit. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Technology")
	class USAFTechnologyComponent* GetTechnologyComponent() const { return TechnologyComponent; }

	// Actor Interface Overrides
	// ==========================================================================================================================================
	virtual void 					InitFromAsset_Implementation(USAFAsset* InAsset, ASAFPlayerState* InOwner, bool bReinitialize) 		override;
	virtual void 					SetAsset_Implementation(USAFAsset* InAsset) 											 			override;

	// Unit Interface / API
	// ==========================================================================================================================================
	virtual void 					InitAsPawn_Implementation();
	virtual void 					InitAsPawns_Implementation();
	virtual void 					AttachToPawn_Implementation(APawn* Pawn);
	virtual void 					DetachFromPawn_Implementation();
	virtual void 					OnAttachedPawnDestroyed_Implementation(AActor* DestroyedPawn);

	virtual bool 					GetSquadMode_Implementation() const 												   { return bSquadMode; }
	virtual void 					SetSquadMode_Implementation(bool bInSquadMode) 				 				   { bSquadMode = bInSquadMode; }
	virtual void 					AddPawnToSquad_Implementation(APawn* Pawn, bool bIsNewLeader = false);
	virtual bool 					RemovePawnFromSquad_Implementation(APawn* Pawn);
	virtual APawn* 					GetSquadLeader_Implementation() const 										   { return AttachedPawn.Get(); }
	virtual void 					SetSquadLeader_Implementation(APawn* InSquadLeader);
	virtual APawn* 					FindNextSquadLeader_Implementation();
	virtual APawn* 					GetFrontmostPawnInSquad_Implementation() const;
	virtual void 					ReinitPositions_Implementation(const TArray<FVector>& InPositions);
	virtual void 					InvertPositions_Implementation();
	virtual TArray<FVector> 		GetPositionsAtPoint_Implementation(const FVector& Point, bool bTriggerInversion);
	virtual TArray<FVector> 		GetCoverPositionsAtPoint_Implementation(const FVector& Point, bool bTriggerInversion);
	virtual void 					CullSquadUnit_Implementation();
	virtual void 					CullSquadUnitAndPawns_Implementation();

	virtual bool            		UsesCover_Implementation() const			  { return GetUnitAsset() ? GetUnitAsset()->bUsesCover : false; }
	virtual ESAFCoverType   		GetCurrentCover_Implementation() const												 { return CurrentCover; }

	virtual ASAFFormationManager*	GetFormation_Implementation() const										   { return CurrentFormation.Get(); }
	virtual void            		SetFormation_Implementation(ASAFFormationManager* InFormation)			  { CurrentFormation = InFormation; }
	virtual float           		GetFormationSpacing_Implementation() const;	

	virtual bool            		GetOrderable_Implementation() const													   { return bOrderable; }
	virtual void            		SetOrderable_Implementation(bool bNewOrderable)								  { bOrderable = bNewOrderable; }
	virtual bool            		Order_Implementation(FSAFOrder Order);
	virtual bool            		NotifyOrderCompleted_Implementation();
	virtual void            		GetOrderTags_Implementation(TArray<FGameplayTag>& OutTags) const;
	
	/** Proxy to bind delegates */
	UFUNCTION() void OnAttachedPawnDestroyedProxy(AActor* DestroyedPawn)
	{ ISAFUnitInterface::Execute_OnAttachedPawnDestroyed(this, DestroyedPawn); }

	/** A reference to this Unit parent pawn. Units that have pawn representation usually attach to the (or one of the) 
	 * pawns that represent it for movement with their representations. NOTE: if this unit is a squad, this will be the
	 * squad leader. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, ReplicatedUsing=OnRep_AttachedPawn, Category="SeinARTS")
	TObjectPtr<APawn> AttachedPawn = nullptr;
	
	// Squad Mode Properties
	// ==============================================================================================================
	/** Boolean flag to indicate if the unit is in SquadMode. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Replicated, Category="SeinARTS")
	bool bSquadMode = false;

	/** Contains references to the instances of the all the pawns representing this unit while in SquadMode. 
	 * Not used if not in SquadMode. 
	 * 
	 * NOTE: this includes the AttachedPawn reference, iterating over this array will iterate over the 
	 * primary pawn as well. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, ReplicatedUsing=OnRep_SquadPawns, Category="SeinARTS|Squad", 
	meta=(EditCondition="bSquadMode", EditConditionHides))
	TArray<TObjectPtr<APawn>> SquadPawns;

	/** Ordered list of positions for SquadPawns when in SquadMode. Not used if not in SquadMode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="SeinARTS|Squad", 
	meta=(EditCondition="bSquadMode", EditConditionHides))
	TArray<FVector> Positions;

	// Orders & Formation
	// ==============================================================================================================
	/** Toggles if the actor is able to be ordered under the default order flow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="SeinARTS")
	bool bOrderable = true;

	/** Stores the current order */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="SeinARTS")
	FSAFOrder CurrentOrder;
	
	/** Toggles the actor able to be selected under the default selection flow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_CurrentFormation, Category="SeinARTS")
	TObjectPtr<ASAFFormationManager> CurrentFormation;

	/** The radius around this actor has that should be clear when positioning other actors
	in a formation. (e.g. if this and the other actor are both set to 50.f, then the actors
	will be 100.f apart from each other ***in addition to their bounds extent***) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Replicated, Category="SeinARTS")
	float FormationSpacing = 50.f;

	// Cover
	// ==============================================================================================================
	/** Tracks the highest state of current cover, for UI/UX purposes.
	(actual cover value for gameplay logic is tracked on individual squad members). */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_CurrentCover, Category="SeinARTS|Cover")
	ESAFCoverType CurrentCover = ESAFCoverType::None;

	/** The default query radius around a point that this squad's members will use to check for or find cover. 
	Also used when querying points for cover during navigation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="SeinARTS|Cover")
	float CoverSearchRadius = 50.f;

#if WITH_EDITOR
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void UpdateEditorPreview() override;
#endif

	virtual void PreInitializeComponents() override;

protected:

	// Production Component
	// ==============================================================================================================
	/** A reference to the SAFProductionComponent on this unit. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category="SeinARTS", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USAFProductionComponent> ProductionComponent = nullptr;

	// Technology Component
	// ==============================================================================================================
	/** A reference to the SAFTechnologyComponent on this unit for receiving technology modifications. */
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS", meta=(AllowPrivateAccess="true"))
	TObjectPtr<class USAFTechnologyComponent> TechnologyComponent = nullptr;

	// GAS Helpers / API
	// ==================================================================================================
	/** A reference to the ASC on this unit. */
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS", meta=(AllowPrivateAccess="true"))
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
	// ==========================================================================================================================
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UFUNCTION() void OnRep_AttachedPawn();
	UFUNCTION() void OnRep_SquadPawns();
	UFUNCTION()	void OnRep_CurrentFormation();
	UFUNCTION() void OnRep_CurrentCover();

};

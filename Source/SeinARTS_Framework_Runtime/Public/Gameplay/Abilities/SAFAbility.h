#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Structs/SAFOrder.h"
#include "Structs/SAFResourceBundle.h"
#include "SAFAbility.generated.h"

class ASAFPlayerState;

/**
 * USAFAbility
 * 
 * Defines the structure of Gameplay Abilities under the SeinARTS Framework. SeinARTS
 * uses the Unreal Gameplay Ability System (GAS) and Gameplay Tags to assign units
 * abilities at the data asset-level. Unit identity is data-driven, so you can quickly
 * define new units and abilities (using Ability Blueprints) without extending and 
 * rebuilding unit subclasses.
 * 
 * Supports optional player resource costs (wood, gold, etc.) in addition to standard
 * GAS costs (mana, stamina). Use ResourceCost for strategic player resources and
 * CostGameplayEffectClass for unit-local resources.
 */
UCLASS(Abstract)
class SEINARTS_FRAMEWORK_RUNTIME_API USAFAbility : public UGameplayAbility {

	GENERATED_BODY()

public:

	/** Constructor sets defaults on replication policies for SAFAbility GAS Blueprints */
	USAFAbility(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Order payload for the ability. */
	UPROPERTY(BlueprintReadWrite, Category="SeinARTS")
	FSAFOrder OrderPayload;

	/** Sets the order payload for the ability. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS") 
	virtual void SetOrderPayload(const FSAFOrder& In) { OrderPayload = In; }

protected:

	// Resource Cost System
	// ==================================================================================================
	/** Whether to check/deduct player resources on activation (disable for free abilities) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resource Cost")
	bool bRequiresPlayerResources = false;

	/** The player resources required to activate this ability (wood, gold, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resource Cost", 
	meta=(EditCondition="bRequiresPlayerResources", EditConditionHides, ShowOnlyInnerProperties))
	FSAFResourceBundle ResourceCost;

	// GAS Overrides
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const override;
	
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	// Blueprint Events
	/** Called when player resource cost check succeeds and ability can activate */
	UFUNCTION(BlueprintImplementableEvent, Category="Resource Cost", meta=(DisplayName="On Player Resource Cost Paid"))
	void ReceivePlayerResourceCostPaid();

	/** Called when player resource cost check fails */
	UFUNCTION(BlueprintImplementableEvent, Category="Resource Cost", meta=(DisplayName="On Insufficient Player Resources"))
	void ReceiveInsufficientPlayerResources(const FSAFResourceBundle& Required, const FSAFResourceBundle& Available);

	// Helper Methods
	/** Get the player state that owns this ability */
	UFUNCTION(BlueprintPure, Category="Resource Cost")
	ASAFPlayerState* GetOwningPlayerState() const;

	/** Get the effective player resource cost (allows for dynamic cost calculation in subclasses) */
	UFUNCTION(BlueprintPure, Category="Resource Cost")
	virtual FSAFResourceBundle GetEffectivePlayerResourceCost() const { return ResourceCost; }
	
};

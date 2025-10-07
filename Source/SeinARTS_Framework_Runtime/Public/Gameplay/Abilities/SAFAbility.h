#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Structs/SAFOrder.h"
#include "SAFAbility.generated.h"

/**
 * SAFAbility
 * 
 * Defines the structure of Gameplay Abilities under the SeinARTS Framework. SeinARTS
 * uses the Unreal Gameplay Ability System (GAS) and Gameplay Tags to assign units
 * abilities at the data asset-level. Unit identity is data-driven, so you can quickly
 * define new units and abilities (using Ability Blueprints) without extending and 
 * rebuilding unit subclasses.
 */
UCLASS(Abstract)
class SEINARTS_FRAMEWORK_RUNTIME_API USAFAbility : public UGameplayAbility {

	GENERATED_BODY()

public:

	/** Constructor sets defaults on replication policies for SAFAbility GAS Blueprints */
	USAFAbility(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

  /** Order payload for the ability. */
	UPROPERTY(BlueprintReadWrite) FSAFOrder OrderPayload;

  /** Sets the order payload for the ability. */
	UFUNCTION(BlueprintCallable) virtual void SetOrderPayload(const FSAFOrder& In) { OrderPayload = In; }
	
};

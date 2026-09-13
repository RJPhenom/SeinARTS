/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinPlayerAbilityBPFL.h
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Player-controller authority for ability requests with inputs.
 *
 * @disclaimer   Generated with assistance from an AI language model.
 */
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Abilities/SeinAbilityActivationInputs.h"
#include "Core/SeinEntityHandle.h"
#include "Types/Vector.h"
#include "GameplayTagContainer.h"
#include "SeinPlayerAbilityBPFL.generated.h"
class ASeinPlayerController;

UCLASS(meta = (DisplayName = "SeinARTS Player Ability Library"))
class SEINARTSFRAMEWORK_API USeinPlayerAbilityBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Submit an ability request from this local player to one exact entity.
	 *  True means submitted; authority and gameplay checks run on execution.
	 *  Current selection does not change the recipient. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability", meta = (DisplayName = "Activate Ability With Inputs", AutoCreateRefTerm = "Inputs"))
	static bool SubmitAbility(ASeinPlayerController* PC, FSeinEntityHandle Entity,
		FGameplayTag AbilityTag, FSeinEntityHandle TargetEntity, FFixedVector TargetLocation,
		const FSeinAbilityActivationInputs& Inputs);
};

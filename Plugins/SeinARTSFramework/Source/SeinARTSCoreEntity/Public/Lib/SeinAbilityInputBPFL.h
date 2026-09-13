/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinAbilityInputBPFL.h
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Blueprint and native command submission with activation inputs.
 *
 * @disclaimer   Generated with assistance from an AI language model.
 */
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Abilities/SeinAbilityActivationInputs.h"
#include "Core/SeinEntityHandle.h"
#include "GameplayTagContainer.h"
#include "Types/Vector.h"
#include "SeinAbilityInputBPFL.generated.h"
class USeinAbility;

UCLASS(meta = (DisplayName = "SeinARTS Ability Input Library", SeinDeterministic))
class SEINARTSCOREENTITY_API USeinAbilityInputBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Chain a request from an authorized simulation callback for the next tick.
	 *  Uses the entity's owner and preserves the supplied input values. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability", meta = (WorldContext = "WorldContextObject", DisplayName = "Issue Ability With Inputs", AutoCreateRefTerm = "Inputs"))
	static bool IssueAbility(const UObject* WorldContextObject, FSeinEntityHandle Entity,
		FGameplayTag AbilityTag, FSeinEntityHandle TargetEntity, FFixedVector TargetLocation,
		const FSeinAbilityActivationInputs& Inputs);

	/** Capture an ability's default activation values. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Ability", meta = (BlueprintInternalUseOnly = "true"))
	static FSeinAbilityActivationInputs MakeInputs(TSubclassOf<USeinAbility> AbilityClass);

	/** Internal typed pin setter. Failed construction remains invalid. */
	UFUNCTION(BlueprintPure, CustomThunk, Category = "SeinARTS|Ability", meta = (AutoCreateRefTerm = "Value", CustomStructureParam = "Value", BlueprintInternalUseOnly = "true"))
	static FSeinAbilityActivationInputs SetInput(const FSeinAbilityActivationInputs& Inputs,
		TSubclassOf<USeinAbility> AbilityClass, FName Name, FGuid CompiledSchema, const int32& Value);
	DECLARE_FUNCTION(execSetInput);

	/** Read a proposed input during Can Activate With Inputs. Connect a variable
	 *  of the input's exact type to Value. False leaves Value unchanged. */
	UFUNCTION(BlueprintPure, CustomThunk, Category = "SeinARTS|Ability", meta = (CustomStructureParam = "Value", DisplayName = "Get Activation Input"))
	static bool GetInput(const FSeinAbilityActivationInputs& Inputs,
		TSubclassOf<USeinAbility> AbilityClass, FName Name, int32& Value);
	DECLARE_FUNCTION(execGetInput);
};

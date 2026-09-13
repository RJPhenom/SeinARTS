/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinAbilityInputBPFL.cpp
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Captures typed Blueprint input values and submits authenticated requests.
 *
 * @disclaimer   Generated with assistance from an AI language model.
 */
#include "Lib/SeinAbilityInputBPFL.h"
#include "Abilities/SeinAbility.h"
#include "Core/SeinSimContext.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Input/SeinCommand.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "UObject/Stack.h"
#include "UObject/Package.h"
#include "Blueprint/BlueprintExceptionInfo.h"
#include "UObject/UnrealType.h"

namespace
{
	FSeinAbilityActivationInputs InvalidInputs()
	{
		FSeinAbilityActivationInputs Inputs;
		Inputs.SchemaA = 1;
		return Inputs;
	}
	USeinWorldSubsystem* World(const UObject* Context)
	{
		UWorld* W = GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull);
		return W ? W->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	}
}

bool USeinAbilityInputBPFL::IssueAbility(const UObject* WorldContextObject,
	FSeinEntityHandle Entity, FGameplayTag AbilityTag, FSeinEntityHandle TargetEntity,
	FFixedVector TargetLocation, const FSeinAbilityActivationInputs& Inputs)
{
	USeinWorldSubsystem* Sub = World(WorldContextObject);
	if (!Sub || !SeinIsInSimContext(Sub) || !Inputs.IsBounded()) return false;
	FSeinCommand Cmd = FSeinCommand::MakeAbilityCommand(Sub->GetEntityOwner(Entity), Entity, AbilityTag, TargetEntity, TargetLocation);
	Cmd.ActivationInputs = Inputs;
	Sub->EnqueueDerivedCommand(Cmd);
	return true;
}

FSeinAbilityActivationInputs USeinAbilityInputBPFL::MakeInputs(TSubclassOf<USeinAbility> AbilityClass)
{
	FSeinAbilityActivationInputs Result = InvalidInputs();
	FString Error;
	if (AbilityClass) FSeinAbilityActivationInputs::Capture(*AbilityClass->GetDefaultObject<USeinAbility>(), Result, Error);
	return Result;
}

FSeinAbilityActivationInputs USeinAbilityInputBPFL::SetInput(const FSeinAbilityActivationInputs&,
	TSubclassOf<USeinAbility>, FName, FGuid, const int32&) { checkNoEntry(); return InvalidInputs(); }
bool USeinAbilityInputBPFL::GetInput(const FSeinAbilityActivationInputs&,
	TSubclassOf<USeinAbility>, FName, int32&) { checkNoEntry(); return false; }

DEFINE_FUNCTION(USeinAbilityInputBPFL::execSetInput)
{
	P_GET_STRUCT_REF(FSeinAbilityActivationInputs, Inputs);
	P_GET_OBJECT(UClass, AbilityClass);
	P_GET_PROPERTY(FNameProperty, Name);
	P_GET_STRUCT(FGuid, CompiledSchema);
	FGuid CurrentSchema;
	FString SchemaError;
	if (!FSeinAbilityActivationInputs::ComputeSchemaDigest(AbilityClass, CurrentSchema, SchemaError) || CurrentSchema != CompiledSchema)
	{
		*static_cast<FSeinAbilityActivationInputs*>(RESULT_PARAM) = InvalidInputs();
		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, FBlueprintExceptionInfo(
			EBlueprintExceptionType::AbortExecution, NSLOCTEXT("SeinAbilityInputs", "StaleSchema", "Activation input schema changed. Recompile the calling Blueprint.")));
		return;
	}
	FProperty* ExpectedProperty = AbilityClass ? FindFProperty<FProperty>(AbilityClass, Name) : nullptr;
	if (!ExpectedProperty)
	{
		*static_cast<FSeinAbilityActivationInputs*>(RESULT_PARAM) = InvalidInputs();
		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, FBlueprintExceptionInfo(
			EBlueprintExceptionType::AbortExecution, NSLOCTEXT("SeinAbilityInputs", "MissingProperty", "Activation input property is missing. Recompile the calling Blueprint.")));
		return;
	}
	FDefaultConstructedPropertyElement Temporary(ExpectedProperty);
	Stack.MostRecentProperty = nullptr;
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FProperty>(Temporary.GetObjAddress());
	FProperty* SourceProperty = Stack.MostRecentProperty;
	void* Source = Stack.MostRecentPropertyAddress ? Stack.MostRecentPropertyAddress : Temporary.GetObjAddress();
	if (!SourceProperty) SourceProperty = ExpectedProperty;
	P_FINISH;
	P_NATIVE_BEGIN;
	FSeinAbilityActivationInputs Result = InvalidInputs();
	if (AbilityClass && AbilityClass->IsChildOf(USeinAbility::StaticClass()) && SourceProperty && Source)
	{
		TStrongObjectPtr<USeinAbility> Candidate(NewObject<USeinAbility>(GetTransientPackage(), AbilityClass));
		FProperty* Property = FindFProperty<FProperty>(AbilityClass, Name);
		FString Error;
		if (Property && Property->ArrayDim == SourceProperty->ArrayDim && Property->SameType(SourceProperty) && Candidate->GetActivationInputNames().Contains(Name)
			&& Inputs.Decode(*Candidate, Error))
		{
			if (FBoolProperty* Bool = CastField<FBoolProperty>(Property))
				Bool->SetPropertyValue_InContainer(Candidate.Get(), CastFieldChecked<FBoolProperty>(SourceProperty)->GetPropertyValue(Source));
			else Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Candidate.Get()), Source);
			FSeinAbilityActivationInputs::Capture(*Candidate, Result, Error);
		}
	}
	*static_cast<FSeinAbilityActivationInputs*>(RESULT_PARAM) = MoveTemp(Result);
	P_NATIVE_END;
}

DEFINE_FUNCTION(USeinAbilityInputBPFL::execGetInput)
{
	P_GET_STRUCT_REF(FSeinAbilityActivationInputs, Inputs);
	P_GET_OBJECT(UClass, AbilityClass);
	P_GET_PROPERTY(FNameProperty, Name);
	Stack.MostRecentProperty = nullptr;
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	FProperty* OutputProperty = Stack.MostRecentProperty;
	void* Output = Stack.MostRecentPropertyAddress;
	P_FINISH;
	P_NATIVE_BEGIN;
	bool Success = false;
	if (AbilityClass && AbilityClass->IsChildOf(USeinAbility::StaticClass()) && OutputProperty && Output)
	{
		TStrongObjectPtr<USeinAbility> Candidate(NewObject<USeinAbility>(GetTransientPackage(), AbilityClass));
		FProperty* Property = FindFProperty<FProperty>(AbilityClass, Name);
		FString Error;
		if (Property && Property->ArrayDim == OutputProperty->ArrayDim && Property->SameType(OutputProperty) && Candidate->GetActivationInputNames().Contains(Name)
			&& Inputs.Decode(*Candidate, Error))
		{
			if (FBoolProperty* Bool = CastField<FBoolProperty>(Property))
				CastFieldChecked<FBoolProperty>(OutputProperty)->SetPropertyValue(Output, Bool->GetPropertyValue_InContainer(Candidate.Get()));
			else OutputProperty->CopyCompleteValue(Output, Property->ContainerPtrToValuePtr<void>(Candidate.Get()));
			Success = true;
		}
	}
	*static_cast<bool*>(RESULT_PARAM) = Success;
	P_NATIVE_END;
}

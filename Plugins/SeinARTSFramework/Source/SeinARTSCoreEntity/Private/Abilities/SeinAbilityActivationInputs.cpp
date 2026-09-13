/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinAbilityActivationInputs.cpp
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Serializes activation inputs using the bounded canonical object codec.
 *
 * @disclaimer   Generated with assistance from an AI language model.
 */
#include "Abilities/SeinAbilityActivationInputs.h"
#include "Abilities/SeinAbility.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "UObject/UnrealType.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/PropertyOptional.h"

namespace
{
	// Synchronous codec callback context only; never authoritative state. Scoped
	// restoration permits nested calls and isolates concurrent callers.
	thread_local const TArray<FName>* ActiveFields = nullptr;
	bool InputFilter(const FProperty& Property)
	{
		if (Property.HasAnyPropertyFlags(CPF_EditorOnly)) return false;
		return !Cast<UClass>(Property.GetOwnerStruct())
			|| (ActiveFields && ActiveFields->Contains(Property.GetFName()));
	}
	const FSeinStructWireLimits Limits{FSeinAbilityActivationInputs::MaxBytes, 256, 512, 32, 65536};

	bool HasDynamicStruct(const FProperty* Property, TSet<const UStruct*>& Seen)
	{
		if (const FArrayProperty* Array = CastField<FArrayProperty>(Property)) return HasDynamicStruct(Array->Inner, Seen);
		if (const FOptionalProperty* Optional = CastField<FOptionalProperty>(Property)) return HasDynamicStruct(Optional->GetValueProperty(), Seen);
		if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
		{
			if (Struct->Struct == FInstancedStruct::StaticStruct()) return true;
			if (Seen.Contains(Struct->Struct)) return false;
			Seen.Add(Struct->Struct);
			for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
				if (HasDynamicStruct(*It, Seen)) return true;
		}
		return false;
	}

	bool Schema(const UClass* Class, FGuid& Digest, FString& Error)
	{
		if (!Class || !Class->IsChildOf(USeinAbility::StaticClass()))
		{
			Error = TEXT("Select an ability class.");
			return false;
		}
		const TArray<FName> Fields = Class->GetDefaultObject<USeinAbility>()->GetActivationInputNames();
		if (Fields.Num() > FSeinAbilityActivationInputs::MaxFields)
		{
			Error = TEXT("An ability may expose at most 64 activation inputs.");
			return false;
		}
		for (FName Name : Fields)
		{
			const FProperty* Property = FindFProperty<FProperty>(Class, Name);
			TSet<const UStruct*> Seen;
			if (Property && (Property->ArrayDim != 1
				|| (CastField<FBoolProperty>(Property) && !CastFieldChecked<FBoolProperty>(Property)->IsNativeBool())
				|| HasDynamicStruct(Property, Seen)
				|| (Cast<UClass>(Property->GetOwnerStruct())
					&& !CastChecked<UClass>(Property->GetOwnerStruct())->HasAnyClassFlags(CLASS_CompiledFromBlueprint)
					&& Property->HasAllPropertyFlags(CPF_Edit | CPF_DisableEditOnInstance))))
			{
				Error = FString::Printf(TEXT("Activation input '%s' cannot use fixed C arrays, bitfield booleans, Instanced Struct, or native EditDefaultsOnly storage. Use concrete deterministic types and mutable instance properties."), *Name.ToString());
				return false;
			}
			if (!Property || Property->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly | CPF_Deprecated | CPF_SkipSerialization))
			{
				Error = FString::Printf(TEXT("Activation input '%s' must be a serialized property."), *Name.ToString());
				return false;
			}
		}
		TGuardValue<const TArray<FName>*> Guard(ActiveFields, &Fields);
		return FSeinCanonicalStateCodec::ComputeObjectSchemaDigest(Class, InputFilter, Digest, Error);
	}
}

bool FSeinAbilityActivationInputs::ComputeSchemaDigest(const UClass* Class, FGuid& OutDigest, FString& OutError)
{
	return Schema(Class, OutDigest, OutError);
}

bool FSeinAbilityActivationInputs::ValidateClass(const UClass* Class, FString& OutError)
{
	FGuid Digest;
	return Schema(Class, Digest, OutError);
}

bool FSeinAbilityActivationInputs::Capture(const USeinAbility& Values,
	FSeinAbilityActivationInputs& OutInputs, FString& OutError)
{
	FSeinAbilityActivationInputs Result;
	FGuid Digest;
	if (!Schema(Values.GetClass(), Digest, OutError)) return false;
	Result.SetSchemaDigest(Digest);
	const TArray<FName> Fields = Values.GetActivationInputNames();
	TGuardValue<const TArray<FName>*> Guard(ActiveFields, &Fields);
	if (!FSeinCanonicalStateCodec::EncodeObject(Values, {}, Limits, InputFilter, Result.Data, OutError)
		|| !Result.IsBounded()) return false;
	OutInputs = MoveTemp(Result);
	return true;
}

bool FSeinAbilityActivationInputs::Decode(USeinAbility& Candidate, FString& OutError) const
{
	FGuid Expected;
	if (!IsBounded() || !Schema(Candidate.GetClass(), Expected, OutError)) return false;
	if (IsEmpty()) return true;
	if (GetSchemaDigest() != Expected)
	{
		OutError = TEXT("Activation inputs do not match the ability class and exposed-variable schema.");
		return false;
	}
	const TArray<FName> Fields = Candidate.GetActivationInputNames();
	TGuardValue<const TArray<FName>*> Guard(ActiveFields, &Fields);
	return FSeinCanonicalStateCodec::DecodeObject(Data, Candidate, {}, Limits, InputFilter, OutError);
}

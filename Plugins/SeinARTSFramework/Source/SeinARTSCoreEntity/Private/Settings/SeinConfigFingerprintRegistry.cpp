/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinConfigFingerprintRegistry.cpp
 */

#include "Settings/SeinConfigFingerprintRegistry.h"
#include "UObject/PropertyOptional.h"
#include "UObject/UnrealType.h"
#include "UObject/Object.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinConfigFingerprint, Log, All);

namespace
{
	FString FrameFingerprintValue(const FString& Value)
	{
		return FString::FromInt(Value.Len()) + TEXT(":") + Value;
	}

	FString ExportFingerprintValue(const FProperty& Property, const void* ValuePtr)
	{
		if (const FMapProperty* MapProperty = CastField<FMapProperty>(&Property))
		{
			FScriptMapHelper Helper(MapProperty, ValuePtr);
			TArray<FString> Entries;
			Entries.Reserve(Helper.Num());
			for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
			{
				if (!Helper.IsValidIndex(Index)) continue;

				const FString Key = ExportFingerprintValue(
					*MapProperty->KeyProp, Helper.GetKeyPtr(Index));
				const FString Value = ExportFingerprintValue(
					*MapProperty->ValueProp, Helper.GetValuePtr(Index));
				Entries.Add(FrameFingerprintValue(Key) + FrameFingerprintValue(Value));
			}
			Entries.Sort([](const FString& A, const FString& B)
			{
				return A.Compare(B, ESearchCase::CaseSensitive) < 0;
			});

			FString Result = TEXT("Map[");
			for (const FString& Entry : Entries)
			{
				Result += FrameFingerprintValue(Entry);
			}
			Result += TEXT("]");
			return Result;
		}

		if (const FSetProperty* SetProperty = CastField<FSetProperty>(&Property))
		{
			FScriptSetHelper Helper(SetProperty, ValuePtr);
			TArray<FString> Entries;
			Entries.Reserve(Helper.Num());
			for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
			{
				if (!Helper.IsValidIndex(Index)) continue;

				Entries.Add(ExportFingerprintValue(
					*SetProperty->ElementProp, Helper.GetElementPtr(Index)));
			}
			Entries.Sort([](const FString& A, const FString& B)
			{
				return A.Compare(B, ESearchCase::CaseSensitive) < 0;
			});

			FString Result = TEXT("Set[");
			for (const FString& Entry : Entries)
			{
				Result += FrameFingerprintValue(Entry);
			}
			Result += TEXT("]");
			return Result;
		}

		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(&Property))
		{
			FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
			FString Result = TEXT("Array[");
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				Result += FrameFingerprintValue(ExportFingerprintValue(
					*ArrayProperty->Inner, Helper.GetRawPtr(Index)));
			}
			Result += TEXT("]");
			return Result;
		}

		if (const FOptionalProperty* OptionalProperty = CastField<FOptionalProperty>(&Property))
		{
			const void* InnerValue = OptionalProperty->GetValuePointerForReadIfSet(ValuePtr);
			return InnerValue
				? TEXT("Set[") + FrameFingerprintValue(ExportFingerprintValue(
					*OptionalProperty->GetValueProperty(), InnerValue)) + TEXT("]")
				: TEXT("Unset");
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(&Property))
		{
			FString Result = TEXT("Struct[");
			for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
			{
				const FProperty& Field = **It;
				Result += FrameFingerprintValue(Field.GetName());
				for (int32 ArrayIndex = 0; ArrayIndex < Field.ArrayDim; ++ArrayIndex)
				{
					Result += FrameFingerprintValue(ExportFingerprintValue(
						Field, Field.ContainerPtrToValuePtr<void>(ValuePtr, ArrayIndex)));
				}
			}
			Result += TEXT("]");
			return Result;
		}

		FString Value;
		Property.ExportTextItem_Direct(Value, ValuePtr, nullptr, nullptr, PPF_None);
		return Value;
	}

	FString ExportFingerprintField(const FProperty& Property, const void* Container)
	{
		FString Result;
		for (int32 ArrayIndex = 0; ArrayIndex < Property.ArrayDim; ++ArrayIndex)
		{
			Result += FrameFingerprintValue(ExportFingerprintValue(
				Property, Property.ContainerPtrToValuePtr<void>(Container, ArrayIndex)));
		}
		return Result;
	}
}

TArray<FSeinConfigFingerprintContributor>& FSeinConfigFingerprintRegistry::Get()
{
	// Meyers singleton — first-use init, no static-init-order fiasco.
	static TArray<FSeinConfigFingerprintContributor> Registry;
	return Registry;
}

FCriticalSection& FSeinConfigFingerprintRegistry::Mutex()
{
	static FCriticalSection M;
	return M;
}

bool FSeinConfigFingerprintRegistry::RegisterContributor(
	FName StableId, const UObject* SettingsCDO, TArray<FName> FieldNames)
{
	if (StableId.IsNone() || !SettingsCDO || !SettingsCDO->HasAnyFlags(RF_ClassDefaultObject)
		|| FieldNames.IsEmpty())
	{
		UE_LOG(LogSeinConfigFingerprint, Error,
			TEXT("Rejected config-fingerprint contributor '%s': expected a non-empty ID, CDO, and field list."),
			*StableId.ToString());
		return false;
	}

	TSet<FName> UniqueFields;
	for (const FName FieldName : FieldNames)
	{
		if (FieldName.IsNone() || UniqueFields.Contains(FieldName)
			|| !FindFProperty<FProperty>(SettingsCDO->GetClass(), FieldName))
		{
			UE_LOG(LogSeinConfigFingerprint, Error,
				TEXT("Rejected config-fingerprint contributor '%s': field '%s' is missing or duplicated on %s."),
				*StableId.ToString(), *FieldName.ToString(), *SettingsCDO->GetClass()->GetPathName());
			return false;
		}
		UniqueFields.Add(FieldName);
	}

	FScopeLock Lock(&Mutex());
	TArray<FSeinConfigFingerprintContributor>& Registry = Get();
	if (FSeinConfigFingerprintContributor* Existing =
		Registry.FindByPredicate([&](const FSeinConfigFingerprintContributor& C){ return C.StableId == StableId; }))
	{
		const FString ClassPath = SettingsCDO->GetClass()->GetPathName();
		const bool bSameSchema = Existing->FieldNames == FieldNames
			&& Existing->SettingsClassPath == ClassPath;
		if (!bSameSchema)
		{
			UE_LOG(LogSeinConfigFingerprint, Error,
				TEXT("Rejected conflicting config-fingerprint contributor ID '%s'."),
				*StableId.ToString());
			return false;
		}

		// Idempotent re-register (e.g. hot reload): refresh the CDO in place.
		Existing->SettingsCDO = SettingsCDO;
	}
	else
	{
		FSeinConfigFingerprintContributor New;
		New.StableId = StableId;
		New.SettingsCDO = SettingsCDO;
		New.SettingsClassPath = SettingsCDO->GetClass()->GetPathName();
		New.FieldNames = MoveTemp(FieldNames);
		Registry.Add(MoveTemp(New));
	}
	return true;
}

void FSeinConfigFingerprintRegistry::UnregisterContributor(FName StableId)
{
	FScopeLock Lock(&Mutex());
	Get().RemoveAll([&](const FSeinConfigFingerprintContributor& C){ return C.StableId == StableId; });
}

void FSeinConfigFingerprintRegistry::AppendContributors(FString& OutFp)
{
	// Copy under lock, then sort by StableId with a CONTENT-based lexical compare
	// (NOT the registration-order-dependent FName index) so the fold is identical
	// across clients regardless of module load order — the whole point of the seam.
	TArray<FSeinConfigFingerprintContributor> Copy;
	{
		FScopeLock Lock(&Mutex());
		Copy = Get();
	}
	Copy.Sort([](const FSeinConfigFingerprintContributor& A, const FSeinConfigFingerprintContributor& B)
	{
		return A.StableId.LexicalLess(B.StableId);
	});

	for (const FSeinConfigFingerprintContributor& C : Copy)
	{
		const UObject* CDO = C.SettingsCDO.Get();
		if (!CDO)
		{
			const UClass* SettingsClass = FindObject<UClass>(nullptr, *C.SettingsClassPath);
			CDO = SettingsClass ? SettingsClass->GetDefaultObject() : nullptr;
		}
		if (!CDO)
		{
			UE_LOG(LogSeinConfigFingerprint, Fatal,
				TEXT("Config-fingerprint contributor '%s' lost its settings CDO (%s)."),
				*C.StableId.ToString(), *C.SettingsClassPath);
			return;
		}
		const UClass* Cls = CDO->GetClass();
		for (const FName& FieldName : C.FieldNames)
		{
			// `<StableId>|<field>=<value>;` — the StableId prefix namespaces the field.
			// Maps/sets use sorted, length-framed entries because UE ExportText follows
			// their internal hash iteration order; other fields retain ExportText.
			OutFp += C.StableId.ToString();
			OutFp += TEXT("|");
			OutFp += FieldName.ToString();
			OutFp += TEXT("=");
			const FProperty* Prop = FindFProperty<FProperty>(Cls, FieldName);
			if (!Prop)
			{
				UE_LOG(LogSeinConfigFingerprint, Fatal,
					TEXT("Config-fingerprint field '%s.%s' disappeared after registration."),
					*C.StableId.ToString(), *FieldName.ToString());
				return;
			}
			OutFp += ExportFingerprintField(*Prop, CDO);
			OutFp += TEXT(";");
		}
	}
}

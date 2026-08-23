/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinConfigFingerprintRegistry.cpp
 */

#include "Settings/SeinConfigFingerprintRegistry.h"
#include "UObject/PropertyOptional.h"
#include "UObject/UnrealType.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinConfigFingerprint, Log, All);

namespace
{
	struct FSeinConfigFingerprintClaim
	{
		uint64 Token = 0;
		TWeakObjectPtr<const UObject> SettingsCDO;
	};

	struct FSeinConfigFingerprintContributor
	{
		FName StableId;
		FString SettingsClassPath;
		TArray<FName> FieldNames;
		TArray<FSeinConfigFingerprintClaim> Claims;
	};

	struct FSeinConfigFingerprintRegistryState
	{
		FCriticalSection Mutex;
		TArray<FSeinConfigFingerprintContributor> Contributors;
		uint64 NextToken = 1;
	};

	FSeinConfigFingerprintRegistryState& GetRegistryState()
	{
		static FSeinConfigFingerprintRegistryState State;
		return State;
	}

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

		// FText is presentation data, never simulation input — and its exported
		// form is NOT stable across process modes: an editor process stamps the
		// package-localization namespace ("[/Script/Module]") onto texts loaded
		// from config while a -game process exports an empty namespace. Hashing it
		// made every editor-hosted vs game-process pairing fail config parity
		// (the separate-process PIE kick). Emit a fixed marker so struct framing
		// stays positional without the text ever contributing.
		if (CastField<FTextProperty>(&Property))
		{
			return TEXT("Text[omitted]");
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

FString FSeinConfigFingerprintRegistry::ExportFieldCanonical(
	const FProperty& Property, const void* Container)
{
	return ExportFingerprintField(Property, Container);
}

FSeinConfigFingerprintRegistrationHandle::
	~FSeinConfigFingerprintRegistrationHandle()
{
	Reset();
}

FSeinConfigFingerprintRegistrationHandle::
	FSeinConfigFingerprintRegistrationHandle(
		FSeinConfigFingerprintRegistrationHandle&& Other) noexcept
	: Token(Other.Token)
{
	Other.Token = 0;
}

FSeinConfigFingerprintRegistrationHandle&
FSeinConfigFingerprintRegistrationHandle::operator=(
	FSeinConfigFingerprintRegistrationHandle&& Other) noexcept
{
	if (this != &Other)
	{
		Reset();
		Token = Other.Token;
		Other.Token = 0;
	}
	return *this;
}

void FSeinConfigFingerprintRegistrationHandle::Reset()
{
	if (Token != 0)
	{
		FSeinConfigFingerprintRegistry::UnregisterContributor(Token);
		Token = 0;
	}
}

FSeinConfigFingerprintRegistrationHandle
FSeinConfigFingerprintRegistry::RegisterContributor(
	FName StableId, const UObject* SettingsCDO, TArray<FName> FieldNames)
{
	if (StableId.IsNone() || !SettingsCDO || !SettingsCDO->HasAnyFlags(RF_ClassDefaultObject)
		|| FieldNames.IsEmpty())
	{
		UE_LOG(LogSeinConfigFingerprint, Error,
			TEXT("Rejected config-fingerprint contributor '%s': expected a non-empty ID, CDO, and field list."),
			*StableId.ToString());
		return {};
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
			return {};
		}
		UniqueFields.Add(FieldName);
	}

	FSeinConfigFingerprintRegistryState& State = GetRegistryState();
	FScopeLock Lock(&State.Mutex);
	if (State.NextToken == 0 || State.NextToken == MAX_uint64)
	{
		UE_LOG(LogSeinConfigFingerprint, Error,
			TEXT("Rejected config-fingerprint contributor '%s': registration token space is exhausted."),
			*StableId.ToString());
		return {};
	}

	TArray<FSeinConfigFingerprintContributor>& Registry = State.Contributors;
	FSeinConfigFingerprintContributor* Existing =
		Registry.FindByPredicate(
			[&](const FSeinConfigFingerprintContributor& Contributor)
			{
				return Contributor.StableId == StableId;
			});
	if (Existing)
	{
		const FString ClassPath = SettingsCDO->GetClass()->GetPathName();
		const bool bSameSchema = Existing->FieldNames == FieldNames
			&& Existing->SettingsClassPath == ClassPath;
		if (!bSameSchema)
		{
			UE_LOG(LogSeinConfigFingerprint, Error,
				TEXT("Rejected conflicting config-fingerprint contributor ID '%s'."),
				*StableId.ToString());
			return {};
		}
		if (Existing->Claims.Num() >= MaxReloadClaimsPerContributor)
		{
			UE_LOG(LogSeinConfigFingerprint, Error,
				TEXT("Rejected config-fingerprint contributor '%s': too many overlapping reload generations."),
				*StableId.ToString());
			return {};
		}
	}
	else
	{
		FSeinConfigFingerprintContributor New;
		New.StableId = StableId;
		New.SettingsClassPath = SettingsCDO->GetClass()->GetPathName();
		New.FieldNames = MoveTemp(FieldNames);
		Registry.Add(MoveTemp(New));
		Existing = &Registry.Last();
	}

	const uint64 Token = State.NextToken++;
	FSeinConfigFingerprintClaim& Claim = Existing->Claims.AddDefaulted_GetRef();
	Claim.Token = Token;
	Claim.SettingsCDO = SettingsCDO;
	return FSeinConfigFingerprintRegistrationHandle(Token);
}

void FSeinConfigFingerprintRegistry::UnregisterContributor(uint64 Token)
{
	if (Token == 0)
	{
		return;
	}

	FSeinConfigFingerprintRegistryState& State = GetRegistryState();
	FScopeLock Lock(&State.Mutex);
	const int32 ContributorIndex = State.Contributors.IndexOfByPredicate(
		[Token](const FSeinConfigFingerprintContributor& Contributor)
		{
			return Contributor.Claims.ContainsByPredicate(
				[Token](const FSeinConfigFingerprintClaim& Claim)
				{
					return Claim.Token == Token;
				});
		});
	if (ContributorIndex == INDEX_NONE)
	{
		return;
	}

	FSeinConfigFingerprintContributor& Contributor =
		State.Contributors[ContributorIndex];
	const int32 RemovedClaims = Contributor.Claims.RemoveAll(
		[Token](const FSeinConfigFingerprintClaim& Claim)
		{
			return Claim.Token == Token;
		});
	check(RemovedClaims == 1);
	if (Contributor.Claims.IsEmpty())
	{
		State.Contributors.RemoveAt(ContributorIndex);
	}
}

void FSeinConfigFingerprintRegistry::AppendContributors(FString& OutFp)
{
	// Copy under lock, then sort by StableId with a CONTENT-based lexical compare
	// (NOT the registration-order-dependent FName index) so the fold is identical
	// across clients regardless of module load order — the whole point of the seam.
	TArray<FSeinConfigFingerprintContributor> Copy;
	{
		FSeinConfigFingerprintRegistryState& State = GetRegistryState();
		FScopeLock Lock(&State.Mutex);
		Copy = State.Contributors;
	}
	Copy.Sort([](const FSeinConfigFingerprintContributor& A, const FSeinConfigFingerprintContributor& B)
	{
		return A.StableId.LexicalLess(B.StableId);
	});

	for (const FSeinConfigFingerprintContributor& C : Copy)
	{
		const UObject* CDO = nullptr;
		uint64 NewestLiveToken = 0;
		for (const FSeinConfigFingerprintClaim& Claim : C.Claims)
		{
			if (Claim.Token > NewestLiveToken)
			{
				if (const UObject* Candidate = Claim.SettingsCDO.Get())
				{
					CDO = Candidate;
					NewestLiveToken = Claim.Token;
				}
			}
		}
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

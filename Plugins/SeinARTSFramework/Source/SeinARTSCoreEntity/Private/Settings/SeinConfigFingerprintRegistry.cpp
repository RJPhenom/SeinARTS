/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinConfigFingerprintRegistry.cpp
 */

#include "Settings/SeinConfigFingerprintRegistry.h"
#include "UObject/UnrealType.h"
#include "UObject/Object.h"

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

void FSeinConfigFingerprintRegistry::RegisterContributor(FName StableId, const UObject* SettingsCDO, TArray<FName> FieldNames)
{
	FScopeLock Lock(&Mutex());
	TArray<FSeinConfigFingerprintContributor>& Registry = Get();
	if (FSeinConfigFingerprintContributor* Existing =
		Registry.FindByPredicate([&](const FSeinConfigFingerprintContributor& C){ return C.StableId == StableId; }))
	{
		// Idempotent re-register (e.g. hot-reload): replace in place.
		Existing->SettingsCDO = SettingsCDO;
		Existing->FieldNames = MoveTemp(FieldNames);
	}
	else
	{
		FSeinConfigFingerprintContributor New;
		New.StableId = StableId;
		New.SettingsCDO = SettingsCDO;
		New.FieldNames = MoveTemp(FieldNames);
		Registry.Add(MoveTemp(New));
	}
}

void FSeinConfigFingerprintRegistry::UnregisterContributor(FName StableId)
{
	FScopeLock Lock(&Mutex());
	Get().RemoveAll([&](const FSeinConfigFingerprintContributor& C){ return C.StableId == StableId; });
}

void FSeinConfigFingerprintRegistry::AppendContributors(FString& OutFp)
{
	FScopeLock Lock(&Mutex());
	// Copy under lock, then sort by StableId with a CONTENT-based lexical compare
	// (NOT the registration-order-dependent FName index) so the fold is identical
	// across clients regardless of module load order — the whole point of the seam.
	TArray<FSeinConfigFingerprintContributor> Copy = Get();
	Copy.Sort([](const FSeinConfigFingerprintContributor& A, const FSeinConfigFingerprintContributor& B)
	{
		return A.StableId.LexicalLess(B.StableId);
	});

	for (const FSeinConfigFingerprintContributor& C : Copy)
	{
		const UObject* CDO = C.SettingsCDO.Get();
		if (!CDO) continue;   // unregistered / unloaded extension contributes nothing
		const UClass* Cls = CDO->GetClass();
		for (const FName& FieldName : C.FieldNames)
		{
			// `<StableId>|<field>=<ExportText>;` — the StableId prefix namespaces the
			// field so two contributors can safely share a name. The value loop is
			// byte-identical to the core Fields[] loop in ComputeConfigFingerprint.
			OutFp += C.StableId.ToString();
			OutFp += TEXT("|");
			OutFp += FieldName.ToString();
			OutFp += TEXT("=");
			if (const FProperty* Prop = FindFProperty<FProperty>(Cls, FieldName))
			{
				FString Value;
				Prop->ExportText_InContainer(0, Value, CDO, nullptr, nullptr, PPF_None);
				OutFp += Value;
			}
			OutFp += TEXT(";");
		}
	}
}

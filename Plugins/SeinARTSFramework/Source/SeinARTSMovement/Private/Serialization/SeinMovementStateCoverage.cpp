/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementStateCoverage.cpp
 */

#include "Serialization/SeinMovementStateCoverage.h"

#include "Movement/SeinAvoidance.h"
#include "Movement/SeinMovement.h"
#include "SeinARTSMovementModule.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Serialization/SeinMovementStateCoverageInternal.h"
#include "CoreGlobals.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

namespace
{
	struct FCoverageClaim
	{
		uint64 Token = 0;
		FString Owner;
		FSeinMovementStateCoverageDescriptor Descriptor;
		TStrongObjectPtr<UClass> ClassRoot;
	};

	struct FCoverageEntry
	{
		FString ClassPath;
		FString Owner;
		ESeinMovementStateCoverage Coverage =
			ESeinMovementStateCoverage::Stateless;
		TArray<FSeinCanonicalStateKey> RequiredProviders;
		TArray<FCoverageClaim> Claims;
	};

	TArray<FCoverageEntry>& Registry()
	{
		static TArray<FCoverageEntry> Value;
		return Value;
	}

	uint64& NextToken()
	{
		static uint64 Value = 1;
		return Value;
	}

	bool& ProviderRefreshEnabled()
	{
		static bool Value = false;
		return Value;
	}

	void SetError(FString* OutError, FString Error)
	{
		if (OutError)
		{
			*OutError = MoveTemp(Error);
		}
	}

	FString CanonicalOwner(FName Owner)
	{
		return Owner.ToString().ToLower();
	}

	bool IsStableIdentifier(const FString& Value)
	{
		if (Value.IsEmpty())
		{
			return false;
		}
		for (const TCHAR Character : Value)
		{
			if (!((Character >= TEXT('a') && Character <= TEXT('z'))
				|| (Character >= TEXT('0') && Character <= TEXT('9'))
				|| Character == TEXT('.')
				|| Character == TEXT('_')
				|| Character == TEXT('-')))
			{
				return false;
			}
		}
		return true;
	}

	FString CanonicalProviderKey(const FSeinCanonicalStateKey& Key)
	{
		return FSeinCanonicalStateRegistry::CanonicalKey(Key);
	}

	bool ProviderKeyLess(
		const FSeinCanonicalStateKey& A,
		const FSeinCanonicalStateKey& B)
	{
		return CanonicalProviderKey(A) < CanonicalProviderKey(B);
	}

	bool ProviderKeysEqual(
		TConstArrayView<FSeinCanonicalStateKey> A,
		TConstArrayView<FSeinCanonicalStateKey> B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (!(A[Index] == B[Index]))
			{
				return false;
			}
		}
		return true;
	}

	const TCHAR* CoverageName(ESeinMovementStateCoverage Coverage)
	{
		switch (Coverage)
		{
		case ESeinMovementStateCoverage::Stateless:
			return TEXT("stateless");
		case ESeinMovementStateCoverage::ReflectedComplete:
			return TEXT("reflected-complete");
		case ESeinMovementStateCoverage::Supplemental:
			return TEXT("supplemental");
		default:
			return TEXT("invalid");
		}
	}

	const TCHAR* PolicyKind(const UClass* Class)
	{
		return Class && Class->IsChildOf(USeinMovement::StaticClass())
			? TEXT("movement")
			: TEXT("avoidance");
	}

	bool IsRelevantNativeClass(const UClass* Class)
	{
		return Class
			&& Class->HasAnyClassFlags(CLASS_Native)
			&& !Class->HasAnyClassFlags(
				CLASS_Deprecated | CLASS_NewerVersionExists)
			&& (Class->IsChildOf(USeinMovement::StaticClass())
				|| Class->IsChildOf(USeinAvoidance::StaticClass()));
	}

	FCoverageEntry* FindEntryByPath(const FString& ClassPath)
	{
		return Registry().FindByPredicate(
			[&ClassPath](const FCoverageEntry& Entry)
			{
				return Entry.ClassPath == ClassPath;
			});
	}

	const FCoverageEntry* FindEntryForClass(const UClass* Class)
	{
		return Class ? FindEntryByPath(Class->GetPathName()) : nullptr;
	}

	uint64 AllocateToken()
	{
		uint64& Candidate = NextToken();
		if (Candidate == 0)
		{
			Candidate = 1;
		}
		return Candidate++;
	}

	bool RefreshCanonicalProvider(FString& OutError)
	{
		// Module ShutdownModule callbacks run after UObject class teardown has
		// begun during final process exit. No world can observe a new contract at
		// that point, and attempting to rebuild one would dereference class
		// identities UE has deliberately retired. Live module unload and hot
		// reload still refresh because no exit has been requested there.
		if (!ProviderRefreshEnabled() || IsEngineExitRequested())
		{
			return true;
		}
		FSeinARTSMovementModule* Module =
			FModuleManager::GetModulePtr<FSeinARTSMovementModule>(
				TEXT("SeinARTSMovement"));
		if (!Module)
		{
			OutError =
				TEXT("SeinARTSMovement is not loaded while refreshing movement state coverage.");
			return false;
		}
		return Module->RefreshCanonicalStateProvider(OutError);
	}

	bool RemoveToken(uint64 Token, bool bRefresh, bool& bOutManifestChanged)
	{
		bOutManifestChanged = false;
		for (int32 EntryIndex = 0; EntryIndex < Registry().Num(); ++EntryIndex)
		{
			FCoverageEntry& Entry = Registry()[EntryIndex];
			const int32 ClaimIndex = Entry.Claims.IndexOfByPredicate(
				[Token](const FCoverageClaim& Claim)
				{
					return Claim.Token == Token;
				});
			if (ClaimIndex == INDEX_NONE)
			{
				continue;
			}

			Entry.Claims.RemoveAt(ClaimIndex);
			if (Entry.Claims.IsEmpty())
			{
				Registry().RemoveAt(EntryIndex);
				bOutManifestChanged = true;
			}

			if (bRefresh && bOutManifestChanged)
			{
				FString Error;
				if (!RefreshCanonicalProvider(Error))
				{
					UE_LOG(LogTemp, Error,
						TEXT("Movement canonical provider refresh failed after coverage withdrawal: %s"),
						*Error);
				}
			}
			return true;
		}
		return false;
	}

	bool ContainsToken(uint64 Token)
	{
		return Token != 0 && Registry().ContainsByPredicate(
			[Token](const FCoverageEntry& Entry)
			{
				return Entry.Claims.ContainsByPredicate(
					[Token](const FCoverageClaim& Claim)
					{
						return Claim.Token == Token;
					});
			});
	}
}

FSeinMovementStateCoverageRegistrationHandle::
	~FSeinMovementStateCoverageRegistrationHandle()
{
	Reset();
}

FSeinMovementStateCoverageRegistrationHandle::
	FSeinMovementStateCoverageRegistrationHandle(
		FSeinMovementStateCoverageRegistrationHandle&& Other) noexcept
	: Token(Other.Token)
{
	check(IsInGameThread());
	Other.Token = 0;
}

FSeinMovementStateCoverageRegistrationHandle&
FSeinMovementStateCoverageRegistrationHandle::operator=(
	FSeinMovementStateCoverageRegistrationHandle&& Other) noexcept
{
	check(IsInGameThread());
	if (this != &Other)
	{
		Reset();
		Token = Other.Token;
		Other.Token = 0;
	}
	return *this;
}

void FSeinMovementStateCoverageRegistrationHandle::Reset()
{
	if (Token != 0)
	{
		check(IsInGameThread());
		const bool bRemoved =
			FSeinMovementStateCoverageRegistry::UnregisterToken(Token);
		check(bRemoved);
		Token = 0;
	}
}

FSeinMovementStateCoverageRegistrationHandle
FSeinMovementStateCoverageRegistry::Register(
	FName OwnerModuleId,
	const FSeinMovementStateCoverageDescriptor& Descriptor,
	FString* OutError)
{
	check(IsInGameThread());
	SetError(OutError, FString());

	const FString Owner = CanonicalOwner(OwnerModuleId);
	if (!IsStableIdentifier(Owner)
		|| !IsRelevantNativeClass(Descriptor.NativeClass))
	{
		SetError(OutError,
			TEXT("Movement state coverage requires a stable owner and an exact native USeinMovement/USeinAvoidance class."));
		return {};
	}

	FSeinMovementStateCoverageDescriptor Canonical = Descriptor;
	Canonical.RequiredProviders.Sort(ProviderKeyLess);
	for (int32 Index = 0; Index < Canonical.RequiredProviders.Num(); ++Index)
	{
		const FString ProviderKey =
			CanonicalProviderKey(
				Canonical.RequiredProviders[Index]);
		if (!Canonical.RequiredProviders[Index].IsValid()
			|| ProviderKey.IsEmpty()
			|| ProviderKey
				== TEXT("seinarts.movement/persistent-policy-instances")
			|| (Index > 0
				&& Canonical.RequiredProviders[Index]
					== Canonical.RequiredProviders[Index - 1]))
		{
			SetError(OutError,
				TEXT("Movement supplemental provider keys must be valid and unique."));
			return {};
		}
	}
	if ((Canonical.Coverage == ESeinMovementStateCoverage::Supplemental)
			!= !Canonical.RequiredProviders.IsEmpty())
	{
		SetError(OutError,
			TEXT("Only Supplemental movement coverage may declare providers, and it must declare at least one."));
		return {};
	}

	const FString ClassPath = Canonical.NativeClass->GetPathName();
	FCoverageEntry* Entry = FindEntryByPath(ClassPath);
	const bool bManifestChanged = Entry == nullptr;
	if (Entry)
	{
		if (Entry->Owner != Owner
			|| Entry->Coverage != Canonical.Coverage
			|| !ProviderKeysEqual(
				Entry->RequiredProviders,
				Canonical.RequiredProviders))
		{
			SetError(OutError, FString::Printf(
				TEXT("Conflicting movement state coverage claim for '%s'."),
				*ClassPath));
			return {};
		}
	}
	else
	{
		Entry = &Registry().AddDefaulted_GetRef();
		Entry->ClassPath = ClassPath;
		Entry->Owner = Owner;
		Entry->Coverage = Canonical.Coverage;
		Entry->RequiredProviders = Canonical.RequiredProviders;
	}

	FCoverageClaim Claim;
	Claim.Token = AllocateToken();
	Claim.Owner = Owner;
	Claim.Descriptor = MoveTemp(Canonical);
	Claim.ClassRoot.Reset(
		const_cast<UClass*>(Claim.Descriptor.NativeClass));
	const uint64 Token = Claim.Token;
	Entry->Claims.Add(MoveTemp(Claim));

	if (bManifestChanged)
	{
		FString RefreshError;
		if (!RefreshCanonicalProvider(RefreshError))
		{
			bool bIgnoredChanged = false;
			RemoveToken(Token, false, bIgnoredChanged);
			FString RollbackError;
			RefreshCanonicalProvider(RollbackError);
			SetError(OutError, FString::Printf(
				TEXT("Movement coverage provider refresh failed: %s"),
				*RefreshError));
			return {};
		}
	}
	return FSeinMovementStateCoverageRegistrationHandle(Token);
}

bool FSeinMovementStateCoverageRegistry::Unregister(
	FSeinMovementStateCoverageRegistrationHandle& Handle)
{
	check(IsInGameThread());
	if (!Handle.IsValid())
	{
		return false;
	}
	if (!UnregisterToken(Handle.Token))
	{
		return false;
	}
	Handle.Token = 0;
	return true;
}

bool FSeinMovementStateCoverageRegistry::UnregisterAll(
	TArray<FSeinMovementStateCoverageRegistrationHandle>& Handles,
	FString* OutError)
{
	check(IsInGameThread());
	SetError(OutError, FString());
	if (Handles.IsEmpty())
	{
		return true;
	}

	TSet<uint64> Tokens;
	Tokens.Reserve(Handles.Num());
	for (const FSeinMovementStateCoverageRegistrationHandle& Handle : Handles)
	{
		if (!Handle.IsValid()
			|| Tokens.Contains(Handle.Token)
			|| !ContainsToken(Handle.Token))
		{
			SetError(OutError,
				TEXT("Movement state coverage batch contains an invalid, duplicate, or stale handle."));
			return false;
		}
		Tokens.Add(Handle.Token);
	}

	bool bManifestChanged = false;
	for (FSeinMovementStateCoverageRegistrationHandle& Handle : Handles)
	{
		bool bEntryChanged = false;
		const bool bRemoved =
			RemoveToken(Handle.Token, false, bEntryChanged);
		check(bRemoved);
		bManifestChanged |= bEntryChanged;
		Handle.Token = 0;
	}
	Handles.Reset();

	if (bManifestChanged)
	{
		FString RefreshError;
		if (!RefreshCanonicalProvider(RefreshError))
		{
			SetError(OutError, FString::Printf(
				TEXT("Movement canonical provider refresh failed after atomic coverage withdrawal: %s"),
				*RefreshError));
			return false;
		}
	}
	return true;
}

bool FSeinMovementStateCoverageRegistry::UnregisterToken(uint64 Token)
{
	check(IsInGameThread());
	bool bManifestChanged = false;
	return Token != 0
		&& RemoveToken(Token, true, bManifestChanged);
}

int32 FSeinMovementStateCoverageRegistry::GetRegisteredClassCount()
{
	return Registry().Num();
}

bool SeinBuildMovementStateCoverageSnapshot(
	FSeinMovementStateCoverageSnapshot& OutSnapshot,
	FString& OutError,
	bool bRequireCompleteLoadedClasses)
{
	check(IsInGameThread());
	OutSnapshot = {};
	OutError.Reset();

	if (bRequireCompleteLoadedClasses)
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (IsRelevantNativeClass(*It)
				&& !FindEntryForClass(*It))
			{
				OutError = FString::Printf(
					TEXT("Native movement policy layer '%s' has no exact state coverage claim."),
					*It->GetPathName());
				return false;
			}
		}
	}

	TArray<const FCoverageEntry*> Entries;
	Entries.Reserve(Registry().Num());
	for (const FCoverageEntry& Entry : Registry())
	{
		Entries.Add(&Entry);
	}
	Entries.Sort([](
		const FCoverageEntry& A,
		const FCoverageEntry& B)
	{
		return A.ClassPath < B.ClassPath;
	});

	TSet<FString> SupplementalKeys;
	FSeinCanonicalDigestWriter Writer(
		TEXT("SeinARTS.Movement.StateCoverage"), 1);
	if (!Writer.WriteUInt32(static_cast<uint32>(Entries.Num())))
	{
		OutError = Writer.GetError();
		return false;
	}

	for (const FCoverageEntry* Entry : Entries)
	{
		if (!Entry || Entry->Claims.IsEmpty())
		{
			OutError =
				TEXT("Movement state coverage registry contains an empty class entry.");
			return false;
		}
		const UClass* Class =
			Entry->Claims.Last().Descriptor.NativeClass;
		if (!IsRelevantNativeClass(Class)
			|| Class->GetPathName() != Entry->ClassPath)
		{
			OutError =
				TEXT("Movement state coverage registry contains a stale class identity.");
			return false;
		}
		if (!SeinValidateMovementReflectedClassForCanonicalState(
			Class,
			Entry->Coverage
				== ESeinMovementStateCoverage::Stateless,
			OutError))
		{
			return false;
		}

		FString Providers;
		for (int32 Index = 0; Index < Entry->RequiredProviders.Num(); ++Index)
		{
			const FString Key =
				CanonicalProviderKey(Entry->RequiredProviders[Index]);
			if (Index > 0)
			{
				Providers += TEXT(",");
			}
			Providers += Key;
			SupplementalKeys.Add(Key);
		}
		const FString Claim = FString::Printf(
			TEXT("kind=%s|class=%s|owner=%s|coverage=%s|providers=%s"),
			PolicyKind(Class),
			*Class->GetPathName(),
			*Entry->Owner,
			CoverageName(Entry->Coverage),
			*Providers);
		OutSnapshot.Claims.Add(Claim);
		if (!Writer.WriteString(Claim))
		{
			OutError = Writer.GetError();
			return false;
		}
	}

	FGuid IdentityDigest;
	if (!Writer.Finalize(IdentityDigest, OutError))
	{
		return false;
	}
	OutSnapshot.Identity = FName(*FString::Printf(
		TEXT("coverage-%s"),
		*IdentityDigest.ToString(EGuidFormats::Digits).ToLower()));

	for (const FCoverageEntry& Entry : Registry())
	{
		for (const FSeinCanonicalStateKey& Key :
			Entry.RequiredProviders)
		{
			if (SupplementalKeys.Contains(CanonicalProviderKey(Key)))
			{
				OutSnapshot.SupplementalProviders.Add(Key);
				SupplementalKeys.Remove(CanonicalProviderKey(Key));
			}
		}
	}
	OutSnapshot.SupplementalProviders.Sort(ProviderKeyLess);
	return true;
}

bool SeinValidateMovementStateCoverageForClass(
	const UClass* ConcreteClass,
	FString& OutError)
{
	OutError.Reset();
	if (!ConcreteClass
		|| !(ConcreteClass->IsChildOf(USeinMovement::StaticClass())
			|| ConcreteClass->IsChildOf(USeinAvoidance::StaticClass())))
	{
		OutError =
			TEXT("Movement state coverage validation requires a movement or avoidance class.");
		return false;
	}

	for (const UClass* Cursor = ConcreteClass;
		Cursor;
		Cursor = Cursor->GetSuperClass())
	{
		if (!IsRelevantNativeClass(Cursor))
		{
			continue;
		}
		const FCoverageEntry* Entry = FindEntryForClass(Cursor);
		if (!Entry || Entry->Claims.IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("Class '%s' inherits unclaimed native state layer '%s'."),
				*ConcreteClass->GetPathName(),
				*Cursor->GetPathName());
			return false;
		}
	}
	return true;
}

void SeinSetMovementCoverageProviderRefreshEnabled(bool bEnabled)
{
	check(IsInGameThread());
	ProviderRefreshEnabled() = bEnabled;
}

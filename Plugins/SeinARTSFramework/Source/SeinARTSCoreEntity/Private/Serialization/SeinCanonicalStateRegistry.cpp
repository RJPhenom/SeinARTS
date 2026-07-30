/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCanonicalStateRegistry.cpp
 */

#include "Serialization/SeinCanonicalStateRegistry.h"

#include "Hash/Blake3.h"
#include "Input/SeinCommandSchemaRegistry.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "Serialization/SeinStateProviderTransaction.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Misc/ScopeLock.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinCanonicalState, Log, All);

namespace
{
	constexpr uint32 ContractFormatVersion = 1;
	constexpr int32 MaxNativeCheckpointBytes = 64 * 1024 * 1024;

	bool IsProviderInvocationActive()
	{
		return FSeinStateProviderTransactionScope::IsActive();
	}

	class FProviderInvocationScope
	{
	public:
		FProviderInvocationScope() = default;
		~FProviderInvocationScope() = default;

		FProviderInvocationScope(const FProviderInvocationScope&) = delete;
		FProviderInvocationScope& operator=(
			const FProviderInvocationScope&) = delete;

	private:
		FSeinStateProviderTransactionScope Scope;
	};

	struct FProviderClaim
	{
		uint64 Token = 0;
		FString Owner;
		FSeinCanonicalStateDescriptor Descriptor;
		FSeinCanonicalStateContributorOps Ops;
		TStrongObjectPtr<UScriptStruct> PayloadRoot;
		TArray<TStrongObjectPtr<UScriptStruct>> DynamicRoots;
	};

	struct FRegisteredContributor
	{
		FString CanonicalKey;
		FString CanonicalDescriptor;
		FGuid DescriptorDigest;
		TArray<FProviderClaim> Claims;
	};

	TArray<FRegisteredContributor>& Registry()
	{
		static TArray<FRegisteredContributor> Value;
		return Value;
	}

	FCriticalSection& RegistryMutex()
	{
		static FCriticalSection Value;
		return Value;
	}

	uint64& NextToken()
	{
		static uint64 Value = 1;
		return Value;
	}

	void SetError(FString* OutError, FString Error)
	{
		if (OutError)
		{
			*OutError = MoveTemp(Error);
		}
	}

	uint32 ReadUInt32BigEndian(const uint8* Bytes)
	{
		return static_cast<uint32>(Bytes[0]) << 24
			| static_cast<uint32>(Bytes[1]) << 16
			| static_cast<uint32>(Bytes[2]) << 8
			| static_cast<uint32>(Bytes[3]);
	}

	FGuid DigestUtf8(const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		const FBlake3Hash Hash = FBlake3::HashBuffer(
			Utf8.Get(), Utf8.Length());
		const uint8* Bytes = Hash.GetBytes();
		return FGuid(
			ReadUInt32BigEndian(Bytes),
			ReadUInt32BigEndian(Bytes + 4),
			ReadUInt32BigEndian(Bytes + 8),
			ReadUInt32BigEndian(Bytes + 12));
	}

	void AppendFramed(FString& Out, const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		Out += FString::Printf(TEXT("%d:"), Utf8.Length());
		Out += Value;
		Out += TEXT("\n");
	}

	bool IsStableASCIIIdentifier(const FString& Value)
	{
		if (Value.IsEmpty())
		{
			return false;
		}
		for (const TCHAR Character : Value)
		{
			const bool bValid =
				(Character >= TEXT('a') && Character <= TEXT('z'))
				|| (Character >= TEXT('0') && Character <= TEXT('9'))
				|| Character == TEXT('.')
				|| Character == TEXT('_')
				|| Character == TEXT('-');
			if (!bValid)
			{
				return false;
			}
		}
		return true;
	}

	FString CanonicalName(FName Value)
	{
		return Value.IsNone() ? FString() : Value.ToString().ToLower();
	}

	class FStagedPayloadView final
		: public ISeinCanonicalStateStagedPayloadView
	{
	public:
		FStagedPayloadView(
			const TMap<FString, FInstancedStruct>& InPayloads,
			const TSet<FString>& InAllowedKeys)
			: Payloads(InPayloads)
			, AllowedKeys(InAllowedKeys)
		{
		}

		virtual const FInstancedStruct* FindStagedPayload(
			const FSeinCanonicalStateKey& Key) const override
		{
			const FString Canonical =
				FSeinCanonicalStateRegistry::CanonicalKey(Key);
			if (!AllowedKeys.Contains(Canonical))
			{
				return nullptr;
			}
			return Payloads.Find(Canonical);
		}

	private:
		const TMap<FString, FInstancedStruct>& Payloads;
		const TSet<FString>& AllowedKeys;
	};

	bool CanonicalizeDescriptorCollections(
		const FSeinCanonicalStateDescriptor& Descriptor,
		FSeinCanonicalStateDescriptor& OutDescriptor,
		FString& OutError)
	{
		OutDescriptor = Descriptor;
		if (OutDescriptor.DynamicPayloadStructs.Contains(nullptr))
		{
			OutError =
				TEXT("Dynamic payload schema types must be non-null and unique.");
			return false;
		}
		OutDescriptor.DynamicPayloadStructs.Sort(
			[](const UScriptStruct& A, const UScriptStruct& B)
			{
				return A.GetPathName() < B.GetPathName();
			});

		FString IgnoredNameManifest;
		SeinBuildCanonicalWireNameCatalog(
			Descriptor.AllowedNames,
			OutDescriptor.AllowedNames,
			IgnoredNameManifest);

		OutDescriptor.RestoreAfter.Sort(
			[](const FSeinCanonicalStateKey& A,
				const FSeinCanonicalStateKey& B)
			{
				return FSeinCanonicalStateRegistry::CanonicalKey(A)
					< FSeinCanonicalStateRegistry::CanonicalKey(B);
			});

		for (int32 Index = 1;
			Index < OutDescriptor.DynamicPayloadStructs.Num();
			++Index)
		{
			const UScriptStruct* Previous =
				OutDescriptor.DynamicPayloadStructs[Index - 1];
			const UScriptStruct* Current =
				OutDescriptor.DynamicPayloadStructs[Index];
			if (Previous->GetPathName() == Current->GetPathName())
			{
				OutError =
					TEXT("Dynamic payload schema types must be non-null and unique.");
				return false;
			}
		}
		for (int32 Index = 1;
			Index < OutDescriptor.RestoreAfter.Num();
			++Index)
		{
			if (FSeinCanonicalStateRegistry::CanonicalKey(
					OutDescriptor.RestoreAfter[Index - 1])
				== FSeinCanonicalStateRegistry::CanonicalKey(
					OutDescriptor.RestoreAfter[Index]))
			{
				OutError =
					TEXT("Restore dependencies must be unique.");
				return false;
			}
		}
		return true;
	}

	FSeinStructWireLimits BuildWireLimits(
		const FSeinCanonicalStateLimits& StateLimits)
	{
		FSeinStructWireLimits Limits;
		Limits.MaxBytes = StateLimits.MaxEncodedBytes;
		Limits.MaxAggregateElements = StateLimits.MaxAggregateElements;
		Limits.MaxStringBytes = FMath::Min(
			StateLimits.MaxEncodedBytes, 1024 * 1024);
		Limits.MaxRecursionDepth = StateLimits.MaxRecursionDepth;
		Limits.MaxNativeAllocationBytes = StateLimits.MaxEncodedBytes;
		return Limits;
	}

	bool ComputeLeafDigest(
		const FGuid& DescriptorDigest,
		TConstArrayView<uint8> PayloadBytes,
		FGuid& OutLeafDigest,
		FString& OutError)
	{
		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.CanonicalState.Leaf"), 1);
		return Writer.WriteGuid(DescriptorDigest)
			&& Writer.WriteBytes(PayloadBytes)
			&& Writer.Finalize(OutLeafDigest, OutError);
	}

	bool EncodeContributorPayload(
		const FSeinFrozenCanonicalStateContributor& Contributor,
		const FInstancedStruct& Payload,
		TArray<uint8>& OutBytes,
		FGuid& OutLeafDigest,
		FString& OutError)
	{
		OutBytes.Reset();
		OutLeafDigest.Invalidate();
		if (!Payload.IsValid()
			|| Payload.GetScriptStruct()
				!= Contributor.Descriptor.PayloadStruct)
		{
			OutError =
				TEXT("Contributor returned the wrong frozen payload type.");
			return false;
		}

		FSeinWireCost Cost;
		if (!FSeinCanonicalStateCodec::EncodeWithCost(
			Payload.GetScriptStruct(),
			Payload.GetMemory(),
			{ Contributor.Descriptor.DynamicPayloadStructs,
				Contributor.Descriptor.AllowedNames },
			BuildWireLimits(Contributor.Descriptor.Limits),
			OutBytes,
			OutError,
			Cost)
			|| !ComputeLeafDigest(
				Contributor.DescriptorDigest,
				OutBytes,
				OutLeafDigest,
				OutError))
		{
			OutBytes.Reset();
			OutLeafDigest.Invalidate();
			return false;
		}
		return true;
	}

	bool DecodeContributorPayload(
		const FSeinFrozenCanonicalStateContributor& Contributor,
		const FSeinCanonicalStateContributorRecord& Record,
		FInstancedStruct& OutPayload,
		FString& OutError)
	{
		OutPayload.Reset();
		if (!Contributor.Descriptor.PayloadStruct
			|| Record.SchemaVersion
				!= static_cast<int32>(
					Contributor.Descriptor.SchemaVersion)
			|| Record.DescriptorDigest
				!= Contributor.DescriptorDigest
			|| Record.PayloadBytes.Num()
				> Contributor.Descriptor.Limits.MaxEncodedBytes)
		{
			OutError =
				TEXT("Contributor record does not match its frozen descriptor.");
			return false;
		}

		FGuid ExpectedLeaf;
		if (!ComputeLeafDigest(
				Contributor.DescriptorDigest,
				Record.PayloadBytes,
				ExpectedLeaf,
				OutError)
			|| ExpectedLeaf != Record.LeafDigest)
		{
			OutError =
				TEXT("Contributor record leaf digest is invalid.");
			return false;
		}

		OutPayload.InitializeAs(Contributor.Descriptor.PayloadStruct);
		FSeinWireCost Cost;
		if (!FSeinCanonicalStateCodec::DecodeWithCost(
			Record.PayloadBytes,
			Contributor.Descriptor.PayloadStruct,
			OutPayload.GetMutableMemory(),
			{ Contributor.Descriptor.DynamicPayloadStructs,
				Contributor.Descriptor.AllowedNames },
			BuildWireLimits(Contributor.Descriptor.Limits),
			OutError,
			Cost))
		{
			OutPayload.Reset();
			return false;
		}
		return true;
	}

	bool IsKnownRole(ESeinCanonicalStateRole Role)
	{
		switch (Role)
		{
		case ESeinCanonicalStateRole::Authoritative:
		case ESeinCanonicalStateRole::Continuation:
		case ESeinCanonicalStateRole::DerivedCache:
			return true;
		default:
			return false;
		}
	}

	int32 RoleRank(ESeinCanonicalStateRole Role)
	{
		switch (Role)
		{
		case ESeinCanonicalStateRole::Authoritative:
			return 0;
		case ESeinCanonicalStateRole::Continuation:
			return 1;
		case ESeinCanonicalStateRole::DerivedCache:
			return 2;
		default:
			return INDEX_NONE;
		}
	}

	const TCHAR* RoleName(ESeinCanonicalStateRole Role)
	{
		switch (Role)
		{
		case ESeinCanonicalStateRole::Authoritative:
			return TEXT("Authoritative");
		case ESeinCanonicalStateRole::Continuation:
			return TEXT("Continuation");
		case ESeinCanonicalStateRole::DerivedCache:
			return TEXT("DerivedCache");
		default:
			return TEXT("Unknown");
		}
	}

	bool ComputeSchemaDigest(
		const UScriptStruct* Struct,
		FGuid& OutDigest,
		FString& OutError)
	{
		if (!Struct)
		{
			OutError = TEXT("Payload struct is null.");
			return false;
		}
		if (!FSeinCanonicalStateCodec::ComputeSchemaDigest(
			Struct, OutDigest, OutError))
		{
			OutError = FString::Printf(
				TEXT("State schema '%s' is not canonical: %s"),
				*Struct->GetPathName(), *OutError);
			return false;
		}
		return true;
	}

	bool BuildCanonicalDescriptor(
		const FSeinCanonicalStateDescriptor& Descriptor,
		FString& OutCanonical,
		FGuid& OutDigest,
		FString& OutError)
	{
		const FString Key =
			FSeinCanonicalStateRegistry::CanonicalKey(Descriptor.Key);
		if (Key.IsEmpty()
			|| Descriptor.SchemaVersion == 0
			|| Descriptor.ImplementationRevision == 0
			|| !IsKnownRole(Descriptor.Role)
			|| Descriptor.Limits.MaxRecursionDepth <= 0
			|| Descriptor.Limits.MaxEncodedBytes <= 0
			|| Descriptor.Limits.MaxEncodedBytes
				> MaxNativeCheckpointBytes
			|| Descriptor.Limits.MaxAggregateElements <= 0)
		{
			OutError =
				TEXT("Expected stable ASCII IDs, positive bounded revisions and limits, and a known role.");
			return false;
		}

		const bool bDerived =
			Descriptor.Role == ESeinCanonicalStateRole::DerivedCache;
		if (bDerived != (Descriptor.PayloadStruct == nullptr))
		{
			OutError =
				TEXT("Derived caches must have no payload; persistent roles require one.");
			return false;
		}

		FGuid PayloadDigest;
		if (Descriptor.PayloadStruct
			&& !ComputeSchemaDigest(
				Descriptor.PayloadStruct,
				PayloadDigest,
				OutError))
		{
			return false;
		}

		struct FDynamicSchema
		{
			FString Path;
			FGuid Digest;
		};
		TArray<FDynamicSchema> DynamicSchemas;
		DynamicSchemas.Reserve(Descriptor.DynamicPayloadStructs.Num());
		for (const UScriptStruct* DynamicStruct :
			Descriptor.DynamicPayloadStructs)
		{
			FGuid DynamicDigest;
			if (!DynamicStruct
				|| !ComputeSchemaDigest(
					DynamicStruct,
					DynamicDigest,
					OutError))
			{
				return false;
			}
			DynamicSchemas.Add({ DynamicStruct->GetPathName(), DynamicDigest });
		}
		DynamicSchemas.Sort(
			[](const FDynamicSchema& A, const FDynamicSchema& B)
			{
				return A.Path < B.Path;
			});
		for (int32 Index = 1; Index < DynamicSchemas.Num(); ++Index)
		{
			if (DynamicSchemas[Index - 1].Path == DynamicSchemas[Index].Path)
			{
				OutError = TEXT("Dynamic payload schema paths must be unique.");
				return false;
			}
		}

		TArray<FName> CanonicalNames;
		FString NameManifest;
		SeinBuildCanonicalWireNameCatalog(
			Descriptor.AllowedNames, CanonicalNames, NameManifest);

		TArray<FString> Dependencies;
		Dependencies.Reserve(Descriptor.RestoreAfter.Num());
		for (const FSeinCanonicalStateKey& Dependency :
			Descriptor.RestoreAfter)
		{
			const FString DependencyKey =
				FSeinCanonicalStateRegistry::CanonicalKey(Dependency);
			if (DependencyKey.IsEmpty() || DependencyKey == Key)
			{
				OutError =
					TEXT("Restore dependencies must be valid and may not reference self.");
				return false;
			}
			Dependencies.Add(DependencyKey);
		}
		Dependencies.Sort();
		for (int32 Index = 1; Index < Dependencies.Num(); ++Index)
		{
			if (Dependencies[Index - 1] == Dependencies[Index])
			{
				OutError = TEXT("Restore dependencies must be unique.");
				return false;
			}
		}

		OutCanonical = TEXT("SeinARTS.CanonicalState.Descriptor\n");
		AppendFramed(OutCanonical, Key);
		AppendFramed(
			OutCanonical,
			LexToString(Descriptor.SchemaVersion));
		AppendFramed(
			OutCanonical,
			LexToString(Descriptor.ImplementationRevision));
		AppendFramed(
			OutCanonical,
			LexToString(static_cast<uint8>(Descriptor.Role)));
		AppendFramed(
			OutCanonical,
			Descriptor.PayloadStruct
				? Descriptor.PayloadStruct->GetPathName()
				: TEXT("<derived>"));
		AppendFramed(
			OutCanonical,
			PayloadDigest.ToString(EGuidFormats::Digits));
		AppendFramed(
			OutCanonical,
			LexToString(Descriptor.Limits.MaxRecursionDepth));
		AppendFramed(
			OutCanonical,
			LexToString(Descriptor.Limits.MaxEncodedBytes));
		AppendFramed(
			OutCanonical,
			LexToString(Descriptor.Limits.MaxAggregateElements));
		AppendFramed(OutCanonical, LexToString(DynamicSchemas.Num()));
		for (const FDynamicSchema& Dynamic : DynamicSchemas)
		{
			AppendFramed(OutCanonical, Dynamic.Path);
			AppendFramed(
				OutCanonical,
				Dynamic.Digest.ToString(EGuidFormats::Digits));
		}
		AppendFramed(OutCanonical, NameManifest);
		AppendFramed(OutCanonical, LexToString(Dependencies.Num()));
		for (const FString& Dependency : Dependencies)
		{
			AppendFramed(OutCanonical, Dependency);
		}
		OutDigest = DigestUtf8(OutCanonical);
		return OutDigest.IsValid();
	}

	bool ValidateOps(
		const FSeinCanonicalStateDescriptor& Descriptor,
		const FSeinCanonicalStateContributorOps& Ops,
		FString& OutError)
	{
		if (Descriptor.Role == ESeinCanonicalStateRole::DerivedCache)
		{
			if (!Ops.StageDerived || !Ops.CommitDerived)
			{
				OutError =
					TEXT("A derived-cache contributor requires stage and commit callbacks.");
				return false;
			}
			return true;
		}
		if (!Ops.Capture || !Ops.StageRestore || !Ops.CommitRestore)
		{
			OutError =
				TEXT("A persistent contributor requires capture, stage, and commit callbacks.");
			return false;
		}
		return true;
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

	bool BuildTopologicalOrder(
		const TArray<const FProviderClaim*>& Claims,
		TArray<int32>& OutOrder,
		FString& OutError)
	{
		TArray<const FSeinCanonicalStateDescriptor*> Descriptors;
		Descriptors.Reserve(Claims.Num());
		for (const FProviderClaim* Claim : Claims)
		{
			Descriptors.Add(Claim ? &Claim->Descriptor : nullptr);
		}
		return FSeinCanonicalStateRegistry::BuildCanonicalRestoreOrder(
			Descriptors, OutOrder, OutError);
	}
}

struct FSeinCanonicalStateSchemaSnapshot::FData
{
	TArray<FSeinFrozenCanonicalStateContributor> Contributors;
	TArray<TStrongObjectPtr<UScriptStruct>> TypeRoots;
	FString CanonicalManifest;
	FGuid ContractDigest;
};

struct FSeinCanonicalStateRestorePlan::FData
{
	struct FItem
	{
		FString CanonicalKey;
		uint64 ProviderToken = 0;
		bool bDerived = false;
		FSeinCanonicalStateContributorOps Ops;
		TUniquePtr<ISeinCanonicalStateRestoreStage> Stage;
		TArray<TStrongObjectPtr<UObject>> RootedObjects;
	};

	TArray<FItem> Items;
	TMap<FString, FInstancedStruct> StagedPayloads;
	bool bReady = false;
};

FSeinCanonicalStateRestorePlan::FSeinCanonicalStateRestorePlan()
{
	check(IsInGameThread());
}

FSeinCanonicalStateRestorePlan::~FSeinCanonicalStateRestorePlan()
{
	check(IsInGameThread());
	Reset();
}

FSeinCanonicalStateRestorePlan::FSeinCanonicalStateRestorePlan(
	FSeinCanonicalStateRestorePlan&& Other) noexcept
{
	check(IsInGameThread());
	Data = MoveTemp(Other.Data);
}

FSeinCanonicalStateRestorePlan&
FSeinCanonicalStateRestorePlan::operator=(
	FSeinCanonicalStateRestorePlan&& Other) noexcept
{
	check(IsInGameThread());
	if (this != &Other)
	{
		Reset();
		Data = MoveTemp(Other.Data);
	}
	return *this;
}

bool FSeinCanonicalStateRestorePlan::IsReady() const
{
	check(IsInGameThread());
	return Data && Data->bReady;
}

void FSeinCanonicalStateRestorePlan::Reset()
{
	check(IsInGameThread());
	if (!Data)
	{
		return;
	}
	if (IsProviderInvocationActive())
	{
		Data.Reset();
		return;
	}

	// Restore stages and copied provider operations own module vtables and
	// callable captures. Their destructors are executable provider code too.
	FProviderInvocationScope InvocationScope;
	Data.Reset();
}

bool FSeinCanonicalStateRestorePlan::VerifyProviderLeases(
	FString& OutError) const
{
	check(IsInGameThread());
	OutError.Reset();
	if (!Data || !Data->bReady)
	{
		OutError = TEXT("Canonical state restore plan is not ready.");
		return false;
	}

	{
		FScopeLock Lock(&RegistryMutex());
		for (const FData::FItem& Item : Data->Items)
		{
			const FRegisteredContributor* Registered =
				Registry().FindByPredicate(
					[&Item](const FRegisteredContributor& Candidate)
					{
						return Candidate.CanonicalKey == Item.CanonicalKey;
					});
			const bool bExactGenerationAvailable =
				Registered
				&& Registered->Claims.ContainsByPredicate(
					[&Item](const FProviderClaim& Claim)
					{
						return Claim.Token == Item.ProviderToken;
					});
			if (!bExactGenerationAvailable)
			{
				OutError = FString::Printf(
					TEXT("%s: frozen provider generation became unavailable before commit."),
					*Item.CanonicalKey);
				return false;
			}
		}
	}

	{
		// Never invoke module code while holding Core's registry lock. The
		// shared scope also blocks cross-registry mutation from a nested lease.
		FProviderInvocationScope InvocationScope;
		for (const FData::FItem& Item : Data->Items)
		{
			FString ExternalError;
			if (Item.Stage
				&& !Item.Stage->VerifyExternalLeases(ExternalError))
			{
				OutError = FString::Printf(
					TEXT("%s: %s"),
					*Item.CanonicalKey,
					ExternalError.IsEmpty()
						? TEXT("nested provider generation became unavailable before commit.")
						: *ExternalError);
				return false;
			}
		}
	}
	return true;
}

const FInstancedStruct*
FSeinCanonicalStateRestorePlan::FindStagedPayload(
	const FSeinCanonicalStateKey& Key) const
{
	check(IsInGameThread());
	if (!Data || !Data->bReady)
	{
		return nullptr;
	}
	const FString Canonical =
		FSeinCanonicalStateRegistry::CanonicalKey(Key);
	return Data->StagedPayloads.Find(Canonical);
}

bool FSeinCanonicalStateRegistry::PrepareWorldBindings(
	const FSeinCanonicalStateSchemaSnapshot& Schema,
	const FSeinCanonicalStateWorldBindingContext& Context,
	FString& OutError)
{
	OutError.Reset();
	if (!IsInGameThread() || !Schema.IsValid())
	{
		OutError =
			TEXT("Canonical state world preparation requires a valid frozen schema on the game thread.");
		return false;
	}
	if (IsProviderInvocationActive())
	{
		OutError =
			TEXT("Canonical state world preparation may not re-enter a provider callback transaction.");
		return false;
	}
	FProviderInvocationScope InvocationScope;

	for (const FSeinFrozenCanonicalStateContributor& Contributor :
		Schema.GetContributors())
	{
		FSeinCanonicalStateContributorOps Ops;
		if (!ResolveProvider(
			Contributor.ProviderToken, Ops, &OutError))
		{
			return false;
		}
		if (!Ops.PrepareWorldBinding)
		{
			continue;
		}
		if (!Ops.PrepareWorldBinding(Context, OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Contributor world-binding preparation failed.");
			}
			OutError = FString::Printf(
				TEXT("%s: %s"),
				*CanonicalKey(Contributor.Descriptor.Key),
				*OutError);
			return false;
		}
	}
	return true;
}

bool FSeinCanonicalStateRegistry::CaptureWorldBindingFrames(
	const FSeinCanonicalStateSchemaSnapshot& Schema,
	const FSeinCanonicalStateWorldBindingContext& Context,
	TArray<FString>& OutFrames,
	FString& OutError)
{
	OutFrames.Reset();
	OutError.Reset();
	if (!IsInGameThread() || !Schema.IsValid())
	{
		OutError =
			TEXT("Canonical state world binding requires a valid frozen schema on the game thread.");
		return false;
	}
	if (IsProviderInvocationActive())
	{
		OutError =
			TEXT("Canonical state world binding may not re-enter a provider callback transaction.");
		return false;
	}
	FProviderInvocationScope InvocationScope;

	TArray<FString> Candidate;
	for (const FSeinFrozenCanonicalStateContributor& Contributor :
		Schema.GetContributors())
	{
		FSeinCanonicalStateContributorOps Ops;
		if (!ResolveProvider(
			Contributor.ProviderToken, Ops, &OutError))
		{
			return false;
		}
		if (!Ops.FreezeWorldBinding)
		{
			continue;
		}

		FString ProviderFrame;
		if (!Ops.FreezeWorldBinding(
				Context, ProviderFrame, OutError)
			|| ProviderFrame.IsEmpty())
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Contributor returned an empty world-binding frame.");
			}
			OutError = FString::Printf(
				TEXT("%s: %s"),
				*CanonicalKey(Contributor.Descriptor.Key),
				*OutError);
			return false;
		}

		FString BoundFrame =
			TEXT("SeinARTS.CanonicalState.WorldBinding\n");
		AppendFramed(
			BoundFrame,
			CanonicalKey(Contributor.Descriptor.Key));
		AppendFramed(BoundFrame, ProviderFrame);
		Candidate.Add(MoveTemp(BoundFrame));
	}

	OutFrames = MoveTemp(Candidate);
	return true;
}

void FSeinCanonicalStateRestorePlan::Commit(
	FSeinCanonicalStateCommitContext& Context)
{
	check(IsInGameThread());
	check(IsReady());
	if (IsProviderInvocationActive())
	{
		UE_LOG(
			LogSeinCanonicalState,
			Fatal,
			TEXT("Canonical-state commit re-entered a provider callback transaction."));
	}
	FString LeaseError;
	checkf(
		VerifyProviderLeases(LeaseError),
		TEXT("Canonical state provider lease invalidated after restore adoption: %s"),
		*LeaseError);
	FProviderInvocationScope InvocationScope;
	for (FData::FItem& Item : Data->Items)
	{
		if (Item.bDerived)
		{
			check(Item.Ops.CommitDerived);
			Item.Ops.CommitDerived(Context, MoveTemp(Item.Stage));
		}
		else
		{
			check(Item.Ops.CommitRestore);
			Item.Ops.CommitRestore(Context, MoveTemp(Item.Stage));
		}
	}
	Reset();
}

FSeinCanonicalStateRegistrationHandle::~FSeinCanonicalStateRegistrationHandle()
{
	Reset();
}

FSeinCanonicalStateRegistrationHandle::FSeinCanonicalStateRegistrationHandle(
	FSeinCanonicalStateRegistrationHandle&& Other) noexcept
	: Token(Other.Token)
{
	check(IsInGameThread());
	Other.Token = 0;
}

FSeinCanonicalStateRegistrationHandle&
FSeinCanonicalStateRegistrationHandle::operator=(
	FSeinCanonicalStateRegistrationHandle&& Other) noexcept
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

void FSeinCanonicalStateRegistrationHandle::Reset()
{
	if (Token != 0)
	{
		check(IsInGameThread());
		if (IsProviderInvocationActive())
		{
			UE_LOG(
				LogSeinCanonicalState,
				Fatal,
				TEXT("A canonical-state registration handle was destroyed from inside a provider callback transaction."));
		}
		const bool bUnregistered =
			FSeinCanonicalStateRegistry::UnregisterToken(Token);
		check(bUnregistered);
		Token = 0;
	}
}

int32 FSeinCanonicalStateSchemaSnapshot::GetContributorCount() const
{
	return Data.IsValid() ? Data->Contributors.Num() : 0;
}

const FString&
FSeinCanonicalStateSchemaSnapshot::GetCanonicalManifest() const
{
	static const FString Empty;
	return Data.IsValid() ? Data->CanonicalManifest : Empty;
}

FGuid FSeinCanonicalStateSchemaSnapshot::GetContractDigest() const
{
	return Data.IsValid() ? Data->ContractDigest : FGuid();
}

TConstArrayView<FSeinFrozenCanonicalStateContributor>
FSeinCanonicalStateSchemaSnapshot::GetContributors() const
{
	return Data.IsValid()
		? TConstArrayView<FSeinFrozenCanonicalStateContributor>(
			Data->Contributors)
		: TConstArrayView<FSeinFrozenCanonicalStateContributor>();
}

FSeinCanonicalStateRegistrationHandle
FSeinCanonicalStateRegistry::Register(
	FName OwnerModuleId,
	const FSeinCanonicalStateDescriptor& Descriptor,
	FSeinCanonicalStateContributorOps Ops,
	FString* OutError)
{
	check(IsInGameThread());
	SetError(OutError, FString());
	if (IsProviderInvocationActive())
	{
		const FString Error =
			TEXT("Canonical-state providers may not register during a provider callback transaction.");
		SetError(OutError, Error);
		UE_LOG(LogSeinCanonicalState, Error, TEXT("%s"), *Error);
		return {};
	}
	const FString Owner = CanonicalName(OwnerModuleId);
	FSeinCanonicalStateDescriptor CanonicalDescriptorValue;
	FString CanonicalDescriptor;
	FGuid DescriptorDigest;
	FString Error;
	if (!IsStableASCIIIdentifier(Owner)
		|| !CanonicalizeDescriptorCollections(
			Descriptor, CanonicalDescriptorValue, Error)
		|| !BuildCanonicalDescriptor(
			CanonicalDescriptorValue,
			CanonicalDescriptor,
			DescriptorDigest,
			Error)
		|| !ValidateOps(CanonicalDescriptorValue, Ops, Error))
	{
		if (Error.IsEmpty())
		{
			Error = TEXT("Owner module ID must be stable lowercase-compatible ASCII.");
		}
		SetError(OutError, Error);
		UE_LOG(LogSeinCanonicalState, Error,
			TEXT("Rejected canonical state contributor: %s"), *Error);
		return {};
	}

	const FString Key = CanonicalKey(Descriptor.Key);
	FScopeLock Lock(&RegistryMutex());
	FRegisteredContributor* Registered =
		Registry().FindByPredicate(
			[&Key](const FRegisteredContributor& Candidate)
			{
				return Candidate.CanonicalKey == Key;
			});
	if (Registered)
	{
		if (Registered->CanonicalDescriptor != CanonicalDescriptor
			|| Registered->Claims.IsEmpty()
			|| Registered->Claims[0].Owner != Owner)
		{
			Error = FString::Printf(
				TEXT("Conflicting canonical state contributor '%s'."),
				*Key);
			SetError(OutError, Error);
			UE_LOG(LogSeinCanonicalState, Error, TEXT("%s"), *Error);
			return {};
		}
	}
	else
	{
		Registered = &Registry().AddDefaulted_GetRef();
		Registered->CanonicalKey = Key;
		Registered->CanonicalDescriptor = CanonicalDescriptor;
		Registered->DescriptorDigest = DescriptorDigest;
	}

	FProviderClaim Claim;
	Claim.Token = AllocateToken();
	Claim.Owner = Owner;
	Claim.Descriptor = CanonicalDescriptorValue;
	Claim.Ops = MoveTemp(Ops);
	if (CanonicalDescriptorValue.PayloadStruct)
	{
		Claim.PayloadRoot.Reset(
			const_cast<UScriptStruct*>(
				CanonicalDescriptorValue.PayloadStruct));
	}
	Claim.DynamicRoots.Reserve(
		CanonicalDescriptorValue.DynamicPayloadStructs.Num());
	for (const UScriptStruct* Dynamic :
		CanonicalDescriptorValue.DynamicPayloadStructs)
	{
		Claim.DynamicRoots.Emplace(const_cast<UScriptStruct*>(Dynamic));
	}
	const uint64 Token = Claim.Token;
	Registered->Claims.Add(MoveTemp(Claim));
	return FSeinCanonicalStateRegistrationHandle(Token);
}

bool FSeinCanonicalStateRegistry::Unregister(
	FSeinCanonicalStateRegistrationHandle& Handle)
{
	check(IsInGameThread());
	if (!Handle.IsValid())
	{
		return false;
	}
	if (IsProviderInvocationActive())
	{
		UE_LOG(
			LogSeinCanonicalState,
			Error,
			TEXT("Canonical-state providers may not unregister during a provider callback transaction."));
		return false;
	}
	const uint64 Token = Handle.Token;
	if (!UnregisterToken(Token))
	{
		return false;
	}
	Handle.Token = 0;
	return true;
}

bool FSeinCanonicalStateRegistry::UnregisterToken(uint64 Token)
{
	check(IsInGameThread());
	check(!IsProviderInvocationActive());
	if (Token == 0)
	{
		return false;
	}

	FString Owner;
	{
		FScopeLock Lock(&RegistryMutex());
		for (const FRegisteredContributor& Registered : Registry())
		{
			const FProviderClaim* Claim =
				Registered.Claims.FindByPredicate(
					[Token](const FProviderClaim& Candidate)
					{
						return Candidate.Token == Token;
					});
			if (Claim)
			{
				Owner = Claim->Owner;
				break;
			}
		}
	}
	if (Owner.IsEmpty())
	{
		return false;
	}

	// A frozen world holds this exact generation, not merely its descriptor.
	// Terminate it while module callbacks are still callable, matching latent
	// codec withdrawal semantics and preventing post-unload capture/restore.
	for (TObjectIterator<USeinWorldSubsystem> It; It; ++It)
	{
		if (It->HasAnyFlags(RF_ClassDefaultObject)
			|| !It->NativeCanonicalStateSchema.IsValid())
		{
			continue;
		}
		const bool bUsesToken =
			It->NativeCanonicalStateSchema.GetContributors()
				.ContainsByPredicate(
					[Token](
						const FSeinFrozenCanonicalStateContributor&
							Contributor)
					{
						return Contributor.ProviderToken == Token;
					});
		if (bUsesToken)
		{
			It->TerminateAndReleaseForModuleUnload(
				FName(*Owner),
				TEXT("its frozen canonical-state provider generation unloaded"));
		}
	}

	FProviderClaim RemovedClaim;
	bool bRemoved = false;
	{
		FScopeLock Lock(&RegistryMutex());
		for (int32 EntryIndex = 0;
			EntryIndex < Registry().Num();
			++EntryIndex)
		{
			FRegisteredContributor& Registered = Registry()[EntryIndex];
			const int32 ClaimIndex =
				Registered.Claims.IndexOfByPredicate(
					[Token](const FProviderClaim& Claim)
					{
						return Claim.Token == Token;
					});
			if (ClaimIndex == INDEX_NONE)
			{
				continue;
			}
			RemovedClaim =
				MoveTemp(Registered.Claims[ClaimIndex]);
			Registered.Claims.RemoveAt(ClaimIndex);
			if (Registered.Claims.IsEmpty())
			{
				Registry().RemoveAt(EntryIndex);
			}
			bRemoved = true;
			break;
		}
	}
	if (!bRemoved)
	{
		return false;
	}
	{
		// Provider operation captures may own executable destructors. Destroy
		// them only after the registry structure and mutex are stable.
		FProviderInvocationScope InvocationScope;
		RemovedClaim = {};
	}
	return true;
}

FSeinCanonicalStateSchemaSnapshot
FSeinCanonicalStateRegistry::CaptureSchemaSnapshot(FString* OutError)
{
	check(IsInGameThread());
	SetError(OutError, FString());
	if (IsProviderInvocationActive())
	{
		SetError(
			OutError,
			TEXT("Canonical state schema capture may not re-enter a provider callback transaction."));
		return {};
	}
	TSharedRef<FSeinCanonicalStateSchemaSnapshot::FData, ESPMode::ThreadSafe>
		Data =
			MakeShared<
				FSeinCanonicalStateSchemaSnapshot::FData,
				ESPMode::ThreadSafe>();

	FString Error;
	{
		FScopeLock Lock(&RegistryMutex());
		TArray<const FProviderClaim*> Claims;
		TArray<const FRegisteredContributor*> Entries;
		Claims.Reserve(Registry().Num());
		Entries.Reserve(Registry().Num());
		for (const FRegisteredContributor& Registered : Registry())
		{
			if (Registered.Claims.IsEmpty())
			{
				continue;
			}
			// New worlds select the newest exact provider generation. Existing
			// worlds retain their already-frozen older token.
			Claims.Add(&Registered.Claims.Last());
			Entries.Add(&Registered);
		}

		TArray<int32> RestoreOrder;
		if (!BuildTopologicalOrder(Claims, RestoreOrder, Error))
		{
			SetError(OutError, Error);
			UE_LOG(LogSeinCanonicalState, Error,
				TEXT("Failed to freeze state contract: %s"), *Error);
			return {};
		}

		Data->Contributors.Reserve(Claims.Num());
		for (const int32 Index : RestoreOrder)
		{
			const FProviderClaim& Claim = *Claims[Index];
			const FRegisteredContributor& Registered = *Entries[Index];
			FSeinFrozenCanonicalStateContributor& Frozen =
				Data->Contributors.AddDefaulted_GetRef();
			Frozen.Descriptor = Claim.Descriptor;
			Frozen.DescriptorDigest = Registered.DescriptorDigest;
			Frozen.ProviderToken = Claim.Token;
			if (Claim.Descriptor.PayloadStruct)
			{
				Data->TypeRoots.Emplace(
					const_cast<UScriptStruct*>(
						Claim.Descriptor.PayloadStruct));
			}
			for (const UScriptStruct* Dynamic :
				Claim.Descriptor.DynamicPayloadStructs)
			{
				Data->TypeRoots.Emplace(
					const_cast<UScriptStruct*>(Dynamic));
			}
		}

		Entries.Sort(
			[](const FRegisteredContributor& A,
				const FRegisteredContributor& B)
			{
				return A.CanonicalKey < B.CanonicalKey;
			});
		Data->CanonicalManifest =
			TEXT("SeinARTS.CanonicalState.Contract\n");
		AppendFramed(
			Data->CanonicalManifest,
			LexToString(ContractFormatVersion));
		AppendFramed(
			Data->CanonicalManifest,
			LexToString(Entries.Num()));
		for (const FRegisteredContributor* Entry : Entries)
		{
			AppendFramed(
				Data->CanonicalManifest,
				Entry->CanonicalDescriptor);
		}
	}

	Data->ContractDigest = DigestUtf8(Data->CanonicalManifest);
	if (!Data->ContractDigest.IsValid())
	{
		SetError(OutError, TEXT("State contract digest is invalid."));
		return {};
	}

	FSeinCanonicalStateSchemaSnapshot Snapshot;
	Snapshot.Data = Data;
	return Snapshot;
}

bool FSeinCanonicalStateRegistry::BuildCanonicalRestoreOrder(
	TConstArrayView<const FSeinCanonicalStateDescriptor*> Descriptors,
	TArray<int32>& OutOrder,
	FString& OutError)
{
	OutOrder.Reset();
	OutError.Reset();

	TMap<FString, int32> Indices;
	Indices.Reserve(Descriptors.Num());
	for (int32 Index = 0; Index < Descriptors.Num(); ++Index)
	{
		const FSeinCanonicalStateDescriptor* Descriptor =
			Descriptors[Index];
		const FString Key = Descriptor
			? CanonicalKey(Descriptor->Key)
			: FString();
		if (!Descriptor || Key.IsEmpty()
			|| !IsKnownRole(Descriptor->Role)
			|| Indices.Contains(Key))
		{
			OutError =
				TEXT("Canonical state restore descriptors contain a null, invalid, unknown-role, or duplicate entry.");
			return false;
		}
		Indices.Add(Key, Index);
	}

	TArray<int32> Incoming;
	Incoming.Init(0, Descriptors.Num());
	TArray<TArray<int32>> Dependents;
	Dependents.SetNum(Descriptors.Num());
	for (int32 Index = 0; Index < Descriptors.Num(); ++Index)
	{
		const FSeinCanonicalStateDescriptor& Descriptor =
			*Descriptors[Index];
		const int32 DescriptorRole = RoleRank(Descriptor.Role);
		for (const FSeinCanonicalStateKey& Dependency :
			Descriptor.RestoreAfter)
		{
			const FString DependencyKey = CanonicalKey(Dependency);
			const int32* DependencyIndex = Indices.Find(DependencyKey);
			if (!DependencyIndex)
			{
				OutError = FString::Printf(
					TEXT("Contributor '%s' depends on a missing contributor '%s'."),
					*CanonicalKey(Descriptor.Key),
					*DependencyKey);
				return false;
			}

			const FSeinCanonicalStateDescriptor& DependencyDescriptor =
				*Descriptors[*DependencyIndex];
			if (RoleRank(DependencyDescriptor.Role) > DescriptorRole)
			{
				OutError = FString::Printf(
					TEXT("Contributor '%s' (%s) cannot restore after '%s' (%s): ")
					TEXT("the canonical state role barrier is Authoritative -> Continuation -> DerivedCache."),
					*CanonicalKey(Descriptor.Key),
					RoleName(Descriptor.Role),
					*DependencyKey,
					RoleName(DependencyDescriptor.Role));
				return false;
			}

			++Incoming[Index];
			Dependents[*DependencyIndex].Add(Index);
		}
	}

	TArray<int32> Ready;
	for (int32 Index = 0; Index < Incoming.Num(); ++Index)
	{
		if (Incoming[Index] == 0)
		{
			Ready.Add(Index);
		}
	}
	const auto SortReady =
		[Descriptors](TArray<int32>& Values)
		{
			Values.Sort(
				[Descriptors](int32 A, int32 B)
				{
					const FSeinCanonicalStateDescriptor& ADescriptor =
						*Descriptors[A];
					const FSeinCanonicalStateDescriptor& BDescriptor =
						*Descriptors[B];
					const int32 ARole = RoleRank(ADescriptor.Role);
					const int32 BRole = RoleRank(BDescriptor.Role);
					if (ARole != BRole)
					{
						// Pop() consumes the back: later roles sort first so
						// the earliest hard barrier is consumed first.
						return ARole > BRole;
					}
					return CanonicalKey(ADescriptor.Key)
						> CanonicalKey(BDescriptor.Key);
				});
		};
	SortReady(Ready);

	OutOrder.Reserve(Descriptors.Num());
	while (!Ready.IsEmpty())
	{
		const int32 Index = Ready.Pop(EAllowShrinking::No);
		OutOrder.Add(Index);
		for (const int32 Dependent : Dependents[Index])
		{
			if (--Incoming[Dependent] == 0)
			{
				Ready.Add(Dependent);
			}
		}
		SortReady(Ready);
	}
	if (OutOrder.Num() != Descriptors.Num())
	{
		OutOrder.Reset();
		OutError =
			TEXT("Canonical state restore dependencies contain a cycle.");
		return false;
	}
	return true;
}

bool FSeinCanonicalStateRegistry::ResolveProvider(
	uint64 ProviderToken,
	FSeinCanonicalStateContributorOps& OutOps,
	FString* OutError)
{
	check(IsInGameThread());
	OutOps = {};
	SetError(OutError, FString());
	if (ProviderToken == 0)
	{
		SetError(OutError, TEXT("Provider token is invalid."));
		return false;
	}

	FScopeLock Lock(&RegistryMutex());
	for (const FRegisteredContributor& Registered : Registry())
	{
		const FProviderClaim* Claim =
			Registered.Claims.FindByPredicate(
				[ProviderToken](const FProviderClaim& Candidate)
				{
					return Candidate.Token == ProviderToken;
				});
		if (Claim)
		{
			OutOps = Claim->Ops;
			return true;
		}
	}
	SetError(
		OutError,
		TEXT("The frozen provider generation is unavailable (module unloaded or reloaded)."));
	return false;
}

bool FSeinCanonicalStateRegistry::CaptureContributorRecords(
	const FSeinCanonicalStateSchemaSnapshot& Schema,
	const FSeinCanonicalStateCaptureContext& Context,
	TArray<FSeinCanonicalStateContributorRecord>& OutRecords,
	FString& OutError)
{
	OutError.Reset();
	if (!IsInGameThread() || !Schema.IsValid())
	{
		OutError =
			TEXT("Canonical state capture requires a valid frozen schema on the game thread.");
		return false;
	}
	if (IsProviderInvocationActive())
	{
		OutError =
			TEXT("Canonical state capture may not re-enter a provider callback transaction.");
		return false;
	}
	FProviderInvocationScope InvocationScope;

	TArray<FSeinCanonicalStateContributorRecord> Candidate;
	Candidate.Reserve(Schema.GetContributorCount());
	uint64 AggregatePayloadBytes = 0;
	for (const FSeinFrozenCanonicalStateContributor& Contributor :
		Schema.GetContributors())
	{
		FSeinCanonicalStateContributorOps Ops;
		if (!ResolveProvider(
			Contributor.ProviderToken, Ops, &OutError))
		{
			return false;
		}
		if (Contributor.Descriptor.Role
			== ESeinCanonicalStateRole::DerivedCache)
		{
			continue;
		}

		FInstancedStruct Payload;
		if (!Ops.Capture
			|| !Ops.Capture(Context, Payload, OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError = TEXT("Contributor capture callback failed.");
			}
			OutError = FString::Printf(
				TEXT("%s: %s"),
				*CanonicalKey(Contributor.Descriptor.Key),
				*OutError);
			return false;
		}

		FSeinCanonicalStateContributorRecord& Record =
			Candidate.AddDefaulted_GetRef();
		Record.Key = Contributor.Descriptor.Key;
		Record.SchemaVersion = static_cast<int32>(
			Contributor.Descriptor.SchemaVersion);
		Record.DescriptorDigest = Contributor.DescriptorDigest;
		if (!EncodeContributorPayload(
			Contributor,
			Payload,
			Record.PayloadBytes,
			Record.LeafDigest,
			OutError))
		{
			OutError = FString::Printf(
				TEXT("%s: %s"),
				*CanonicalKey(Contributor.Descriptor.Key),
				*OutError);
			return false;
		}
		AggregatePayloadBytes +=
			static_cast<uint64>(Record.PayloadBytes.Num());
		if (AggregatePayloadBytes
			> static_cast<uint64>(MaxNativeCheckpointBytes))
		{
			OutError =
				TEXT("Native canonical state exceeds the aggregate checkpoint bound.");
			return false;
		}
	}

	Candidate.Sort(
		[](const FSeinCanonicalStateContributorRecord& A,
			const FSeinCanonicalStateContributorRecord& B)
		{
			return FSeinCanonicalStateRegistry::CanonicalKey(A.Key)
				< FSeinCanonicalStateRegistry::CanonicalKey(B.Key);
		});
	OutRecords = MoveTemp(Candidate);
	return true;
}

bool FSeinCanonicalStateRegistry::TryStageContributorRestore(
	const FSeinCanonicalStateSchemaSnapshot& Schema,
	const FSeinCanonicalStateStageContext& Context,
	TConstArrayView<FSeinCanonicalStateContributorRecord> Records,
	FSeinCanonicalStateRestorePlan& OutPlan,
	FString& OutError)
{
	OutPlan.Reset();
	OutError.Reset();
	if (!IsInGameThread() || !Schema.IsValid())
	{
		OutError =
			TEXT("Canonical state restore requires a valid frozen schema on the game thread.");
		return false;
	}
	if (IsProviderInvocationActive())
	{
		OutError =
			TEXT("Canonical state restore may not re-enter a provider callback transaction.");
		return false;
	}
	FProviderInvocationScope InvocationScope;

	TMap<FString, const FSeinCanonicalStateContributorRecord*> RecordsByKey;
	FString PreviousKey;
	uint64 AggregatePayloadBytes = 0;
	for (const FSeinCanonicalStateContributorRecord& Record : Records)
	{
		const FString Key = CanonicalKey(Record.Key);
		if (Key.IsEmpty()
			|| (!PreviousKey.IsEmpty() && !(PreviousKey < Key))
			|| Record.SchemaVersion <= 0
			|| !Record.DescriptorDigest.IsValid()
			|| !Record.LeafDigest.IsValid())
		{
			OutError =
				TEXT("Native canonical state records are malformed or not in strict canonical order.");
			return false;
		}
		AggregatePayloadBytes +=
			static_cast<uint64>(Record.PayloadBytes.Num());
		if (AggregatePayloadBytes
			> static_cast<uint64>(MaxNativeCheckpointBytes))
		{
			OutError =
				TEXT("Native canonical state exceeds the aggregate checkpoint bound.");
			return false;
		}
		PreviousKey = Key;
		RecordsByKey.Add(Key, &Record);
	}

	int32 ExpectedPersistentRecords = 0;
	for (const FSeinFrozenCanonicalStateContributor& Contributor :
		Schema.GetContributors())
	{
		if (Contributor.Descriptor.Role
			!= ESeinCanonicalStateRole::DerivedCache)
		{
			++ExpectedPersistentRecords;
		}
	}
	if (Records.Num() != ExpectedPersistentRecords)
	{
		OutError =
			TEXT("Native canonical state record count does not match the frozen schema.");
		return false;
	}

	TUniquePtr<FSeinCanonicalStateRestorePlan::FData> Candidate =
		MakeUnique<FSeinCanonicalStateRestorePlan::FData>();
	Candidate->Items.Reserve(Schema.GetContributorCount());
	TMap<FString, FInstancedStruct> StagedPayloads;
	StagedPayloads.Reserve(ExpectedPersistentRecords);
	const auto GatherStageRoots =
		[&OutError](
			FSeinCanonicalStateRestorePlan::FData::FItem& Item)
	{
		if (!Item.Stage)
		{
			return true;
		}

		TArray<UObject*> ReportedObjects;
		Item.Stage->GatherReferencedObjects(ReportedObjects);
		TSet<UObject*> UniqueObjects;
		UniqueObjects.Reserve(ReportedObjects.Num());
		for (UObject* Object : ReportedObjects)
		{
			if (!IsValid(Object) || UniqueObjects.Contains(Object))
			{
				OutError = FString::Printf(
					TEXT("%s: restore stage reported an invalid or duplicate UObject root."),
					*Item.CanonicalKey);
				return false;
			}
			UniqueObjects.Add(Object);
		}

		Item.RootedObjects.Reserve(ReportedObjects.Num());
		for (UObject* Object : ReportedObjects)
		{
			Item.RootedObjects.Emplace(Object);
		}
		return true;
	};
	for (const FSeinFrozenCanonicalStateContributor& Contributor :
		Schema.GetContributors())
	{
		TSet<FString> AllowedDependencyKeys;
		AllowedDependencyKeys.Reserve(
			Contributor.Descriptor.RestoreAfter.Num());
		for (const FSeinCanonicalStateKey& Dependency :
			Contributor.Descriptor.RestoreAfter)
		{
			AllowedDependencyKeys.Add(CanonicalKey(Dependency));
		}
		const FStagedPayloadView DependencyView(
			StagedPayloads,
			AllowedDependencyKeys);
		FSeinCanonicalStateStageContext ContributorContext = Context;
		ContributorContext.Dependencies = &DependencyView;

		FSeinCanonicalStateContributorOps Ops;
		if (!ResolveProvider(
			Contributor.ProviderToken, Ops, &OutError))
		{
			return false;
		}

		FSeinCanonicalStateRestorePlan::FData::FItem& Item =
			Candidate->Items.AddDefaulted_GetRef();
		Item.CanonicalKey = CanonicalKey(Contributor.Descriptor.Key);
		Item.ProviderToken = Contributor.ProviderToken;
		Item.bDerived = Contributor.Descriptor.Role
			== ESeinCanonicalStateRole::DerivedCache;
		Item.Ops = MoveTemp(Ops);

		if (Item.bDerived)
		{
			if (!Item.Ops.StageDerived
				|| !Item.Ops.StageDerived(
					ContributorContext, Item.Stage, OutError))
			{
				if (OutError.IsEmpty())
				{
					OutError =
						TEXT("Derived-cache stage callback failed.");
				}
				OutError = FString::Printf(
					TEXT("%s: %s"),
					*Item.CanonicalKey,
					*OutError);
				return false;
			}
			if (!GatherStageRoots(Item))
			{
				return false;
			}
			continue;
		}

		const FSeinCanonicalStateContributorRecord* const* Found =
			RecordsByKey.Find(Item.CanonicalKey);
		if (!Found)
		{
			OutError = FString::Printf(
				TEXT("%s: persistent contributor record is missing."),
				*Item.CanonicalKey);
			return false;
		}
		FInstancedStruct Payload;
		if (!DecodeContributorPayload(
			Contributor, **Found, Payload, OutError)
			|| !Item.Ops.StageRestore
			|| !Item.Ops.StageRestore(
				ContributorContext, Payload, Item.Stage, OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Persistent contributor stage callback failed.");
			}
			OutError = FString::Printf(
				TEXT("%s: %s"),
				*Item.CanonicalKey,
				*OutError);
			return false;
		}
		if (!GatherStageRoots(Item))
		{
			return false;
		}
		StagedPayloads.Add(Item.CanonicalKey, MoveTemp(Payload));
	}

	Candidate->bReady = true;
	Candidate->StagedPayloads = MoveTemp(StagedPayloads);
	OutPlan.Data = MoveTemp(Candidate);
	return true;
}

int32 FSeinCanonicalStateRegistry::GetRegisteredContributorCount()
{
	FScopeLock Lock(&RegistryMutex());
	return Registry().Num();
}

FString FSeinCanonicalStateRegistry::CanonicalKey(
	const FSeinCanonicalStateKey& Key)
{
	if (Key.StableDomainId.GetNumber() != 0
		|| Key.StableContributorId.GetNumber() != 0)
	{
		return FString();
	}
	const FString Domain = CanonicalName(Key.StableDomainId);
	const FString Contributor = CanonicalName(Key.StableContributorId);
	if (!IsStableASCIIIdentifier(Domain)
		|| !IsStableASCIIIdentifier(Contributor))
	{
		return FString();
	}
	return Domain + TEXT("/") + Contributor;
}

bool FSeinCanonicalStateRegistry::BuildDescriptorIdentity(
	const FSeinCanonicalStateDescriptor& Descriptor,
	FString& OutCanonicalDescriptor,
	FGuid& OutDescriptorDigest,
	FString& OutError)
{
	OutCanonicalDescriptor.Reset();
	OutDescriptorDigest.Invalidate();
	OutError.Reset();
	return BuildCanonicalDescriptor(
		Descriptor,
		OutCanonicalDescriptor,
		OutDescriptorDigest,
		OutError);
}

bool FSeinCanonicalStateRegistry::BuildCombinedContractIdentity(
	const FSeinCanonicalStateSchemaSnapshot& NativeSnapshot,
	TConstArrayView<FString> AdditionalCanonicalDescriptors,
	FString& OutCanonicalManifest,
	FGuid& OutContractDigest,
	FString& OutError)
{
	OutCanonicalManifest.Reset();
	OutContractDigest.Invalidate();
	OutError.Reset();
	if (!NativeSnapshot.IsValid()
		|| !NativeSnapshot.GetContractDigest().IsValid())
	{
		OutError = TEXT("Native state schema snapshot is invalid.");
		return false;
	}

	TArray<FString> CanonicalAdditional(AdditionalCanonicalDescriptors);
	CanonicalAdditional.Sort();
	for (int32 Index = 0; Index < CanonicalAdditional.Num(); ++Index)
	{
		if (CanonicalAdditional[Index].IsEmpty()
			|| (Index > 0
				&& CanonicalAdditional[Index - 1]
					== CanonicalAdditional[Index]))
		{
			OutError =
				TEXT("Additional state descriptors must be non-empty and unique.");
			return false;
		}
	}

	OutCanonicalManifest =
		TEXT("SeinARTS.CanonicalState.CombinedContract\n");
	AppendFramed(
		OutCanonicalManifest,
		LexToString(ContractFormatVersion));
	AppendFramed(
		OutCanonicalManifest,
		NativeSnapshot.GetCanonicalManifest());
	AppendFramed(
		OutCanonicalManifest,
		LexToString(CanonicalAdditional.Num()));
	for (const FString& Descriptor : CanonicalAdditional)
	{
		AppendFramed(OutCanonicalManifest, Descriptor);
	}
	OutContractDigest = DigestUtf8(OutCanonicalManifest);
	if (!OutContractDigest.IsValid())
	{
		OutError = TEXT("Combined state contract digest is invalid.");
		return false;
	}
	return true;
}

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLatentActionCodecRegistry.cpp
 */

#include "Serialization/SeinLatentActionCodecRegistry.h"

#include "Abilities/SeinAbility.h"
#include "Abilities/SeinLatentAction.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Data/SeinWorldSnapshot.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "Serialization/SeinStateProviderTransaction.h"
#include "SeinARTSCoreEntityLog.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

namespace
{
	constexpr uint32 ManifestFormatVersion = 1;
	constexpr uint32 RecordFormatVersion = 1;
	constexpr uint32 SequenceFormatVersion = 1;

	class FInvocationScope
	{
	public:
		FInvocationScope() = default;
		~FInvocationScope() = default;

	private:
		FSeinStateProviderTransactionScope Scope;
	};

	struct FCodecClaim
	{
		uint64 Token = 0;
		FString Owner;
		FSeinLatentActionCodecDescriptor Descriptor;
		FGuid DescriptorDigest;
		FString CanonicalDescriptor;
		FSeinLatentActionCodecOps Ops;
		TStrongObjectPtr<UClass> ClassRoot;
		TStrongObjectPtr<UScriptStruct> PayloadRoot;
		TArray<TStrongObjectPtr<UScriptStruct>> DynamicRoots;
	};

	struct FCodecEntry
	{
		FString ClassPath;
		FString Owner;
		FString StableCodecId;
		TArray<FCodecClaim> Claims;
	};

	TArray<FCodecEntry>& Registry()
	{
		static TArray<FCodecEntry> Value;
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

	bool IsStableIdentifier(const FString& Value)
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

	void AppendFramed(FString& Out, const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		Out += FString::Printf(TEXT("%d:"), Utf8.Length());
		Out += Value;
		Out += TEXT("\n");
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

	FSeinStructWireLimits MakeWireLimits(
		const FSeinCanonicalStateLimits& StateLimits)
	{
		FSeinStructWireLimits Limits;
		Limits.MaxBytes = StateLimits.MaxEncodedBytes;
		Limits.MaxAggregateElements =
			StateLimits.MaxAggregateElements;
		Limits.MaxStringBytes = FMath::Min(
			StateLimits.MaxEncodedBytes, 1024 * 1024);
		Limits.MaxRecursionDepth =
			StateLimits.MaxRecursionDepth;
		Limits.MaxNativeAllocationBytes =
			StateLimits.MaxEncodedBytes;
		return Limits;
	}

	bool CanonicalizeDescriptor(
		FName OwnerModuleId,
		const FSeinLatentActionCodecDescriptor& Descriptor,
		FSeinLatentActionCodecDescriptor& OutDescriptor,
		FString& OutOwner,
		FGuid& OutDescriptorDigest,
		FString& OutCanonicalDescriptor,
		FString& OutError)
	{
		OutOwner = OwnerModuleId.ToString().ToLower();
		OutDescriptor = Descriptor;
		OutDescriptor.StableCodecId =
			Descriptor.StableCodecId.ToLower();

		if (!IsStableIdentifier(OutOwner)
			|| !IsStableIdentifier(OutDescriptor.StableCodecId)
			|| !OutDescriptor.SupportedClass
			|| !OutDescriptor.SupportedClass->IsChildOf(
				USeinLatentAction::StaticClass())
			|| OutDescriptor.SupportedClass->HasAnyClassFlags(
				CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
			|| !OutDescriptor.PayloadStruct
			|| OutDescriptor.StateSchemaVersion == 0
			|| OutDescriptor.BehaviorRevision == 0
			|| OutDescriptor.CodecRevision == 0
			|| OutDescriptor.Limits.MaxRecursionDepth <= 0
			|| OutDescriptor.Limits.MaxEncodedBytes <= 0
			|| OutDescriptor.Limits.MaxEncodedBytes
				> FSeinLatentActionCodecRegistry::MaxPayloadBytes
			|| OutDescriptor.Limits.MaxAggregateElements <= 0)
		{
			OutError =
				TEXT("Latent codecs require stable IDs, an exact concrete action class, a payload schema, positive revisions, and bounded limits.");
			return false;
		}

		FGuid ComputedSchema;
		if (!FSeinCanonicalStateCodec::ComputeSchemaDigest(
			OutDescriptor.PayloadStruct,
			ComputedSchema,
			OutError)
			|| !OutDescriptor.PayloadSchemaDigest.IsValid()
			|| OutDescriptor.PayloadSchemaDigest != ComputedSchema)
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Latent codec payload schema digest does not match its payload type.");
			}
			return false;
		}
		OutDescriptor.PayloadSchemaDigest = ComputedSchema;

		if (OutDescriptor.DynamicPayloadStructs.Contains(nullptr))
		{
			OutError =
				TEXT("Latent codec dynamic payload schemas must be non-null and unique.");
			return false;
		}
		OutDescriptor.DynamicPayloadStructs.Sort(
			[](const UScriptStruct& A, const UScriptStruct& B)
			{
				return A.GetPathName() < B.GetPathName();
			});
		for (int32 Index = 1;
			Index < OutDescriptor.DynamicPayloadStructs.Num();
			++Index)
		{
			if (OutDescriptor.DynamicPayloadStructs[Index - 1]
					->GetPathName()
				== OutDescriptor.DynamicPayloadStructs[Index]
					->GetPathName())
			{
				OutError =
					TEXT("Latent codec dynamic payload schemas must be unique.");
				return false;
			}
		}
		FString IgnoredNameManifest;
		SeinBuildCanonicalWireNameCatalog(
			Descriptor.AllowedNames,
			OutDescriptor.AllowedNames,
			IgnoredNameManifest);

		OutDescriptor.RequiredNativeContributors.Sort(
			[](const FSeinCanonicalStateKey& A,
				const FSeinCanonicalStateKey& B)
			{
				return FSeinCanonicalStateRegistry::CanonicalKey(A)
					< FSeinCanonicalStateRegistry::CanonicalKey(B);
			});
		FString PreviousDependency;
		for (const FSeinCanonicalStateKey& Dependency :
			OutDescriptor.RequiredNativeContributors)
		{
			const FString Canonical =
				FSeinCanonicalStateRegistry::CanonicalKey(Dependency);
			if (Canonical.IsEmpty() || Canonical == PreviousDependency)
			{
				OutError =
					TEXT("Latent codec native dependencies must be valid and unique.");
				return false;
			}
			PreviousDependency = Canonical;
		}

		FSeinCanonicalStateDescriptor PayloadDescriptor;
		PayloadDescriptor.Key.StableDomainId =
			TEXT("seinarts.latent.codec");
		PayloadDescriptor.Key.StableContributorId =
			FName(*OutDescriptor.StableCodecId);
		PayloadDescriptor.SchemaVersion =
			OutDescriptor.StateSchemaVersion;
		PayloadDescriptor.ImplementationRevision =
			OutDescriptor.CodecRevision;
		PayloadDescriptor.Role =
			ESeinCanonicalStateRole::Continuation;
		PayloadDescriptor.PayloadStruct =
			OutDescriptor.PayloadStruct;
		PayloadDescriptor.DynamicPayloadStructs =
			OutDescriptor.DynamicPayloadStructs;
		PayloadDescriptor.AllowedNames =
			OutDescriptor.AllowedNames;
		PayloadDescriptor.Limits = OutDescriptor.Limits;
		FString PayloadDescriptorManifest;
		FGuid PayloadDescriptorDigest;
		if (!FSeinCanonicalStateRegistry::BuildDescriptorIdentity(
			PayloadDescriptor,
			PayloadDescriptorManifest,
			PayloadDescriptorDigest,
			OutError))
		{
			return false;
		}

		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.LatentAction.CodecDescriptor"), 1);
		bool bWriteOK = Writer.WriteString(
				OutDescriptor.SupportedClass->GetPathName())
			&& Writer.WriteString(OutDescriptor.StableCodecId)
			&& Writer.WriteUInt32(OutDescriptor.StateSchemaVersion)
			&& Writer.WriteUInt32(OutDescriptor.BehaviorRevision)
			&& Writer.WriteUInt32(OutDescriptor.CodecRevision)
			&& Writer.WriteGuid(OutDescriptor.PayloadSchemaDigest)
			&& Writer.WriteGuid(PayloadDescriptorDigest)
			&& Writer.WriteInt32(
				OutDescriptor.RequiredNativeContributors.Num());

		OutCanonicalDescriptor =
			TEXT("SeinARTS.LatentAction.CodecDescriptor\n1\n");
		AppendFramed(
			OutCanonicalDescriptor,
			OutDescriptor.SupportedClass->GetPathName());
		AppendFramed(
			OutCanonicalDescriptor,
			OutDescriptor.StableCodecId);
		AppendFramed(
			OutCanonicalDescriptor,
			LexToString(OutDescriptor.StateSchemaVersion));
		AppendFramed(
			OutCanonicalDescriptor,
			LexToString(OutDescriptor.BehaviorRevision));
		AppendFramed(
			OutCanonicalDescriptor,
			LexToString(OutDescriptor.CodecRevision));
		AppendFramed(
			OutCanonicalDescriptor,
			OutDescriptor.PayloadSchemaDigest.ToString(
				EGuidFormats::Digits));
		AppendFramed(
			OutCanonicalDescriptor,
			PayloadDescriptorDigest.ToString(EGuidFormats::Digits));
		AppendFramed(
			OutCanonicalDescriptor,
			LexToString(
				OutDescriptor.RequiredNativeContributors.Num()));
		for (const FSeinCanonicalStateKey& Dependency :
			OutDescriptor.RequiredNativeContributors)
		{
			const FString Canonical =
				FSeinCanonicalStateRegistry::CanonicalKey(Dependency);
			bWriteOK = bWriteOK && Writer.WriteString(Canonical);
			AppendFramed(OutCanonicalDescriptor, Canonical);
		}
		return bWriteOK
			&& Writer.Finalize(OutDescriptorDigest, OutError);
	}

	bool ValidateOps(
		const FSeinLatentActionCodecOps& Ops,
		FString& OutError)
	{
		if (!Ops.Capture || !Ops.StageRestore || !Ops.CommitRestore)
		{
			OutError =
				TEXT("Latent codec requires capture, data-only stage, and commit callbacks.");
			return false;
		}
		return true;
	}
}

struct FSeinLatentActionCodecManifest::FData
{
	struct FEntry
	{
		uint64 Token = 0;
		FString Owner;
		FSeinLatentActionCodecDescriptor Descriptor;
		FGuid DescriptorDigest;
		FString CanonicalDescriptor;
		TStrongObjectPtr<UClass> ClassRoot;
		TStrongObjectPtr<UScriptStruct> PayloadRoot;
		TArray<TStrongObjectPtr<UScriptStruct>> DynamicRoots;
	};

	TArray<FEntry> Entries;
	FString CanonicalManifest;
	FGuid Digest;
};

struct FSeinLatentActionRestorePlan::FData
{
	struct FItem
	{
		uint64 Token = 0;
		FSeinLatentActionCodecOps Ops;
		TUniquePtr<ISeinLatentActionRestoreStage> Stage;
		FSeinSnapshotLatentActionRecord Record;
		const UClass* ExactActionClass = nullptr;
	};

	TArray<FItem> Items;
	int64 NextActionID = 1;
	int64 NextAbilityActivationID = 1;
	bool bReady = false;
};

namespace
{
	bool ResolveClaim(
		uint64 Token,
		FCodecClaim& OutClaim,
		FString& OutError)
	{
		OutClaim = {};
		for (const FCodecEntry& Entry : Registry())
		{
			const FCodecClaim* Claim =
				Entry.Claims.FindByPredicate(
					[Token](const FCodecClaim& Candidate)
					{
						return Candidate.Token == Token;
					});
			if (Claim)
			{
				OutClaim.Token = Claim->Token;
				OutClaim.Owner = Claim->Owner;
				OutClaim.Descriptor = Claim->Descriptor;
				OutClaim.DescriptorDigest = Claim->DescriptorDigest;
				OutClaim.CanonicalDescriptor =
					Claim->CanonicalDescriptor;
				OutClaim.Ops = Claim->Ops;
				return true;
			}
		}
		OutError =
			TEXT("The frozen latent-action codec generation is unavailable.");
		return false;
	}

	bool EncodePayload(
		const FSeinLatentActionCodecDescriptor& Descriptor,
		const FInstancedStruct& Payload,
		TArray<uint8>& OutBytes,
		FString& OutError)
	{
		OutBytes.Reset();
		if (!Payload.IsValid()
			|| Payload.GetScriptStruct() != Descriptor.PayloadStruct)
		{
			OutError =
				TEXT("Latent codec returned the wrong payload type.");
			return false;
		}
		FSeinWireCost Cost;
		return FSeinCanonicalStateCodec::EncodeWithCost(
			Payload.GetScriptStruct(),
			Payload.GetMemory(),
			{ Descriptor.DynamicPayloadStructs,
				Descriptor.AllowedNames },
			MakeWireLimits(Descriptor.Limits),
			OutBytes,
			OutError,
			Cost);
	}

	bool DecodePayload(
		const FSeinLatentActionCodecDescriptor& Descriptor,
		TConstArrayView<uint8> Bytes,
		FInstancedStruct& OutPayload,
		FString& OutError)
	{
		OutPayload.Reset();
		if (Bytes.Num() > Descriptor.Limits.MaxEncodedBytes)
		{
			OutError =
				TEXT("Latent payload exceeds its frozen codec bound.");
			return false;
		}
		OutPayload.InitializeAs(Descriptor.PayloadStruct);
		FSeinWireCost Cost;
		if (!FSeinCanonicalStateCodec::DecodeWithCost(
			Bytes,
			Descriptor.PayloadStruct,
			OutPayload.GetMutableMemory(),
			{ Descriptor.DynamicPayloadStructs,
				Descriptor.AllowedNames },
			MakeWireLimits(Descriptor.Limits),
			OutError,
			Cost))
		{
			OutPayload.Reset();
			return false;
		}
		TArray<uint8> Canonical;
		const bool bEncoded = EncodePayload(
			Descriptor, OutPayload, Canonical, OutError);
		const bool bExact = bEncoded
			&& Canonical.Num() == Bytes.Num()
			&& (Canonical.IsEmpty()
				|| FMemory::Memcmp(
					Canonical.GetData(),
					Bytes.GetData(),
					Bytes.Num()) == 0);
		if (!bExact)
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Latent payload is not in canonical byte form.");
			}
			OutPayload.Reset();
			return false;
		}
		return true;
	}

	bool ComputePayloadDigest(
		const FSeinSnapshotLatentActionRecord& Record,
		FGuid& OutDigest,
		FString& OutError)
	{
		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.LatentAction.Payload"),
			RecordFormatVersion);
		return Writer.WriteUInt32(Record.StateSchemaVersion)
			&& Writer.WriteGuid(Record.PayloadSchemaDigest)
			&& Writer.WriteBytes(Record.PayloadBytes)
			&& Writer.Finalize(OutDigest, OutError);
	}

	bool ComputeRecordDigest(
		const FSeinSnapshotLatentActionRecord& Record,
		FGuid& OutDigest,
		FString& OutError)
	{
		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.LatentAction.Record"),
			RecordFormatVersion);
		return Writer.WriteInt32(Record.Ordinal)
			&& Writer.WriteInt64(Record.ActionID)
			&& Writer.WriteInt32(Record.AbilityPoolID)
			&& Writer.WriteInt64(Record.AbilityActivationID)
			&& Writer.WriteInt32(Record.OwnerEntity.Index)
			&& Writer.WriteInt32(Record.OwnerEntity.Generation)
			&& Writer.WriteString(Record.ActionClassPath)
			&& Writer.WriteString(Record.StableCodecID)
			&& Writer.WriteUInt32(Record.StateSchemaVersion)
			&& Writer.WriteUInt32(Record.BehaviorRevision)
			&& Writer.WriteUInt32(Record.CodecRevision)
			&& Writer.WriteGuid(Record.CodecDescriptorDigest)
			&& Writer.WriteGuid(Record.PayloadSchemaDigest)
			&& Writer.WriteGuid(Record.PayloadDigest)
			&& Writer.Finalize(OutDigest, OutError);
	}

	bool ComputeSequenceDigest(
		int64 NextActionID,
		int64 NextAbilityActivationID,
		TConstArrayView<FSeinSnapshotLatentActionRecord> Records,
		FGuid& OutDigest,
		FString& OutError)
	{
		OutDigest.Invalidate();
		if (NextActionID <= 0 || NextAbilityActivationID <= 0)
		{
			OutError =
				TEXT("Latent allocator cursors must be positive.");
			return false;
		}
		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.LatentAction.Sequence"),
			SequenceFormatVersion);
		if (!Writer.WriteInt64(NextActionID)
			|| !Writer.WriteInt64(NextAbilityActivationID)
			|| !Writer.WriteInt32(Records.Num()))
		{
			OutError = Writer.GetError();
			return false;
		}
		for (const FSeinSnapshotLatentActionRecord& Record : Records)
		{
			if (!Writer.WriteGuid(Record.RecordDigest))
			{
				OutError = Writer.GetError();
				return false;
			}
		}
		return Writer.Finalize(OutDigest, OutError);
	}

	class FDependencyPayloadView final
		: public ISeinCanonicalStateStagedPayloadView
	{
	public:
		explicit FDependencyPayloadView(
			const TMap<FString, const FInstancedStruct*>& InPayloads)
			: Payloads(InPayloads)
		{
		}

		virtual const FInstancedStruct* FindStagedPayload(
			const FSeinCanonicalStateKey& Key) const override
		{
			const FString Canonical =
				FSeinCanonicalStateRegistry::CanonicalKey(Key);
			const FInstancedStruct* const* Found =
				Payloads.Find(Canonical);
			return Found ? *Found : nullptr;
		}

	private:
		const TMap<FString, const FInstancedStruct*>& Payloads;
	};
}

FSeinLatentActionCodecRegistrationHandle::
	~FSeinLatentActionCodecRegistrationHandle()
{
	Reset();
}

FSeinLatentActionCodecRegistrationHandle::
	FSeinLatentActionCodecRegistrationHandle(
		FSeinLatentActionCodecRegistrationHandle&& Other) noexcept
	: Token(Other.Token)
{
	check(IsInGameThread());
	Other.Token = 0;
}

FSeinLatentActionCodecRegistrationHandle&
FSeinLatentActionCodecRegistrationHandle::operator=(
	FSeinLatentActionCodecRegistrationHandle&& Other) noexcept
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

void FSeinLatentActionCodecRegistrationHandle::Reset()
{
	if (Token != 0)
	{
		check(IsInGameThread());
		if (FSeinStateProviderTransactionScope::IsActive())
		{
			UE_LOG(LogSeinSim, Fatal,
				TEXT("A latent codec registration handle was destroyed from inside a state-provider callback transaction."));
		}
		const bool bRemoved =
			FSeinLatentActionCodecRegistry::UnregisterToken(Token);
		check(bRemoved);
		Token = 0;
	}
}

int32 FSeinLatentActionCodecManifest::Num() const
{
	return Data.IsValid() ? Data->Entries.Num() : 0;
}

const FString&
FSeinLatentActionCodecManifest::GetCanonicalManifest() const
{
	static const FString Empty;
	return Data.IsValid() ? Data->CanonicalManifest : Empty;
}

FGuid FSeinLatentActionCodecManifest::GetDigest() const
{
	return Data.IsValid() ? Data->Digest : FGuid();
}

int32 FSeinLatentActionCodecRegistry::FindManifestEntryIndex(
	const FSeinLatentActionCodecManifest& Manifest,
	const UClass* ExactClass)
{
	if (!Manifest.Data.IsValid() || !ExactClass)
	{
		return INDEX_NONE;
	}
	return Manifest.Data->Entries.IndexOfByPredicate(
		[ExactClass](
			const FSeinLatentActionCodecManifest::FData::FEntry& Entry)
		{
			return Entry.Descriptor.SupportedClass == ExactClass;
		});
}

int32 FSeinLatentActionCodecRegistry::FindManifestEntryIndexByPath(
	const FSeinLatentActionCodecManifest& Manifest,
	const FString& ExactClassPath)
{
	if (!Manifest.Data.IsValid() || ExactClassPath.IsEmpty())
	{
		return INDEX_NONE;
	}
	return Manifest.Data->Entries.IndexOfByPredicate(
		[&ExactClassPath](
			const FSeinLatentActionCodecManifest::FData::FEntry& Entry)
		{
			return Entry.Descriptor.SupportedClass
				&& Entry.Descriptor.SupportedClass->GetPathName()
					== ExactClassPath;
		});
}

FSeinLatentActionRestorePlan::FSeinLatentActionRestorePlan()
{
	check(IsInGameThread());
}

FSeinLatentActionRestorePlan::~FSeinLatentActionRestorePlan()
{
	check(IsInGameThread());
	Reset();
}

FSeinLatentActionRestorePlan::FSeinLatentActionRestorePlan(
	FSeinLatentActionRestorePlan&& Other) noexcept
	: Data(MoveTemp(Other.Data))
{
	check(IsInGameThread());
}

FSeinLatentActionRestorePlan&
FSeinLatentActionRestorePlan::operator=(
	FSeinLatentActionRestorePlan&& Other) noexcept
{
	check(IsInGameThread());
	if (this != &Other)
	{
		Reset();
		Data = MoveTemp(Other.Data);
	}
	return *this;
}

bool FSeinLatentActionRestorePlan::IsReady() const
{
	return Data && Data->bReady;
}

void FSeinLatentActionRestorePlan::Reset()
{
	check(IsInGameThread());
	if (!Data)
	{
		return;
	}
	if (FSeinStateProviderTransactionScope::IsActive())
	{
		Data.Reset();
		return;
	}

	// Restore stages and copied codec operations may execute module-owned
	// destructors. Keep cross-registry mutation blocked during every unwind.
	FInvocationScope Scope;
	Data.Reset();
}

bool FSeinLatentActionRestorePlan::VerifyProviderLeases(
	FString& OutError) const
{
	check(IsInGameThread());
	OutError.Reset();
	if (!IsReady())
	{
		OutError = TEXT("Latent restore plan is not ready.");
		return false;
	}

	// Resolve every exact generation before invoking any nested lease callback.
	// Claims also retain module-owned callables, so their destruction remains
	// inside the same shared transaction.
	FInvocationScope Scope;
	TArray<FCodecClaim> Claims;
	Claims.Reserve(Data->Items.Num());
	for (const FData::FItem& Item : Data->Items)
	{
		FCodecClaim Claim;
		if (!ResolveClaim(Item.Token, Claim, OutError))
		{
			return false;
		}
		Claims.Add(MoveTemp(Claim));
	}
	for (const FData::FItem& Item : Data->Items)
	{
		FString ExternalError;
		if (Item.Stage
			&& !Item.Stage->VerifyExternalLeases(ExternalError))
		{
			OutError = ExternalError.IsEmpty()
				? TEXT("A latent codec nested generation became unavailable.")
				: MoveTemp(ExternalError);
			return false;
		}
	}
	return true;
}

void FSeinLatentActionRestorePlan::Commit(
	USeinWorldSubsystem& World,
	USeinLatentActionManager& Manager,
	int32 Tick)
{
	check(IsInGameThread());
	check(IsReady());
	FString LeaseError;
	const bool bLeasesValid = VerifyProviderLeases(LeaseError);
	checkf(bLeasesValid,
		TEXT("Latent codec generation disappeared after final lease verification: %s"),
		*LeaseError);
	if (!bLeasesValid)
	{
		UE_LOG(LogSeinSim, Fatal,
			TEXT("Latent codec generation disappeared after final lease verification: %s"),
			*LeaseError);
		return;
	}

	TArray<TObjectPtr<USeinLatentAction>> Restored;
	Restored.Reserve(Data->Items.Num());
	for (FData::FItem& Item : Data->Items)
	{
		USeinAbility* Ability =
			World.GetAbilityInstance(Item.Record.AbilityPoolID);
		check(Ability
			&& Ability->bIsActive
			&& Ability->GetActivationID()
				== Item.Record.AbilityActivationID);
		FSeinLatentActionCommitContext Context{
			World,
			*Ability,
			Tick,
			Item.Record.ActionID,
			Item.Record.AbilityActivationID
		};
		USeinLatentAction* Action = nullptr;
		{
			FInvocationScope Scope;
			Action = Item.Ops.CommitRestore(
				Context, MoveTemp(Item.Stage));
		}
		check(Action
			&& Action->GetClass() == Item.ExactActionClass
			&& Action->ActionID == 0
			&& !Action->bCompleted
			&& !Action->bCancelled
			&& !Action->bFailed);
		Action->OwningAbility = Ability;
		Action->OwnerEntity = Item.Record.OwnerEntity;
		Action->ActionID = Item.Record.ActionID;
		Action->AbilityActivationID =
			Item.Record.AbilityActivationID;
		Action->FailureReason = 0;
		Restored.Add(Action);
	}
	Manager.AdoptRestoredActions(
		MoveTemp(Restored), Data->NextActionID);
	Reset();
}

FSeinLatentActionCodecRegistrationHandle
FSeinLatentActionCodecRegistry::Register(
	FName OwnerModuleId,
	const FSeinLatentActionCodecDescriptor& Descriptor,
	FSeinLatentActionCodecOps Ops,
	FString* OutError)
{
	check(IsInGameThread());
	SetError(OutError, FString());
	if (FSeinStateProviderTransactionScope::IsActive())
	{
		SetError(
			OutError,
			TEXT("Latent codecs may not register during a codec callback."));
		return {};
	}

	FSeinLatentActionCodecDescriptor CanonicalDescriptor;
	FString Owner;
	FGuid DescriptorDigest;
	FString CanonicalDescriptorText;
	FString Error;
	if (!CanonicalizeDescriptor(
		OwnerModuleId,
		Descriptor,
		CanonicalDescriptor,
		Owner,
		DescriptorDigest,
		CanonicalDescriptorText,
		Error)
		|| !ValidateOps(Ops, Error))
	{
		SetError(OutError, Error);
		UE_LOG(LogSeinSim, Error,
			TEXT("Rejected latent-action codec: %s"), *Error);
		return {};
	}

	const FString ClassPath =
		CanonicalDescriptor.SupportedClass->GetPathName();
	for (const FCodecEntry& Existing : Registry())
	{
		if (Existing.ClassPath != ClassPath
			&& Existing.StableCodecId
				== CanonicalDescriptor.StableCodecId)
		{
			Error = FString::Printf(
				TEXT("Latent codec ID '%s' is already bound to another exact action class."),
				*CanonicalDescriptor.StableCodecId);
			SetError(OutError, Error);
			return {};
		}
	}

	FCodecEntry* Entry = Registry().FindByPredicate(
		[&ClassPath](const FCodecEntry& Candidate)
		{
			return Candidate.ClassPath == ClassPath;
		});
	if (Entry)
	{
		if (Entry->Owner != Owner
			|| Entry->StableCodecId
				!= CanonicalDescriptor.StableCodecId
			|| Entry->Claims.IsEmpty()
			|| Entry->Claims[0].DescriptorDigest
				!= DescriptorDigest
			|| Entry->Claims.Num() >= MaxReloadClaimsPerClass)
		{
			Error = FString::Printf(
				TEXT("Conflicting or saturated latent codec claim for '%s'."),
				*ClassPath);
			SetError(OutError, Error);
			return {};
		}
	}
	else
	{
		Entry = &Registry().AddDefaulted_GetRef();
		Entry->ClassPath = ClassPath;
		Entry->Owner = Owner;
		Entry->StableCodecId =
			CanonicalDescriptor.StableCodecId;
	}

	FCodecClaim Claim;
	Claim.Token = AllocateToken();
	Claim.Owner = Owner;
	Claim.Descriptor = MoveTemp(CanonicalDescriptor);
	Claim.DescriptorDigest = DescriptorDigest;
	Claim.CanonicalDescriptor = MoveTemp(CanonicalDescriptorText);
	Claim.Ops = MoveTemp(Ops);
	Claim.ClassRoot.Reset(
		const_cast<UClass*>(Claim.Descriptor.SupportedClass));
	Claim.PayloadRoot.Reset(
		const_cast<UScriptStruct*>(
			Claim.Descriptor.PayloadStruct));
	for (const UScriptStruct* Dynamic :
		Claim.Descriptor.DynamicPayloadStructs)
	{
		Claim.DynamicRoots.Emplace(
			const_cast<UScriptStruct*>(Dynamic));
	}
	const uint64 Token = Claim.Token;
	Entry->Claims.Add(MoveTemp(Claim));
	return FSeinLatentActionCodecRegistrationHandle(Token);
}

bool FSeinLatentActionCodecRegistry::Unregister(
	FSeinLatentActionCodecRegistrationHandle& Handle)
{
	check(IsInGameThread());
	if (!Handle.IsValid()
		|| FSeinStateProviderTransactionScope::IsActive()
		|| !UnregisterToken(Handle.Token))
	{
		return false;
	}
	Handle.Token = 0;
	return true;
}

bool FSeinLatentActionCodecRegistry::UnregisterToken(uint64 Token)
{
	check(IsInGameThread());
	check(!FSeinStateProviderTransactionScope::IsActive());
	if (Token == 0)
	{
		return false;
	}
	for (int32 EntryIndex = 0;
		EntryIndex < Registry().Num();
		++EntryIndex)
	{
		FCodecEntry& Entry = Registry()[EntryIndex];
		const int32 ClaimIndex =
			Entry.Claims.IndexOfByPredicate(
				[Token](const FCodecClaim& Claim)
				{
					return Claim.Token == Token;
				});
		if (ClaimIndex == INDEX_NONE)
		{
			continue;
		}

		const FString Owner = Entry.Claims[ClaimIndex].Owner;
		for (TObjectIterator<USeinWorldSubsystem> It; It; ++It)
		{
			if (It->HasAnyFlags(RF_ClassDefaultObject)
				|| !It->LatentActionCodecManifest.Data.IsValid())
			{
				continue;
			}
			const bool bUsesToken =
				It->LatentActionCodecManifest.Data->Entries
					.ContainsByPredicate(
						[Token](
							const FSeinLatentActionCodecManifest::
								FData::FEntry& Frozen)
						{
							return Frozen.Token == Token;
						});
			if (bUsesToken)
			{
				It->TerminateAndReleaseForModuleUnload(
					FName(*Owner),
					TEXT("its frozen latent-action codec generation unloaded"));
			}
		}

		FCodecClaim RemovedClaim =
			MoveTemp(Entry.Claims[ClaimIndex]);
		Entry.Claims.RemoveAt(ClaimIndex);
		if (Entry.Claims.IsEmpty())
		{
			Registry().RemoveAt(EntryIndex);
		}
		{
			// Destroy executable captures only after registry structure is stable.
			FInvocationScope Scope;
			RemovedClaim = {};
		}
		return true;
	}
	return false;
}

FSeinLatentActionCodecManifest
FSeinLatentActionCodecRegistry::CaptureManifest(
	const FSeinCanonicalStateSchemaSnapshot& NativeSchema,
	FString* OutError)
{
	check(IsInGameThread());
	SetError(OutError, FString());
	if (FSeinStateProviderTransactionScope::IsActive()
		|| !NativeSchema.IsValid())
	{
		SetError(
			OutError,
			TEXT("Latent codec manifest requires a valid native schema outside codec callbacks."));
		return {};
	}

	TSet<FString> NativeKeys;
	for (const FSeinFrozenCanonicalStateContributor& Contributor :
		NativeSchema.GetContributors())
	{
		if (Contributor.Descriptor.Role
			!= ESeinCanonicalStateRole::DerivedCache)
		{
			NativeKeys.Add(
				FSeinCanonicalStateRegistry::CanonicalKey(
					Contributor.Descriptor.Key));
		}
	}

	TSharedRef<FSeinLatentActionCodecManifest::FData,
		ESPMode::ThreadSafe> Data =
		MakeShared<
			FSeinLatentActionCodecManifest::FData,
			ESPMode::ThreadSafe>();
	Data->Entries.Reserve(Registry().Num());
	for (const FCodecEntry& Registered : Registry())
	{
		if (Registered.Claims.IsEmpty())
		{
			continue;
		}
		const FCodecClaim& Claim = Registered.Claims.Last();
		for (const FSeinCanonicalStateKey& Dependency :
			Claim.Descriptor.RequiredNativeContributors)
		{
			const FString Canonical =
				FSeinCanonicalStateRegistry::CanonicalKey(Dependency);
			if (!NativeKeys.Contains(Canonical))
			{
				SetError(
					OutError,
					FString::Printf(
						TEXT("Latent codec '%s' requires missing or non-persistent native contributor '%s'."),
						*Claim.Descriptor.StableCodecId,
						*Canonical));
				return {};
			}
		}

		FSeinLatentActionCodecManifest::FData::FEntry& Frozen =
			Data->Entries.AddDefaulted_GetRef();
		Frozen.Token = Claim.Token;
		Frozen.Owner = Claim.Owner;
		Frozen.Descriptor = Claim.Descriptor;
		Frozen.DescriptorDigest = Claim.DescriptorDigest;
		Frozen.CanonicalDescriptor = Claim.CanonicalDescriptor;
		Frozen.ClassRoot.Reset(
			const_cast<UClass*>(
				Claim.Descriptor.SupportedClass));
		Frozen.PayloadRoot.Reset(
			const_cast<UScriptStruct*>(
				Claim.Descriptor.PayloadStruct));
		for (const UScriptStruct* Dynamic :
			Claim.Descriptor.DynamicPayloadStructs)
		{
			Frozen.DynamicRoots.Emplace(
				const_cast<UScriptStruct*>(Dynamic));
		}
	}
	Data->Entries.Sort(
		[](const FSeinLatentActionCodecManifest::FData::FEntry& A,
			const FSeinLatentActionCodecManifest::FData::FEntry& B)
		{
			if (A.Descriptor.StableCodecId
				!= B.Descriptor.StableCodecId)
			{
				return A.Descriptor.StableCodecId
					< B.Descriptor.StableCodecId;
			}
			return A.Descriptor.SupportedClass->GetPathName()
				< B.Descriptor.SupportedClass->GetPathName();
		});

	FSeinCanonicalDigestWriter Writer(
		TEXT("SeinARTS.LatentAction.CodecManifest"),
		ManifestFormatVersion);
	Data->CanonicalManifest =
		TEXT("SeinARTS.LatentAction.CodecManifest\n1\n");
	bool bWriteOK = Writer.WriteInt32(Data->Entries.Num());
	AppendFramed(
		Data->CanonicalManifest,
		LexToString(Data->Entries.Num()));
	for (const FSeinLatentActionCodecManifest::FData::FEntry& Entry :
		Data->Entries)
	{
		bWriteOK = bWriteOK
			&& Writer.WriteGuid(Entry.DescriptorDigest);
		AppendFramed(
			Data->CanonicalManifest,
			Entry.CanonicalDescriptor);
	}
	FString Error;
	if (!bWriteOK
		|| !Writer.Finalize(Data->Digest, Error)
		|| !Data->Digest.IsValid())
	{
		SetError(
			OutError,
			Error.IsEmpty()
				? TEXT("Latent codec manifest digest failed.")
				: MoveTemp(Error));
		return {};
	}

	FSeinLatentActionCodecManifest Result;
	Result.Data = Data;
	return Result;
}

bool FSeinLatentActionCodecRegistry::CaptureRecords(
	const FSeinLatentActionCodecManifest& Manifest,
	const USeinWorldSubsystem& World,
	const USeinLatentActionManager* Manager,
	int32 Tick,
	int64 NextActionID,
	int64 NextAbilityActivationID,
	TArray<FSeinSnapshotLatentActionRecord>& OutRecords,
	FGuid& OutSequenceDigest,
	FString& OutError)
{
	check(IsInGameThread());
	OutRecords.Reset();
	OutSequenceDigest.Invalidate();
	OutError.Reset();
	if (!Manifest.IsValid() || !Manifest.GetDigest().IsValid()
		|| FSeinStateProviderTransactionScope::IsActive()
		|| NextActionID <= 0
		|| NextAbilityActivationID <= 0)
	{
		OutError =
			TEXT("Latent capture requires a valid frozen manifest and allocator cursors.");
		return false;
	}

	const TConstArrayView<TObjectPtr<USeinLatentAction>> Actions =
		Manager
			? Manager->GetActiveActions()
			: TConstArrayView<TObjectPtr<USeinLatentAction>>();
	if (Actions.Num() > MaxActiveActions)
	{
		OutError =
			TEXT("Active latent action count exceeds the checkpoint bound.");
		return false;
	}

	// Claims copied below own module callables. Keep both invocation and
	// destruction within one shared cross-registry transaction.
	FInvocationScope ProviderTransaction;
	int64 PreviousActionID = 0;
	int64 AggregateBytes = 0;
	OutRecords.Reserve(Actions.Num());
	for (int32 Ordinal = 0; Ordinal < Actions.Num(); ++Ordinal)
	{
		const USeinLatentAction* Action = Actions[Ordinal].Get();
		if (!Action || Action->bCompleted || Action->bCancelled
			|| Action->bFailed
			|| Action->GetActionID() <= PreviousActionID
			|| Action->GetActionID() >= NextActionID)
		{
			OutError =
				TEXT("Latent manager contains null, terminal, duplicate, reordered, or invalid identity state.");
			OutRecords.Reset();
			return false;
		}
		PreviousActionID = Action->GetActionID();

		const USeinAbility* Ability = Action->OwningAbility.Get();
		const int32 AbilityPoolID =
			World.FindAbilityInstanceID(Ability);
		if (!Ability
			|| AbilityPoolID == INDEX_NONE
			|| !Ability->bIsActive
			|| Ability->OwnerEntity != Action->OwnerEntity
			|| !World.IsEntityAlive(Action->OwnerEntity)
			|| Action->GetAbilityActivationID() <= 0
			|| Action->GetAbilityActivationID()
				!= Ability->GetActivationID()
			|| Action->GetAbilityActivationID()
				>= NextAbilityActivationID)
		{
			OutError =
				TEXT("Latent action does not belong to one exact live ability activation.");
			OutRecords.Reset();
			return false;
		}

		const int32 EntryIndex = FindManifestEntryIndex(
			Manifest, Action->GetClass());
		if (EntryIndex == INDEX_NONE)
		{
			OutError = FString::Printf(
				TEXT("Active latent continuation '%s' has no exact checkpoint codec."),
				*Action->GetClass()->GetPathName());
			OutRecords.Reset();
			return false;
		}
		const FSeinLatentActionCodecManifest::FData::FEntry& Entry =
			Manifest.Data->Entries[EntryIndex];
		FCodecClaim Claim;
		if (!ResolveClaim(Entry.Token, Claim, OutError)
			|| Claim.DescriptorDigest != Entry.DescriptorDigest
			|| Claim.Descriptor.SupportedClass != Action->GetClass())
		{
			OutRecords.Reset();
			return false;
		}

		FInstancedStruct Payload;
		if (!Claim.Ops.Capture(
			{ World,
				*Action,
				Tick,
				AbilityPoolID,
				Action->GetAbilityActivationID() },
			Payload,
			OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Latent codec capture failed without a diagnostic.");
			}
			OutRecords.Reset();
			return false;
		}

		FSeinSnapshotLatentActionRecord& Record =
			OutRecords.AddDefaulted_GetRef();
		Record.Ordinal = Ordinal;
		Record.ActionID = Action->GetActionID();
		Record.AbilityPoolID = AbilityPoolID;
		Record.AbilityActivationID =
			Action->GetAbilityActivationID();
		Record.OwnerEntity = Action->OwnerEntity;
		Record.ActionClassPath =
			Action->GetClass()->GetPathName();
		Record.StableCodecID =
			Claim.Descriptor.StableCodecId;
		Record.StateSchemaVersion =
			Claim.Descriptor.StateSchemaVersion;
		Record.BehaviorRevision =
			Claim.Descriptor.BehaviorRevision;
		Record.CodecRevision =
			Claim.Descriptor.CodecRevision;
		Record.CodecDescriptorDigest =
			Claim.DescriptorDigest;
		Record.PayloadSchemaDigest =
			Claim.Descriptor.PayloadSchemaDigest;
		if (!EncodePayload(
			Claim.Descriptor,
			Payload,
			Record.PayloadBytes,
			OutError))
		{
			OutRecords.Reset();
			return false;
		}
		AggregateBytes += Record.PayloadBytes.Num();
		if (AggregateBytes > MaxAggregatePayloadBytes
			|| !ComputePayloadDigest(
				Record, Record.PayloadDigest, OutError)
			|| !ComputeRecordDigest(
				Record, Record.RecordDigest, OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Latent continuation payload aggregate exceeds its bound.");
			}
			OutRecords.Reset();
			return false;
		}
	}

	return ComputeSequenceDigest(
		NextActionID,
		NextAbilityActivationID,
		OutRecords,
		OutSequenceDigest,
		OutError);
}

bool FSeinLatentActionCodecRegistry::CaptureRecordForVerifiedRoot(
	const FSeinLatentActionCodecManifest& Manifest,
	const USeinWorldSubsystem& World,
	const USeinLatentAction& Action,
	int32 Tick,
	int32 Ordinal,
	int64 NextActionID,
	int64 NextAbilityActivationID,
	FSeinSnapshotLatentActionRecord& OutRecord,
	FString& OutError)
{
	check(IsInGameThread());
	OutRecord = FSeinSnapshotLatentActionRecord();
	OutError.Reset();
	if (!Manifest.IsValid() || !Manifest.GetDigest().IsValid()
		|| FSeinStateProviderTransactionScope::IsActive()
		|| Ordinal < 0
		|| NextActionID <= 0
		|| NextAbilityActivationID <= 0
		|| Action.bCompleted
		|| Action.bCancelled
		|| Action.bFailed
		|| Action.GetActionID() <= 0
		|| Action.GetActionID() >= NextActionID)
	{
		OutError =
			TEXT("Incremental latent capture requires one live action and valid frozen allocator context.");
		return false;
	}

	const USeinAbility* Ability = Action.OwningAbility.Get();
	const int32 AbilityPoolID = World.FindAbilityInstanceID(Ability);
	if (!Ability
		|| AbilityPoolID == INDEX_NONE
		|| !Ability->bIsActive
		|| Ability->OwnerEntity != Action.OwnerEntity
		|| !World.IsEntityAlive(Action.OwnerEntity)
		|| Action.GetAbilityActivationID() <= 0
		|| Action.GetAbilityActivationID() != Ability->GetActivationID()
		|| Action.GetAbilityActivationID() >= NextAbilityActivationID)
	{
		OutError =
			TEXT("Incremental latent action does not belong to one exact live ability activation.");
		return false;
	}

	const int32 EntryIndex = FindManifestEntryIndex(
		Manifest, Action.GetClass());
	if (EntryIndex == INDEX_NONE)
	{
		OutError = FString::Printf(
			TEXT("Active latent continuation '%s' has no exact checkpoint codec."),
			*Action.GetClass()->GetPathName());
		return false;
	}
	const FSeinLatentActionCodecManifest::FData::FEntry& Entry =
		Manifest.Data->Entries[EntryIndex];

	// The copied claim owns module callables. Bound both invocation and
	// destruction to this synchronous game-thread transaction.
	FInvocationScope ProviderTransaction;
	FCodecClaim Claim;
	if (!ResolveClaim(Entry.Token, Claim, OutError)
		|| Claim.DescriptorDigest != Entry.DescriptorDigest
		|| Claim.Descriptor.SupportedClass != Action.GetClass())
	{
		return false;
	}

	FInstancedStruct Payload;
	if (!Claim.Ops.Capture(
		{ World,
			Action,
			Tick,
			AbilityPoolID,
			Action.GetAbilityActivationID() },
		Payload,
		OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("Latent codec capture failed without a diagnostic.");
		}
		return false;
	}

	OutRecord.Ordinal = Ordinal;
	OutRecord.ActionID = Action.GetActionID();
	OutRecord.AbilityPoolID = AbilityPoolID;
	OutRecord.AbilityActivationID = Action.GetAbilityActivationID();
	OutRecord.OwnerEntity = Action.OwnerEntity;
	OutRecord.ActionClassPath = Action.GetClass()->GetPathName();
	OutRecord.StableCodecID = Claim.Descriptor.StableCodecId;
	OutRecord.StateSchemaVersion = Claim.Descriptor.StateSchemaVersion;
	OutRecord.BehaviorRevision = Claim.Descriptor.BehaviorRevision;
	OutRecord.CodecRevision = Claim.Descriptor.CodecRevision;
	OutRecord.CodecDescriptorDigest = Claim.DescriptorDigest;
	OutRecord.PayloadSchemaDigest = Claim.Descriptor.PayloadSchemaDigest;
	return EncodePayload(
			Claim.Descriptor,
			Payload,
			OutRecord.PayloadBytes,
			OutError)
		&& ComputePayloadDigest(
			OutRecord, OutRecord.PayloadDigest, OutError)
		&& ComputeRecordDigest(
			OutRecord, OutRecord.RecordDigest, OutError);
}

bool FSeinLatentActionCodecRegistry::ComputeSequenceDigestForVerifiedRoot(
	int64 NextActionID,
	int64 NextAbilityActivationID,
	TConstArrayView<FSeinSnapshotLatentActionRecord> Records,
	FGuid& OutSequenceDigest,
	FString& OutError)
{
	return ComputeSequenceDigest(
		NextActionID,
		NextAbilityActivationID,
		Records,
		OutSequenceDigest,
		OutError);
}

bool FSeinLatentActionCodecRegistry::RecomputeRecordDigestForVerifiedRoot(
	FSeinSnapshotLatentActionRecord& InOutRecord,
	FString& OutError)
{
	return ComputeRecordDigest(
		InOutRecord, InOutRecord.RecordDigest, OutError);
}

bool FSeinLatentActionCodecRegistry::StageRecords(
	const FSeinLatentActionCodecManifest& Manifest,
	const ISeinCanonicalStateCandidateView& Candidate,
	const FSeinCanonicalStateRestorePlan& NativeState,
	const USeinWorldSubsystem& Services,
	int32 Tick,
	int64 NextActionID,
	int64 NextAbilityActivationID,
	TConstArrayView<FSeinSnapshotLatentActionRecord> Records,
	const FGuid& ExpectedSequenceDigest,
	FSeinLatentActionRestorePlan& OutPlan,
	FString& OutError)
{
	check(IsInGameThread());
	OutPlan.Reset();
	OutError.Reset();
	if (!Manifest.IsValid()
		|| !NativeState.IsReady()
		|| !ExpectedSequenceDigest.IsValid()
		|| FSeinStateProviderTransactionScope::IsActive()
		|| NextActionID <= 0
		|| NextAbilityActivationID <= 0
		|| Records.Num() > MaxActiveActions)
	{
		OutError =
			TEXT("Latent restore requires valid frozen manifests, native staging, allocator cursors, and sequence identity.");
		return false;
	}

	// Authenticate the complete sequence and every record before resolving or
	// invoking any module-owned codec callback.
	int64 PreflightPreviousActionID = 0;
	int64 PreflightAggregateBytes = 0;
	for (int32 Ordinal = 0; Ordinal < Records.Num(); ++Ordinal)
	{
		const FSeinSnapshotLatentActionRecord& Record =
			Records[Ordinal];
		PreflightAggregateBytes += Record.PayloadBytes.Num();
		FGuid PayloadDigest;
		FGuid RecordDigest;
		if (Record.Ordinal != Ordinal
			|| Record.ActionID <= PreflightPreviousActionID
			|| Record.ActionID >= NextActionID
			|| Record.AbilityPoolID < 0
			|| Record.AbilityActivationID <= 0
			|| Record.AbilityActivationID
				>= NextAbilityActivationID
			|| !Record.OwnerEntity.IsValid()
			|| Record.ActionClassPath.IsEmpty()
			|| Record.ActionClassPath.Len() > 1024
			|| Record.StableCodecID.IsEmpty()
			|| Record.StableCodecID.Len() > 256
			|| Record.PayloadBytes.Num() > MaxPayloadBytes
			|| PreflightAggregateBytes > MaxAggregatePayloadBytes
			|| !ComputePayloadDigest(
				Record, PayloadDigest, OutError)
			|| PayloadDigest != Record.PayloadDigest
			|| !ComputeRecordDigest(
				Record, RecordDigest, OutError)
			|| RecordDigest != Record.RecordDigest)
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Latent continuation sequence failed structural or digest preflight.");
			}
			return false;
		}
		PreflightPreviousActionID = Record.ActionID;
	}
	FGuid SequenceDigest;
	if (!ComputeSequenceDigest(
		NextActionID,
		NextAbilityActivationID,
		Records,
		SequenceDigest,
		OutError)
		|| SequenceDigest != ExpectedSequenceDigest)
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("Latent continuation sequence digest mismatch.");
		}
		return false;
	}

	// From this point onward local state contains module-owned codec callables
	// and restore-stage vtables. Declare the transaction before that state so
	// every success or failure unwind destroys it while mutation is blocked.
	FInvocationScope ProviderTransaction;
	TUniquePtr<FSeinLatentActionRestorePlan::FData> Plan =
		MakeUnique<FSeinLatentActionRestorePlan::FData>();
	Plan->Items.Reserve(Records.Num());
	Plan->NextActionID = NextActionID;
	Plan->NextAbilityActivationID =
		NextAbilityActivationID;

	int64 PreviousActionID = 0;
	int64 AggregateBytes = 0;
	for (int32 Ordinal = 0; Ordinal < Records.Num(); ++Ordinal)
	{
		const FSeinSnapshotLatentActionRecord& Record =
			Records[Ordinal];
		AggregateBytes += Record.PayloadBytes.Num();
		if (Record.Ordinal != Ordinal
			|| Record.ActionID <= PreviousActionID
			|| Record.ActionID >= NextActionID
			|| Record.AbilityPoolID < 0
			|| Record.AbilityActivationID <= 0
			|| Record.AbilityActivationID
				>= NextAbilityActivationID
			|| !Record.OwnerEntity.IsValid()
			|| Record.ActionClassPath.IsEmpty()
			|| Record.ActionClassPath.Len() > 1024
			|| Record.StableCodecID.IsEmpty()
			|| Record.StableCodecID.Len() > 256
			|| Record.PayloadBytes.Num() > MaxPayloadBytes
			|| AggregateBytes > MaxAggregatePayloadBytes)
		{
			OutError =
				TEXT("Latent continuation records are malformed, reordered, or exceed bounds.");
			return false;
		}
		PreviousActionID = Record.ActionID;

		const int32 EntryIndex = FindManifestEntryIndexByPath(
				Manifest, Record.ActionClassPath);
		if (EntryIndex == INDEX_NONE)
		{
			OutError =
				TEXT("Latent continuation action class is absent from the local frozen manifest.");
			return false;
		}
		const FSeinLatentActionCodecManifest::FData::FEntry& Entry =
			Manifest.Data->Entries[EntryIndex];
		if (Record.StableCodecID
				!= Entry.Descriptor.StableCodecId
			|| Record.StateSchemaVersion
				!= Entry.Descriptor.StateSchemaVersion
			|| Record.BehaviorRevision
				!= Entry.Descriptor.BehaviorRevision
			|| Record.CodecRevision
				!= Entry.Descriptor.CodecRevision
			|| Record.CodecDescriptorDigest
				!= Entry.DescriptorDigest
			|| Record.PayloadSchemaDigest
				!= Entry.Descriptor.PayloadSchemaDigest)
		{
			OutError =
				TEXT("Latent continuation codec identity does not match the local frozen manifest.");
			return false;
		}

		FGuid PayloadDigest;
		FGuid RecordDigest;
		if (!ComputePayloadDigest(
			Record, PayloadDigest, OutError)
			|| PayloadDigest != Record.PayloadDigest
			|| !ComputeRecordDigest(
				Record, RecordDigest, OutError)
			|| RecordDigest != Record.RecordDigest)
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Latent continuation digest mismatch.");
			}
			return false;
		}

		const USeinAbility* Ability =
			Candidate.FindAbility(Record.AbilityPoolID);
		if (!Ability
			|| !Ability->bIsActive
			|| Ability->OwnerEntity != Record.OwnerEntity
			|| Ability->GetActivationID()
				!= Record.AbilityActivationID
			|| !Candidate.IsEntityValid(Record.OwnerEntity))
		{
			OutError =
				TEXT("Latent continuation references an invalid staged entity or ability activation.");
			return false;
		}

		FCodecClaim Claim;
		if (!ResolveClaim(Entry.Token, Claim, OutError)
			|| Claim.DescriptorDigest != Entry.DescriptorDigest
			|| Claim.Descriptor.SupportedClass
				!= Entry.Descriptor.SupportedClass)
		{
			return false;
		}

		FInstancedStruct Payload;
		if (!DecodePayload(
			Claim.Descriptor,
			Record.PayloadBytes,
			Payload,
			OutError))
		{
			return false;
		}

		TMap<FString, const FInstancedStruct*> Dependencies;
		for (const FSeinCanonicalStateKey& Key :
			Claim.Descriptor.RequiredNativeContributors)
		{
			const FString Canonical =
				FSeinCanonicalStateRegistry::CanonicalKey(Key);
			const FInstancedStruct* Dependency =
				NativeState.FindStagedPayload(Key);
			if (!Dependency)
			{
				OutError = FString::Printf(
					TEXT("Latent codec '%s' could not access required staged contributor '%s'."),
					*Claim.Descriptor.StableCodecId,
					*Canonical);
				return false;
			}
			Dependencies.Add(Canonical, Dependency);
		}
		const FDependencyPayloadView DependencyView(Dependencies);

		FSeinLatentActionRestorePlan::FData::FItem& Item =
			Plan->Items.AddDefaulted_GetRef();
		Item.Token = Entry.Token;
		Item.Ops = Claim.Ops;
		Item.Record = Record;
		Item.ExactActionClass =
			Entry.Descriptor.SupportedClass;
		FSeinLatentActionStageContext Context;
		Context.Tick = Tick;
		Context.Candidate = &Candidate;
		Context.Dependencies = &DependencyView;
		Context.Services = &Services;
		Context.Record = &Item.Record;
		if (!Item.Ops.StageRestore(
			Context,
			Payload,
			Item.Stage,
			OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Latent codec staging failed without a diagnostic.");
			}
			return false;
		}
	}

	Plan->bReady = true;
	OutPlan.Data = MoveTemp(Plan);
	return true;
}

#if WITH_DEV_AUTOMATION_TESTS
bool FSeinLatentActionCodecRegistry::RecomputeRecordDigestsForTests(
	int64 NextActionID,
	int64 NextAbilityActivationID,
	TArray<FSeinSnapshotLatentActionRecord>& InOutRecords,
	FGuid& OutSequenceDigest,
	FString& OutError)
{
	check(IsInGameThread());
	OutSequenceDigest.Invalidate();
	OutError.Reset();
	if (NextActionID <= 0
		|| NextAbilityActivationID <= 0
		|| InOutRecords.Num() > MaxActiveActions)
	{
		OutError =
			TEXT("Test latent digest recomputation received invalid bounds.");
		return false;
	}

	int64 AggregateBytes = 0;
	for (FSeinSnapshotLatentActionRecord& Record : InOutRecords)
	{
		AggregateBytes += Record.PayloadBytes.Num();
		if (Record.PayloadBytes.Num() > MaxPayloadBytes
			|| AggregateBytes > MaxAggregatePayloadBytes
			|| !ComputePayloadDigest(
				Record, Record.PayloadDigest, OutError)
			|| !ComputeRecordDigest(
				Record, Record.RecordDigest, OutError))
		{
			return false;
		}
	}
	return ComputeSequenceDigest(
		NextActionID,
		NextAbilityActivationID,
		InOutRecords,
		OutSequenceDigest,
		OutError);
}
#endif

int32 FSeinLatentActionCodecRegistry::GetRegisteredCodecCount()
{
	check(IsInGameThread());
	int32 Count = 0;
	for (const FCodecEntry& Entry : Registry())
	{
		Count += Entry.Claims.Num();
	}
	return Count;
}

bool FSeinLatentActionCodecRegistry::HasRegisteredCodecForExactClass(
	const UClass* ExactClass,
	FString* OutError)
{
	check(IsInGameThread());
	SetError(OutError, FString());
	if (!ExactClass
		|| !ExactClass->IsChildOf(USeinLatentAction::StaticClass())
		|| ExactClass->HasAnyClassFlags(
			CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		SetError(
			OutError,
			TEXT("Checkpoint authoring requires an exact concrete USeinLatentAction class."));
		return false;
	}

	const FCodecEntry* Entry = Registry().FindByPredicate(
		[ExactClass](const FCodecEntry& Candidate)
		{
			return Candidate.ClassPath == ExactClass->GetPathName();
		});
	if (!Entry || Entry->Claims.IsEmpty()
		|| Entry->Claims.Last().Descriptor.SupportedClass != ExactClass)
	{
		SetError(
			OutError,
			FString::Printf(
				TEXT("Exact latent action class '%s' has no live checkpoint codec registration."),
				*ExactClass->GetPathName()));
		return false;
	}
	return true;
}

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarStateCodecRegistry.cpp
 */

#include "Serialization/SeinFogOfWarStateCodecRegistry.h"

#include "Input/SeinCommandSchemaRegistry.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "SeinARTSFogOfWarLog.h"
#include "SeinFogOfWar.h"
#include "SeinFogOfWarSubsystem.h"
#include "UObject/FieldIterator.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

namespace
{
	constexpr int32 MaxConcretePayloadBytes = 32 * 1024 * 1024;

	int32& InvocationDepth()
	{
		static int32 Value = 0;
		return Value;
	}

	class FInvocationScope
	{
	public:
		FInvocationScope()
		{
			check(IsInGameThread());
			check(InvocationDepth() == 0);
			++InvocationDepth();
		}

		~FInvocationScope()
		{
			check(InvocationDepth() == 1);
			--InvocationDepth();
		}
	};

	struct FCodecClaim
	{
		uint64 Token = 0;
		FString Owner;
		FSeinFogOfWarStateCodecDescriptor Descriptor;
		FGuid CodecDescriptorDigest;
		FSeinFogOfWarStateCodecOps Ops;
		TStrongObjectPtr<UClass> ClassRoot;
		TStrongObjectPtr<UScriptStruct> PayloadRoot;
		TArray<TStrongObjectPtr<UScriptStruct>> DynamicRoots;
	};

	struct FCodecEntry
	{
		FString SupportedClassPath;
		FString Owner;
		FString StableImplementationId;
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

	uint64 AllocateToken()
	{
		uint64& Candidate = NextToken();
		if (Candidate == 0)
		{
			Candidate = 1;
		}
		return Candidate++;
	}

	FSeinStructWireLimits BuildWireLimits(
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

	int32 InheritanceDistance(
		const UClass* Concrete,
		const FSeinFogOfWarStateCodecDescriptor& Descriptor)
	{
		const UClass* Supported = Descriptor.SupportedClass;
		if (!Concrete || !Supported)
		{
			return INDEX_NONE;
		}

		int32 Distance = 0;
		for (const UClass* Cursor = Concrete;
			Cursor;
			Cursor = Cursor->GetSuperClass(), ++Distance)
		{
			if (Cursor == Supported)
			{
				return Distance;
			}
			if (Descriptor.SubclassPolicy
					!= ESeinFogOfWarStateCodecSubclassPolicy::
						DataOnlyBlueprintGeneratedChildren
				|| Cursor->HasAnyClassFlags(CLASS_Native)
				|| !Cursor->HasAnyClassFlags(
					CLASS_CompiledFromBlueprint))
			{
				return INDEX_NONE;
			}

			// An inherited codec can safely serve only a data-only Blueprint
			// layer. Added properties may carry future state and added
			// functions may change future behavior, neither of which the
			// ancestor codec can describe.
			if (TFieldIterator<FProperty> Property(
					Cursor, EFieldIteratorFlags::ExcludeSuper);
				Property)
			{
				return INDEX_NONE;
			}
			if (TFieldIterator<UFunction> Function(
					Cursor, EFieldIteratorFlags::ExcludeSuper);
				Function)
			{
				return INDEX_NONE;
			}
		}
		return INDEX_NONE;
	}

	bool CanonicalizeDescriptor(
		FName OwnerModuleId,
		const FSeinFogOfWarStateCodecDescriptor& Descriptor,
		FSeinFogOfWarStateCodecDescriptor& OutDescriptor,
		FString& OutOwner,
		FGuid& OutCodecDescriptorDigest,
		FString& OutError)
	{
		OutOwner = OwnerModuleId.ToString().ToLower();
		OutDescriptor = Descriptor;
		OutDescriptor.StableImplementationId =
			Descriptor.StableImplementationId.ToLower();

		if (!IsStableIdentifier(OutOwner)
			|| !IsStableIdentifier(
				OutDescriptor.StableImplementationId)
			|| !OutDescriptor.SupportedClass
			|| !OutDescriptor.SupportedClass->IsChildOf(
				USeinFogOfWar::StaticClass())
			|| static_cast<uint8>(
				OutDescriptor.SubclassPolicy)
				> static_cast<uint8>(
					ESeinFogOfWarStateCodecSubclassPolicy::
						DataOnlyBlueprintGeneratedChildren)
			|| !OutDescriptor.PayloadStruct
			|| OutDescriptor.StateSchemaVersion == 0
			|| OutDescriptor.BehaviorRevision == 0
			|| OutDescriptor.CodecRevision == 0
			|| OutDescriptor.Limits.MaxRecursionDepth <= 0
			|| OutDescriptor.Limits.MaxEncodedBytes <= 0
			|| OutDescriptor.Limits.MaxEncodedBytes
				> MaxConcretePayloadBytes
			|| OutDescriptor.Limits.MaxAggregateElements <= 0)
		{
			OutError =
				TEXT("Fog state codecs require stable IDs, a supported fog class, a payload schema, positive revisions, and bounded limits.");
			return false;
		}

		FGuid ComputedPayloadDigest;
		if (!FSeinCanonicalStateCodec::ComputeSchemaDigest(
			OutDescriptor.PayloadStruct,
			ComputedPayloadDigest,
			OutError)
			|| !Descriptor.PayloadSchemaDigest.IsValid()
			|| Descriptor.PayloadSchemaDigest != ComputedPayloadDigest)
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Fog state codec payload schema digest does not match its concrete payload type.");
			}
			return false;
		}
		OutDescriptor.PayloadSchemaDigest = ComputedPayloadDigest;

		if (OutDescriptor.DynamicPayloadStructs.Contains(nullptr))
		{
			OutError =
				TEXT("Fog state codec dynamic payload schemas must be non-null and unique.");
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
					TEXT("Fog state codec dynamic payload schemas must be non-null and unique.");
				return false;
			}
		}
		FString IgnoredNameManifest;
		SeinBuildCanonicalWireNameCatalog(
			Descriptor.AllowedNames,
			OutDescriptor.AllowedNames,
			IgnoredNameManifest);

		FSeinCanonicalStateDescriptor PayloadDescriptor;
		PayloadDescriptor.Key.StableDomainId =
			TEXT("seinarts.fog.codec");
		PayloadDescriptor.Key.StableContributorId =
			FName(*OutDescriptor.StableImplementationId);
		PayloadDescriptor.SchemaVersion =
			OutDescriptor.StateSchemaVersion;
		PayloadDescriptor.ImplementationRevision =
			OutDescriptor.CodecRevision;
		PayloadDescriptor.Role =
			ESeinCanonicalStateRole::Authoritative;
		PayloadDescriptor.PayloadStruct =
			OutDescriptor.PayloadStruct;
		PayloadDescriptor.DynamicPayloadStructs =
			OutDescriptor.DynamicPayloadStructs;
		PayloadDescriptor.AllowedNames =
			OutDescriptor.AllowedNames;
		PayloadDescriptor.Limits = OutDescriptor.Limits;

		FString PayloadManifest;
		FGuid PayloadDescriptorDigest;
		if (!FSeinCanonicalStateRegistry::BuildDescriptorIdentity(
			PayloadDescriptor,
			PayloadManifest,
			PayloadDescriptorDigest,
			OutError))
		{
			return false;
		}

		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.FogOfWar.StateCodecClaim"), 2);
		if (!Writer.WriteString(
				OutDescriptor.SupportedClass->GetPathName())
			|| !Writer.WriteUInt8(static_cast<uint8>(
				OutDescriptor.SubclassPolicy))
			|| !Writer.WriteString(
				OutDescriptor.StableImplementationId)
			|| !Writer.WriteUInt32(
				OutDescriptor.StateSchemaVersion)
			|| !Writer.WriteUInt32(
				OutDescriptor.BehaviorRevision)
			|| !Writer.WriteUInt32(
				OutDescriptor.CodecRevision)
			|| !Writer.WriteGuid(
				OutDescriptor.PayloadSchemaDigest)
			|| !Writer.WriteGuid(PayloadDescriptorDigest)
			|| !Writer.Finalize(
				OutCodecDescriptorDigest, OutError))
		{
			return false;
		}
		return true;
	}

	bool ValidateOps(
		const FSeinFogOfWarStateCodecOps& Ops,
		FString& OutError)
	{
		if (!Ops.ComputeStaticEnvironmentDigest
			|| !Ops.Capture
			|| !Ops.StageRestore
			|| !Ops.CommitRestore)
		{
			OutError =
				TEXT("Fog state codec requires static-identity, capture, stage, and atomic-commit callbacks.");
			return false;
		}
		return true;
	}

}

FSeinFogOfWarStateCodecRegistrationHandle::
	~FSeinFogOfWarStateCodecRegistrationHandle()
{
	Reset();
}

FSeinFogOfWarStateCodecRegistrationHandle::
	FSeinFogOfWarStateCodecRegistrationHandle(
		FSeinFogOfWarStateCodecRegistrationHandle&& Other) noexcept
	: Token(Other.Token)
{
	check(IsInGameThread());
	Other.Token = 0;
}

FSeinFogOfWarStateCodecRegistrationHandle&
FSeinFogOfWarStateCodecRegistrationHandle::operator=(
	FSeinFogOfWarStateCodecRegistrationHandle&& Other) noexcept
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

void FSeinFogOfWarStateCodecRegistrationHandle::Reset()
{
	if (Token != 0)
	{
		check(IsInGameThread());
		check(InvocationDepth() == 0);
		const bool bRemoved =
			FSeinFogOfWarStateCodecRegistry::UnregisterToken(Token);
		check(bRemoved);
		Token = 0;
	}
}

FSeinFogOfWarStateCodecRegistrationHandle
FSeinFogOfWarStateCodecRegistry::Register(
	FName OwnerModuleId,
	const FSeinFogOfWarStateCodecDescriptor& Descriptor,
	FSeinFogOfWarStateCodecOps Ops,
	FString* OutError)
{
	check(IsInGameThread());
	SetError(OutError, FString());
	if (InvocationDepth() != 0)
	{
		SetError(
			OutError,
			TEXT("Fog state codecs may not register during a codec callback transaction."));
		return {};
	}

	FSeinFogOfWarStateCodecDescriptor CanonicalDescriptor;
	FString Owner;
	FGuid CodecDescriptorDigest;
	FString Error;
	if (!CanonicalizeDescriptor(
		OwnerModuleId,
		Descriptor,
		CanonicalDescriptor,
		Owner,
		CodecDescriptorDigest,
		Error)
		|| !ValidateOps(Ops, Error))
	{
		SetError(OutError, Error);
		UE_LOG(LogSeinFogOfWar, Error,
			TEXT("Rejected fog state codec: %s"), *Error);
		return {};
	}

	const FString ClassPath =
		CanonicalDescriptor.SupportedClass->GetPathName();
	FCodecEntry* Entry = Registry().FindByPredicate(
		[&ClassPath](const FCodecEntry& Candidate)
		{
			return Candidate.SupportedClassPath == ClassPath;
		});
	if (Entry)
	{
		if (Entry->Owner != Owner
			|| Entry->StableImplementationId
				!= CanonicalDescriptor.StableImplementationId)
		{
			Error = FString::Printf(
				TEXT("Conflicting fog state codec claim for '%s'."),
				*ClassPath);
			SetError(OutError, Error);
			UE_LOG(LogSeinFogOfWar, Error, TEXT("%s"), *Error);
			return {};
		}
	}
	else
	{
		Entry = &Registry().AddDefaulted_GetRef();
		Entry->SupportedClassPath = ClassPath;
		Entry->Owner = Owner;
		Entry->StableImplementationId =
			CanonicalDescriptor.StableImplementationId;
	}

	FCodecClaim Claim;
	Claim.Token = AllocateToken();
	Claim.Owner = Owner;
	Claim.Descriptor = MoveTemp(CanonicalDescriptor);
	Claim.CodecDescriptorDigest = CodecDescriptorDigest;
	Claim.Ops = MoveTemp(Ops);
	Claim.ClassRoot.Reset(
		const_cast<UClass*>(Claim.Descriptor.SupportedClass));
	Claim.PayloadRoot.Reset(
		const_cast<UScriptStruct*>(Claim.Descriptor.PayloadStruct));
	for (const UScriptStruct* Dynamic :
		Claim.Descriptor.DynamicPayloadStructs)
	{
		Claim.DynamicRoots.Emplace(
			const_cast<UScriptStruct*>(Dynamic));
	}
	const uint64 Token = Claim.Token;
	Entry->Claims.Add(MoveTemp(Claim));
	return FSeinFogOfWarStateCodecRegistrationHandle(Token);
}

bool FSeinFogOfWarStateCodecRegistry::Unregister(
	FSeinFogOfWarStateCodecRegistrationHandle& Handle)
{
	check(IsInGameThread());
	if (!Handle.IsValid() || InvocationDepth() != 0)
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

bool FSeinFogOfWarStateCodecRegistry::UnregisterToken(uint64 Token)
{
	check(IsInGameThread());
	check(InvocationDepth() == 0);
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

		// This executes while the concrete module's vtables/callbacks are
		// still valid. Active worlds are stopped and release their fog object
		// before the claim and its reflected roots disappear.
		for (TObjectIterator<USeinFogOfWarSubsystem> It; It; ++It)
		{
			if (!It->HasAnyFlags(RF_ClassDefaultObject))
			{
				It->InvalidateCanonicalStateCodecLease(
					Token,
					TEXT("The exact fog-of-war state codec generation unloaded."));
			}
		}
		Entry.Claims.RemoveAt(ClaimIndex);
		if (Entry.Claims.IsEmpty())
		{
			Registry().RemoveAt(EntryIndex);
		}
		return true;
	}
	return false;
}

bool FSeinFogOfWarStateCodecRegistry::FreezeForClass(
	const UClass* FogClass,
	uint64& OutToken,
	FString& OutError)
{
	check(IsInGameThread());
	OutToken = 0;
	OutError.Reset();
	if (!FogClass || !FogClass->IsChildOf(USeinFogOfWar::StaticClass()))
	{
		OutError = TEXT("Fog state codec binding requires a fog class.");
		return false;
	}

	const FCodecClaim* Best = nullptr;
	int32 BestDistance = MAX_int32;
	for (const FCodecEntry& Entry : Registry())
	{
		if (Entry.Claims.IsEmpty())
		{
			continue;
		}
		const FCodecClaim& Claim = Entry.Claims.Last();
		const int32 Distance = InheritanceDistance(
			FogClass, Claim.Descriptor);
		if (Distance != INDEX_NONE && Distance < BestDistance)
		{
			Best = &Claim;
			BestDistance = Distance;
		}
	}
	if (!Best)
	{
		OutError = FString::Printf(
			TEXT("FogOfWarClass '%s' has no exact reload-safe state codec claim."),
			*FogClass->GetPathName());
		return false;
	}

	OutToken = Best->Token;
	return true;
}

bool FSeinFogOfWarStateCodecRegistry::Resolve(
	uint64 Token,
	FResolvedClaim& OutClaim,
	FString& OutError)
{
	check(IsInGameThread());
	OutClaim = {};
	OutError.Reset();
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
			OutClaim.Descriptor = Claim->Descriptor;
			OutClaim.CodecDescriptorDigest =
				Claim->CodecDescriptorDigest;
			OutClaim.Ops = Claim->Ops;
			return true;
		}
	}
	OutError =
		TEXT("The frozen fog state codec generation is unavailable.");
	return false;
}

bool FSeinFogOfWarStateCodecRegistry::ResolveForClass(
	uint64 Token,
	const UClass* FogClass,
	FResolvedClaim& OutClaim,
	FString& OutError)
{
	if (!Resolve(Token, OutClaim, OutError))
	{
		return false;
	}
	if (InheritanceDistance(FogClass, OutClaim.Descriptor)
		== INDEX_NONE)
	{
		OutError = FString::Printf(
			TEXT("Fog state codec '%s' for '%s' does not explicitly admit active class '%s'. Native or stateful Blueprint subclasses require their own exact codec claim."),
			*OutClaim.Descriptor.StableImplementationId,
			OutClaim.Descriptor.SupportedClass
				? *OutClaim.Descriptor.SupportedClass->GetPathName()
				: TEXT("<null>"),
			FogClass ? *FogClass->GetPathName() : TEXT("<null>"));
		OutClaim = {};
		return false;
	}
	return true;
}

bool FSeinFogOfWarStateCodecRegistry::IsTokenAvailable(uint64 Token)
{
	FResolvedClaim Ignored;
	FString Error;
	return Resolve(Token, Ignored, Error);
}

bool FSeinFogOfWarStateCodecRegistry::ComputeStaticEnvironmentDigest(
	uint64 Token,
	const USeinFogOfWar& Fog,
	FGuid& OutDigest,
	FString& OutError)
{
	OutDigest.Invalidate();
	FResolvedClaim Claim;
	if (InvocationDepth() != 0
		|| !ResolveForClass(
			Token, Fog.GetClass(), Claim, OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("Fog codec callback transaction is already active.");
		}
		return false;
	}
	FInvocationScope Scope;
	if (!Claim.Ops.ComputeStaticEnvironmentDigest(
		Fog, OutDigest, OutError)
		|| !OutDigest.IsValid())
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("Fog codec returned an invalid static-environment digest.");
		}
		OutDigest.Invalidate();
		return false;
	}
	return true;
}

bool FSeinFogOfWarStateCodecRegistry::CapturePayload(
	uint64 Token,
	const FSeinFogOfWarStateCaptureContext& Context,
	FInstancedStruct& OutPayload,
	FString& OutError)
{
	OutPayload.Reset();
	FResolvedClaim Claim;
	if (InvocationDepth() != 0
		|| !ResolveForClass(
			Token, Context.Fog.GetClass(), Claim, OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("Fog codec callback transaction is already active.");
		}
		return false;
	}
	FInvocationScope Scope;
	if (!Claim.Ops.Capture(Context, OutPayload, OutError)
		|| !OutPayload.IsValid()
		|| OutPayload.GetScriptStruct()
			!= Claim.Descriptor.PayloadStruct)
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("Fog codec capture returned the wrong payload type.");
		}
		OutPayload.Reset();
		return false;
	}
	return true;
}

bool FSeinFogOfWarStateCodecRegistry::CaptureRoutineRoot(
	uint64 Token,
	const FSeinFogOfWarStateCaptureContext& Context,
	bool bForceFullRebuild,
	FGuid& OutPayloadDigest,
	uint64& OutProjectedPayloadBytes,
	uint64& OutMutationRevision,
	FString& OutError)
{
	OutPayloadDigest.Invalidate();
	OutProjectedPayloadBytes = 0;
	OutMutationRevision = 0;
	FResolvedClaim Claim;
	if (InvocationDepth() != 0
		|| !ResolveForClass(
			Token, Context.Fog.GetClass(), Claim, OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("Fog codec callback transaction is already active.");
		}
		return false;
	}
	if (!Claim.Ops.CaptureRoutineRoot)
	{
		OutError = FString::Printf(
			TEXT("Fog codec '%s' does not provide a non-blocking routine-root projection."),
			*Claim.Descriptor.StableImplementationId);
		return false;
	}
	FInvocationScope Scope;
	if (!Claim.Ops.CaptureRoutineRoot(
			Context,
			bForceFullRebuild,
			OutPayloadDigest,
			OutProjectedPayloadBytes,
			OutMutationRevision,
			OutError)
		|| !OutPayloadDigest.IsValid())
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("Fog codec returned an invalid routine-root digest.");
		}
		OutPayloadDigest.Invalidate();
		OutProjectedPayloadBytes = 0;
		OutMutationRevision = 0;
		return false;
	}
	return true;
}

bool FSeinFogOfWarStateCodecRegistry::StagePayload(
	uint64 Token,
	const FSeinFogOfWarStateStageContext& Context,
	const FInstancedStruct& Payload,
	TUniquePtr<ISeinFogOfWarStateRestoreStage>& OutStage,
	FString& OutError)
{
	OutStage.Reset();
	FResolvedClaim Claim;
	if (InvocationDepth() != 0
		|| !ResolveForClass(
			Token, Context.Fog.GetClass(), Claim, OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("Fog codec callback transaction is already active.");
		}
		return false;
	}
	if (!Payload.IsValid()
		|| Payload.GetScriptStruct()
			!= Claim.Descriptor.PayloadStruct)
	{
		OutError =
			TEXT("Fog codec stage received the wrong payload type.");
		return false;
	}
	FInvocationScope Scope;
	if (!Claim.Ops.StageRestore(
		Context, Payload, OutStage, OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("Fog codec restore staging failed.");
		}
		OutStage.Reset();
		return false;
	}
	return true;
}

void FSeinFogOfWarStateCodecRegistry::CommitPayload(
	uint64 Token,
	FSeinFogOfWarStateCommitContext& Context,
	TUniquePtr<ISeinFogOfWarStateRestoreStage>&& Stage)
{
	check(IsInGameThread());
	check(InvocationDepth() == 0);
	FResolvedClaim Claim;
	FString Error;
	checkf(
		ResolveForClass(
			Token, Context.Fog.GetClass(), Claim, Error),
		TEXT("Fog codec generation disappeared after final lease verification: %s"),
		*Error);
	FInvocationScope Scope;
	Claim.Ops.CommitRestore(Context, MoveTemp(Stage));
}

bool FSeinFogOfWarStateCodecRegistry::EncodePayload(
	const FResolvedClaim& Claim,
	const FInstancedStruct& Payload,
	TArray<uint8>& OutBytes,
	FString& OutError)
{
	OutBytes.Reset();
	if (!Payload.IsValid()
		|| Payload.GetScriptStruct()
			!= Claim.Descriptor.PayloadStruct)
	{
		OutError =
			TEXT("Fog codec payload does not match its exact schema.");
		return false;
	}
	FSeinWireCost Cost;
	return FSeinCanonicalStateCodec::EncodeWithCost(
		Payload.GetScriptStruct(),
		Payload.GetMemory(),
		{ Claim.Descriptor.DynamicPayloadStructs,
			Claim.Descriptor.AllowedNames },
		BuildWireLimits(Claim.Descriptor.Limits),
		OutBytes,
		OutError,
		Cost);
}

bool FSeinFogOfWarStateCodecRegistry::DecodePayload(
	const FResolvedClaim& Claim,
	TConstArrayView<uint8> Bytes,
	FInstancedStruct& OutPayload,
	FString& OutError)
{
	OutPayload.Reset();
	if (Bytes.Num()
		> Claim.Descriptor.Limits.MaxEncodedBytes)
	{
		OutError =
			TEXT("Fog codec payload exceeds its frozen byte bound.");
		return false;
	}
	OutPayload.InitializeAs(Claim.Descriptor.PayloadStruct);
	FSeinWireCost Cost;
	if (!FSeinCanonicalStateCodec::DecodeWithCost(
		Bytes,
		Claim.Descriptor.PayloadStruct,
		OutPayload.GetMutableMemory(),
		{ Claim.Descriptor.DynamicPayloadStructs,
			Claim.Descriptor.AllowedNames },
		BuildWireLimits(Claim.Descriptor.Limits),
		OutError,
		Cost))
	{
		OutPayload.Reset();
		return false;
	}

	// Reject non-canonical alternate encodings even when they decode to the
	// same reflected value.
	TArray<uint8> CanonicalBytes;
	const auto BytesMatch = [&CanonicalBytes, Bytes]()
	{
		return CanonicalBytes.Num() == Bytes.Num()
			&& (Bytes.IsEmpty()
				|| FMemory::Memcmp(
					CanonicalBytes.GetData(),
					Bytes.GetData(),
					Bytes.Num()) == 0);
	};
	if (!EncodePayload(
		Claim, OutPayload, CanonicalBytes, OutError)
		|| !BytesMatch())
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("Fog codec payload is not in canonical byte form.");
		}
		OutPayload.Reset();
		return false;
	}
	return true;
}

int32 FSeinFogOfWarStateCodecRegistry::GetRegisteredCodecCount()
{
	check(IsInGameThread());
	int32 Count = 0;
	for (const FCodecEntry& Entry : Registry())
	{
		Count += Entry.Claims.Num();
	}
	return Count;
}

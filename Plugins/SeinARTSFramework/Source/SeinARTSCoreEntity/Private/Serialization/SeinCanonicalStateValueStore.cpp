/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCanonicalStateValueStore.cpp
 */

#include "Serialization/SeinCanonicalStateValueStore.h"

#include "Input/SeinCommandSchemaRegistry.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "UObject/GCObject.h"

namespace
{
#if WITH_DEV_AUTOMATION_TESTS
	TFunction<void()> GRestoreStagingTestHook;
#endif

	bool CanonicalizeDefinition(
		const FSeinCanonicalStateValueSlotDefinition& Definition,
		const FInstancedStruct& InitialValue,
		FSeinCanonicalStateDescriptor& OutDescriptor,
		TArray<FInstancedStruct>& OutDynamicSchemas,
		FString& OutError)
	{
		if (!InitialValue.IsValid()
			|| Definition.SchemaVersion <= 0
			|| Definition.ImplementationRevision <= 0)
		{
			OutError =
				TEXT("Value slots require a valid value and positive revisions.");
			return false;
		}

		OutDescriptor = {};
		OutDescriptor.Key = Definition.Key;
		OutDescriptor.SchemaVersion =
			static_cast<uint32>(Definition.SchemaVersion);
		OutDescriptor.ImplementationRevision =
			static_cast<uint32>(Definition.ImplementationRevision);
		OutDescriptor.Role = ESeinCanonicalStateRole::Authoritative;
		OutDescriptor.PayloadStruct = InitialValue.GetScriptStruct();
		OutDescriptor.Limits = Definition.Limits;

		OutDynamicSchemas = Definition.DynamicPayloadSchemas;
		for (const FInstancedStruct& Schema : OutDynamicSchemas)
		{
			if (!Schema.IsValid())
			{
				OutError =
					TEXT("Dynamic state payload schemas must contain concrete values.");
				return false;
			}
		}
		OutDynamicSchemas.Sort(
			[](const FInstancedStruct& A, const FInstancedStruct& B)
			{
				return A.GetScriptStruct()->GetPathName()
					< B.GetScriptStruct()->GetPathName();
			});
		for (int32 Index = 0; Index < OutDynamicSchemas.Num(); ++Index)
		{
			const UScriptStruct* Type =
				OutDynamicSchemas[Index].GetScriptStruct();
			if (Index > 0
				&& OutDynamicSchemas[Index - 1].GetScriptStruct()
					->GetPathName() == Type->GetPathName())
			{
				OutError =
					TEXT("Dynamic state payload schema types must be unique.");
				return false;
			}
			OutDescriptor.DynamicPayloadStructs.Add(Type);
		}

		FString IgnoredNameManifest;
		SeinBuildCanonicalWireNameCatalog(
			Definition.AllowedNames,
			OutDescriptor.AllowedNames,
			IgnoredNameManifest);
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

}

bool FSeinCanonicalStateValueStore::RegisterSlot(
	const FSeinCanonicalStateSchemaSnapshot& NativeSchema,
	const FSeinCanonicalStateValueSlotDefinition& Definition,
	const FInstancedStruct& InitialValue,
	FString& OutError)
{
	OutError.Reset();
	if (bSealed)
	{
		OutError =
			TEXT("Canonical state schema is sealed for this match.");
		return false;
	}
	if (!NativeSchema.IsValid())
	{
		OutError = TEXT("Native canonical state schema is unavailable.");
		return false;
	}
	if (!ValidateTrackedResourceBounds(OutError))
	{
		return false;
	}
	if (Slots.Num() >= MaxSlots)
	{
		OutError =
			TEXT("Canonical state value store exceeds the slot-count bound.");
		return false;
	}

	const FString Key =
		FSeinCanonicalStateRegistry::CanonicalKey(Definition.Key);
	if (Key.IsEmpty() || Slots.Contains(Definition.Key))
	{
		OutError =
			TEXT("Canonical state value slot key is invalid or already registered.");
		return false;
	}
	for (const FSeinFrozenCanonicalStateContributor& Native :
		NativeSchema.GetContributors())
	{
		if (FSeinCanonicalStateRegistry::CanonicalKey(
			Native.Descriptor.Key) == Key)
		{
			OutError = FString::Printf(
				TEXT("Canonical state key '%s' is already owned by a native contributor."),
				*Key);
			return false;
		}
	}

	FSlot Candidate;
	if (!CanonicalizeDefinition(
			Definition,
			InitialValue,
			Candidate.Descriptor,
			Candidate.DynamicSchemaValues,
			OutError)
		|| !FSeinCanonicalStateRegistry::BuildDescriptorIdentity(
			Candidate.Descriptor,
			Candidate.CanonicalDescriptor,
			Candidate.DescriptorDigest,
			OutError))
	{
		return false;
	}
	if (!CanonicalizeValue(
		Candidate,
		InitialValue,
		Candidate.Value,
		Candidate.PayloadBytes,
		Candidate.LeafDigest,
		OutError))
	{
		return false;
	}

	uint64 CandidateAggregateBytes = 0;
	if (!TryApplyPayloadChange(
		AggregatePayloadBytes,
		0,
		Candidate.PayloadBytes.Num(),
		CandidateAggregateBytes))
	{
		OutError =
			TEXT("Canonical state value store exceeds the aggregate payload bound.");
		return false;
	}

	Slots.Add(Definition.Key, MoveTemp(Candidate));
	AggregatePayloadBytes = CandidateAggregateBytes;
	return true;
}

bool FSeinCanonicalStateValueStore::SetValue(
	const FSeinCanonicalStateKey& Key,
	const FInstancedStruct& NewValue,
	FString& OutError)
{
	OutError.Reset();
	if (!ValidateTrackedResourceBounds(OutError))
	{
		return false;
	}
	FSlot* Slot = Slots.Find(Key);
	if (!Slot)
	{
		OutError = TEXT("Canonical state value slot is not registered.");
		return false;
	}

	TArray<uint8> CandidateBytes;
	FGuid CandidateDigest;
	FInstancedStruct CandidateValue;
	if (!CanonicalizeValue(
		*Slot,
		NewValue,
		CandidateValue,
		CandidateBytes,
		CandidateDigest,
		OutError))
	{
		return false;
	}

	uint64 CandidateAggregateBytes = 0;
	if (!TryApplyPayloadChange(
		AggregatePayloadBytes,
		Slot->PayloadBytes.Num(),
		CandidateBytes.Num(),
		CandidateAggregateBytes))
	{
		OutError =
			TEXT("Canonical state value store exceeds the aggregate payload bound.");
		return false;
	}

	Slot->Value = MoveTemp(CandidateValue);
	Slot->PayloadBytes = MoveTemp(CandidateBytes);
	Slot->LeafDigest = CandidateDigest;
	AggregatePayloadBytes = CandidateAggregateBytes;
	return true;
}

bool FSeinCanonicalStateValueStore::GetValue(
	const FSeinCanonicalStateKey& Key,
	FInstancedStruct& OutValue) const
{
	OutValue.Reset();
	const FSlot* Slot = Slots.Find(Key);
	if (!Slot)
	{
		return false;
	}
	OutValue = Slot->Value;
	return true;
}

bool FSeinCanonicalStateValueStore::Seal(
	const FSeinCanonicalStateSchemaSnapshot& NativeSchema,
	FString& OutError)
{
	return Seal(
		NativeSchema,
		TConstArrayView<FString>(),
		OutError);
}

bool FSeinCanonicalStateValueStore::Seal(
	const FSeinCanonicalStateSchemaSnapshot& NativeSchema,
	TConstArrayView<FString> AdditionalContractFrames,
	FString& OutError)
{
	OutError.Reset();
	if (!NativeSchema.IsValid())
	{
		OutError =
			TEXT("Native canonical state schema is unavailable.");
		return false;
	}
	if (!ValidateResourceBounds(OutError))
	{
		return false;
	}

	TArray<FString> AdditionalDescriptors;
	AdditionalDescriptors.Reserve(
		Slots.Num() + AdditionalContractFrames.Num());
	for (const auto& Pair : Slots)
	{
		AdditionalDescriptors.Add(Pair.Value.CanonicalDescriptor);
	}
	if (!AdditionalContractFrames.IsEmpty())
	{
		AdditionalDescriptors.Append(
			AdditionalContractFrames.GetData(),
			AdditionalContractFrames.Num());
	}
	FString CandidateManifest;
	FGuid CandidateDigest;
	if (!FSeinCanonicalStateRegistry::BuildCombinedContractIdentity(
		NativeSchema,
		AdditionalDescriptors,
		CandidateManifest,
		CandidateDigest,
		OutError))
	{
		return false;
	}

	if (bSealed)
	{
		if (ContractDigest == CandidateDigest
			&& CanonicalManifest == CandidateManifest)
		{
			return true;
		}
		OutError =
			TEXT("Canonical state schema is already sealed under a different contract.");
		return false;
	}

	CanonicalManifest = MoveTemp(CandidateManifest);
	ContractDigest = CandidateDigest;
	bSealed = true;
	return true;
}

bool FSeinCanonicalStateValueStore::CaptureRecords(
	TArray<FSeinCanonicalStateValueRecord>& OutRecords,
	FString& OutError) const
{
	OutRecords.Reset();
	OutError.Reset();
	if (!ValidateResourceBounds(OutError))
	{
		return false;
	}

	TArray<const FSlot*> Ordered;
	Ordered.Reserve(Slots.Num());
	for (const auto& Pair : Slots)
	{
		Ordered.Add(&Pair.Value);
	}
	Ordered.Sort(
		[](const FSlot& A, const FSlot& B)
		{
			return FSeinCanonicalStateRegistry::CanonicalKey(
				A.Descriptor.Key)
				< FSeinCanonicalStateRegistry::CanonicalKey(
					B.Descriptor.Key);
		});

	TArray<FSeinCanonicalStateValueRecord> CandidateRecords;
	CandidateRecords.Reserve(Ordered.Num());
	for (const FSlot* Slot : Ordered)
	{
		FSeinCanonicalStateValueRecord& Record =
			CandidateRecords.AddDefaulted_GetRef();
		Record.Key = Slot->Descriptor.Key;
		Record.SchemaVersion =
			static_cast<int32>(Slot->Descriptor.SchemaVersion);
		Record.ImplementationRevision =
			static_cast<int32>(
				Slot->Descriptor.ImplementationRevision);
		Record.PayloadStructPath =
			Slot->Descriptor.PayloadStruct->GetPathName();
		Record.DynamicPayloadStructPaths.Reset(
			Slot->Descriptor.DynamicPayloadStructs.Num());
		for (const UScriptStruct* Dynamic :
			Slot->Descriptor.DynamicPayloadStructs)
		{
			Record.DynamicPayloadStructPaths.Add(
				Dynamic->GetPathName());
		}
		Record.AllowedNames = Slot->Descriptor.AllowedNames;
		Record.Limits = Slot->Descriptor.Limits;
		Record.DescriptorDigest = Slot->DescriptorDigest;
		Record.PayloadBytes = Slot->PayloadBytes;
		Record.LeafDigest = Slot->LeafDigest;
	}
	OutRecords = MoveTemp(CandidateRecords);
	return true;
}

bool FSeinCanonicalStateValueStore::TryRestoreRecords(
	const FSeinCanonicalStateValueStore& ExpectedSchema,
	TConstArrayView<FSeinCanonicalStateValueRecord> Records,
	const FGuid& ExpectedContractDigest,
	FString& OutError)
{
	OutError.Reset();
	if (!ExpectedSchema.IsSealed()
		|| ExpectedSchema.GetContractDigest()
			!= ExpectedContractDigest
		|| !ExpectedContractDigest.IsValid()
		|| Records.Num() > MaxSlots)
	{
		OutError =
			TEXT("Local canonical state declaration or checkpoint contract is invalid.");
		return false;
	}

	uint64 CandidateAggregatePayloadBytes = 0;
	for (const FSeinCanonicalStateValueRecord& Record : Records)
	{
		uint64 NextAggregatePayloadBytes = 0;
		if (!TryApplyPayloadChange(
			CandidateAggregatePayloadBytes,
			0,
			Record.PayloadBytes.Num(),
			NextAggregatePayloadBytes))
		{
			OutError =
				TEXT("Canonical state value checkpoint exceeds the aggregate payload bound.");
			return false;
		}
		CandidateAggregatePayloadBytes = NextAggregatePayloadBytes;
	}

	TArray<FSeinCanonicalStateValueRecord> ExpectedRecords;
	if (!ExpectedSchema.CaptureRecords(ExpectedRecords, OutError))
	{
		return false;
	}
	if (Records.Num() != ExpectedRecords.Num())
	{
		OutError =
			TEXT("Checkpoint state-slot topology does not match the local declaration.");
		return false;
	}

	FSeinCanonicalStateValueStore CandidateStore = ExpectedSchema;
	class FCandidateStoreGCGuard final : public FGCObject
	{
	public:
		explicit FCandidateStoreGCGuard(
			FSeinCanonicalStateValueStore& InStore)
			: Store(InStore)
		{
		}

		virtual void AddReferencedObjects(
			FReferenceCollector& Collector) override
		{
			Store.AddReferencedObjects(Collector);
		}

		virtual FString GetReferencerName() const override
		{
			return TEXT("SeinCanonicalStateValueRestoreCandidate");
		}

	private:
		FSeinCanonicalStateValueStore& Store;
	};
	FCandidateStoreGCGuard CandidateStoreGCGuard(CandidateStore);
	for (int32 Index = 0; Index < Records.Num(); ++Index)
	{
		const FSeinCanonicalStateValueRecord& Record = Records[Index];
		const FSeinCanonicalStateValueRecord& Expected =
			ExpectedRecords[Index];
		const FString Key =
			FSeinCanonicalStateRegistry::CanonicalKey(Record.Key);
		const bool bExactLocalDescriptor =
			!Key.IsEmpty()
			&& Record.Key == Expected.Key
			&& Record.SchemaVersion == Expected.SchemaVersion
			&& Record.ImplementationRevision
				== Expected.ImplementationRevision
			&& Record.PayloadStructPath
				== Expected.PayloadStructPath
			&& Record.DynamicPayloadStructPaths
				== Expected.DynamicPayloadStructPaths
			&& Record.AllowedNames == Expected.AllowedNames
			&& Record.Limits.MaxRecursionDepth
				== Expected.Limits.MaxRecursionDepth
			&& Record.Limits.MaxEncodedBytes
				== Expected.Limits.MaxEncodedBytes
			&& Record.Limits.MaxAggregateElements
				== Expected.Limits.MaxAggregateElements
			&& Record.DescriptorDigest
				== Expected.DescriptorDigest;
		if (!bExactLocalDescriptor
			|| !Record.DescriptorDigest.IsValid()
			|| !Record.LeafDigest.IsValid())
		{
			OutError =
				TEXT("Checkpoint state-slot descriptor does not match the local declaration.");
			return false;
		}

		FSlot* Slot = CandidateStore.Slots.Find(Record.Key);
		if (!Slot
			|| !Slot->Descriptor.PayloadStruct
			|| Slot->DescriptorDigest
				!= Record.DescriptorDigest)
		{
			OutError =
				TEXT("Locally declared state slot disappeared during restore staging.");
			return false;
		}

#if WITH_DEV_AUTOMATION_TESTS
		if (GRestoreStagingTestHook)
		{
			GRestoreStagingTestHook();
		}
#endif

		FInstancedStruct CandidateValue;
		CandidateValue.InitializeAs(
			Slot->Descriptor.PayloadStruct);
		FSeinWireCost Cost;
		if (Record.PayloadBytes.Num()
				> Slot->Descriptor.Limits.MaxEncodedBytes
			|| !FSeinCanonicalStateCodec::DecodeWithCost(
				Record.PayloadBytes,
				Slot->Descriptor.PayloadStruct,
				CandidateValue.GetMutableMemory(),
				{ Slot->Descriptor.DynamicPayloadStructs,
					Slot->Descriptor.AllowedNames },
				BuildWireLimits(Slot->Descriptor.Limits),
				OutError,
				Cost)
			|| !EncodeValue(
				*Slot,
				CandidateValue,
				Slot->PayloadBytes,
				Slot->LeafDigest,
				OutError)
			|| Slot->PayloadBytes != Record.PayloadBytes
			|| Slot->LeafDigest != Record.LeafDigest)
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Canonical state value payload or leaf digest is invalid.");
			}
			return false;
		}
		Slot->Value = MoveTemp(CandidateValue);
	}
	CandidateStore.AggregatePayloadBytes =
		CandidateAggregatePayloadBytes;

	if (!CandidateStore.IsSealed()
		|| CandidateStore.GetContractDigest()
			!= ExpectedContractDigest
		|| !CandidateStore.ValidateResourceBounds(OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("Canonical state value contract changed during restore staging.");
		}
		return false;
	}

	*this = MoveTemp(CandidateStore);
	return true;
}

#if WITH_DEV_AUTOMATION_TESTS
void FSeinCanonicalStateValueStore::SetRestoreStagingTestHook(
	TFunction<void()> Hook)
{
	GRestoreStagingTestHook = MoveTemp(Hook);
}
#endif

void FSeinCanonicalStateValueStore::AddReferencedObjects(
	FReferenceCollector& Collector)
{
	for (auto& Pair : Slots)
	{
		Pair.Value.Value.AddStructReferencedObjects(Collector);
		for (FInstancedStruct& Schema :
			Pair.Value.DynamicSchemaValues)
		{
			Schema.AddStructReferencedObjects(Collector);
		}
	}
}

void FSeinCanonicalStateValueStore::Reset()
{
	Slots.Reset();
	CanonicalManifest.Reset();
	ContractDigest.Invalidate();
	AggregatePayloadBytes = 0;
	bSealed = false;
}

bool FSeinCanonicalStateValueStore::ValidateResourceBounds(
	FString& OutError) const
{
	if (!ValidateTrackedResourceBounds(OutError))
	{
		return false;
	}

	uint64 RecomputedPayloadBytes = 0;
	for (const auto& Pair : Slots)
	{
		uint64 NextPayloadBytes = 0;
		if (!TryApplyPayloadChange(
			RecomputedPayloadBytes,
			0,
			Pair.Value.PayloadBytes.Num(),
			NextPayloadBytes))
		{
			OutError =
				TEXT("Canonical state value store exceeds the aggregate payload bound.");
			return false;
		}
		RecomputedPayloadBytes = NextPayloadBytes;
	}
	if (RecomputedPayloadBytes != AggregatePayloadBytes)
	{
		OutError =
			TEXT("Canonical state value store payload accounting is inconsistent.");
		return false;
	}
	return true;
}

bool FSeinCanonicalStateValueStore::ValidateTrackedResourceBounds(
	FString& OutError) const
{
	if (Slots.Num() > MaxSlots)
	{
		OutError =
			TEXT("Canonical state value store exceeds the slot-count bound.");
		return false;
	}
	if (AggregatePayloadBytes > MaxAggregatePayloadBytes)
	{
		OutError =
			TEXT("Canonical state value store exceeds the aggregate payload bound.");
		return false;
	}
	return true;
}

bool FSeinCanonicalStateValueStore::TryApplyPayloadChange(
	uint64 CurrentBytes,
	int32 RemovedBytes,
	int32 AddedBytes,
	uint64& OutBytes)
{
	OutBytes = 0;
	if (RemovedBytes < 0
		|| AddedBytes < 0
		|| static_cast<uint64>(RemovedBytes) > CurrentBytes)
	{
		return false;
	}

	const uint64 RemainingBytes =
		CurrentBytes - static_cast<uint64>(RemovedBytes);
	const uint64 NewBytes = static_cast<uint64>(AddedBytes);
	if (NewBytes > MaxAggregatePayloadBytes
		|| RemainingBytes > MaxAggregatePayloadBytes - NewBytes)
	{
		return false;
	}
	OutBytes = RemainingBytes + NewBytes;
	return true;
}

bool FSeinCanonicalStateValueStore::EncodeValue(
	const FSlot& Slot,
	const FInstancedStruct& Value,
	TArray<uint8>& OutBytes,
	FGuid& OutLeafDigest,
	FString& OutError)
{
	OutBytes.Reset();
	OutLeafDigest.Invalidate();
	OutError.Reset();
	if (!Value.IsValid()
		|| Value.GetScriptStruct() != Slot.Descriptor.PayloadStruct)
	{
		OutError =
			TEXT("Canonical state value must match the slot's exact frozen root type.");
		return false;
	}

	const FSeinStructWireCatalogView Catalog{
		Slot.Descriptor.DynamicPayloadStructs,
		Slot.Descriptor.AllowedNames
	};
	FSeinWireCost Cost;
	if (!FSeinCanonicalStateCodec::EncodeWithCost(
		Value.GetScriptStruct(),
		Value.GetMemory(),
		Catalog,
		BuildWireLimits(Slot.Descriptor.Limits),
		OutBytes,
		OutError,
		Cost))
	{
		return false;
	}

	FSeinCanonicalDigestWriter Writer(
		TEXT("SeinARTS.CanonicalState.Leaf"), 1);
	if (!Writer.WriteGuid(Slot.DescriptorDigest)
		|| !Writer.WriteBytes(OutBytes)
		|| !Writer.Finalize(OutLeafDigest, OutError))
	{
		OutBytes.Reset();
		OutLeafDigest.Invalidate();
		return false;
	}
	return true;
}

bool FSeinCanonicalStateValueStore::CanonicalizeValue(
	const FSlot& Slot,
	const FInstancedStruct& Value,
	FInstancedStruct& OutCanonicalValue,
	TArray<uint8>& OutBytes,
	FGuid& OutLeafDigest,
	FString& OutError)
{
	OutCanonicalValue.Reset();
	if (!EncodeValue(
		Slot, Value, OutBytes, OutLeafDigest, OutError))
	{
		return false;
	}

	OutCanonicalValue.InitializeAs(Slot.Descriptor.PayloadStruct);
	FSeinWireCost Cost;
	if (!FSeinCanonicalStateCodec::DecodeWithCost(
		OutBytes,
		Slot.Descriptor.PayloadStruct,
		OutCanonicalValue.GetMutableMemory(),
		{ Slot.Descriptor.DynamicPayloadStructs,
			Slot.Descriptor.AllowedNames },
		BuildWireLimits(Slot.Descriptor.Limits),
		OutError,
		Cost))
	{
		OutCanonicalValue.Reset();
		OutBytes.Reset();
		OutLeafDigest.Invalidate();
		return false;
	}
	return true;
}

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCanonicalStateValueStore.h
 * @brief   Core-owned Blueprint-authorable deterministic state slots.
 */

#pragma once

#include "CoreMinimal.h"
#include "Serialization/SeinCanonicalStateRegistry.h"

class FReferenceCollector;

/**
 * Per-world value store for designer-authored state.
 *
 * Blueprint supplies passive deterministic values, never checkpoint
 * callbacks or UObject fields. Core validates, copies, bounds, hashes, and
 * restores those authoritative values transactionally before native
 * contributor staging begins.
 */
class SEINARTSCOREENTITY_API FSeinCanonicalStateValueStore
{
public:
	/** Hard process-independent bounds for one world's passive state values. */
	static constexpr int32 MaxSlots = 4096;
	static constexpr uint64 MaxAggregatePayloadBytes =
		64ull * 1024ull * 1024ull;

	bool RegisterSlot(
		const FSeinCanonicalStateSchemaSnapshot& NativeSchema,
		const FSeinCanonicalStateValueSlotDefinition& Definition,
		const FInstancedStruct& InitialValue,
		FString& OutError);

	bool SetValue(
		const FSeinCanonicalStateKey& Key,
		const FInstancedStruct& NewValue,
		FString& OutError);

	bool GetValue(
		const FSeinCanonicalStateKey& Key,
		FInstancedStruct& OutValue) const;

	/** Freeze the combined native/value contract identity for this match. */
	bool Seal(
		const FSeinCanonicalStateSchemaSnapshot& NativeSchema,
		FString& OutError);

	/**
	 * Freeze with additional locally proven contract frames, such as the
	 * exact canonical-state recipe registry identity. Frames affect the
	 * contract root but are never checkpoint-supplied schema.
	 */
	bool Seal(
		const FSeinCanonicalStateSchemaSnapshot& NativeSchema,
		TConstArrayView<FString> AdditionalContractFrames,
		FString& OutError);

	bool IsSealed() const { return bSealed; }
	int32 Num() const { return Slots.Num(); }
	FGuid GetContractDigest() const { return ContractDigest; }
	const FString& GetCanonicalManifest() const { return CanonicalManifest; }

	/**
	 * Build a canonically sorted immutable copy for checkpoint/digest assembly.
	 * Capture fails closed and clears OutRecords if internal resource invariants
	 * are not satisfied, so a locally produced checkpoint is always restorable.
	 */
	bool CaptureRecords(
		TArray<FSeinCanonicalStateValueRecord>& OutRecords,
		FString& OutError) const;

	/**
	 * Decode a checkpoint strictly against a locally declared, already-sealed
	 * schema. External records may supply values, never types, bounds, names,
	 * revisions, or slot topology. The destination is replaced only after
	 * every payload and digest check succeeds.
	 */
	bool TryRestoreRecords(
		const FSeinCanonicalStateValueStore& ExpectedSchema,
		TConstArrayView<FSeinCanonicalStateValueRecord> Records,
		const FGuid& ExpectedContractDigest,
		FString& OutError);

	void AddReferencedObjects(FReferenceCollector& Collector);
	void Reset();

#if WITH_DEV_AUTOMATION_TESTS
	/** White-box seam for forcing GC while checkpoint records are staged. */
	static void SetRestoreStagingTestHook(TFunction<void()> Hook);
#endif

private:
	friend class USeinWorldSubsystem;

	struct FSlot
	{
		FSeinCanonicalStateDescriptor Descriptor;
		FString CanonicalDescriptor;
		FGuid DescriptorDigest;
		TArray<FInstancedStruct> DynamicSchemaValues;
		FInstancedStruct Value;
		TArray<uint8> PayloadBytes;
		FGuid LeafDigest;
	};

	static bool EncodeValue(
		const FSlot& Slot,
		const FInstancedStruct& Value,
		TArray<uint8>& OutBytes,
		FGuid& OutLeafDigest,
		FString& OutError);

	static bool CanonicalizeValue(
		const FSlot& Slot,
		const FInstancedStruct& Value,
		FInstancedStruct& OutCanonicalValue,
		TArray<uint8>& OutBytes,
		FGuid& OutLeafDigest,
		FString& OutError);

	bool ValidateTrackedResourceBounds(FString& OutError) const;
	bool ValidateResourceBounds(FString& OutError) const;

	static bool TryApplyPayloadChange(
		uint64 CurrentBytes,
		int32 RemovedBytes,
		int32 AddedBytes,
		uint64& OutBytes);

	TMap<FSeinCanonicalStateKey, FSlot> Slots;
	FString CanonicalManifest;
	FGuid ContractDigest;
	uint64 AggregatePayloadBytes = 0;
	bool bSealed = false;
};

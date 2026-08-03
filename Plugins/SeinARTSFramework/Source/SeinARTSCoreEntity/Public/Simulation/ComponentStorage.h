/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    ComponentStorage.h
 * @brief   Slot-indexed component storage keyed by FSeinEntityHandle.
 *
 * Flat arrays parallel to the entity pool: O(1) add/remove/get by slot index,
 * cache-friendly iteration over active slots via a TBitArray.
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "Core/SeinEntityHandle.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

/**
 * Abstract interface for entity-handle-keyed component storage.
 */
class SEINARTSCOREENTITY_API ISeinComponentStorage
{
public:
	virtual ~ISeinComponentStorage() = default;

	virtual void AddComponent(FSeinEntityHandle Handle, const void* ComponentData) = 0;
	virtual void RemoveComponent(FSeinEntityHandle Handle) = 0;
	virtual bool HasComponent(FSeinEntityHandle Handle) const = 0;
	virtual void* GetComponentRaw(FSeinEntityHandle Handle) = 0;
	virtual const void* GetComponentRaw(FSeinEntityHandle Handle) const = 0;
	/** Mutable access without eagerly advancing the canonical-state revision.
	 *  The caller must compare its result and call CommitDeferredMutation exactly
	 *  once, on the serial spine, when deterministic state actually changed. This
	 *  is intended for disjoint-write parallel kernels; ordinary callers should
	 *  use GetComponentRaw, whose conservative touch remains fail-safe. */
	virtual void* GetComponentRawForDeferredMutation(
		FSeinEntityHandle Handle) = 0;
	/** Publish an actual write obtained through
	 *  GetComponentRawForDeferredMutation. Not thread-safe: canonical merge/apply
	 *  code must invoke this serially in stable handle order. */
	virtual void CommitDeferredMutation(FSeinEntityHandle Handle) = 0;
	virtual uint32 ComputeHash() const = 0;
	virtual int32 GetComponentCount() const = 0;
	virtual void Clear() = 0;
	/** Process-local mutation evidence for incremental canonical digests. The
	 *  revision is never serialized or included as state; it only tells the
	 *  cache whether the exact value for this handle must be projected again. */
	virtual uint64 GetMutationRevision(FSeinEntityHandle Handle) const = 0;
	/** Latest revision assigned to any slot; zero means never touched. */
	virtual uint64 GetLatestMutationRevision() const = 0;
	/** Changes whenever the occupied-handle set or storage capacity changes. */
	virtual uint64 GetTopologyRevision() const = 0;

	/**
	 * Sparse live-slot iteration: invoke Visitor for every alive slot exactly
	 * once, in SLOT-INDEX ASCENDING order, handing it the exact generational
	 * handle stored with that slot and a mutable pointer to its raw payload.
	 * This matches
	 * FSeinEntityPool::ForEachEntity's ascending-slot order, so a system that
	 * iterates a single storage instead of the full pool visits its entities
	 * in the identical deterministic order.
	 *
	 * The entity pool remains the authority for whether the yielded handle is
	 * alive. Consumers must validate that exact handle before mutating entity
	 * state; they must never replace its generation with the pool's current
	 * generation for the slot.
	 *
	 * Contract: the Visitor must NOT add or remove components of THIS storage
	 * during iteration (it walks the live bit-array in place). Deferring
	 * destroys / instance-effect removals is fine — those mutate the pool's
	 * pending list or a payload's inner array, not this storage's slot set.
	 */
	virtual void ForEachLiveComponent(
		TFunctionRef<void(FSeinEntityHandle /*Handle*/, void* /*RawComponent*/)> Visitor) = 0;

	/**
	 * Read-only sparse live-slot iteration with the same ascending-slot and
	 * generational-handle guarantees as the mutable overload.
	 */
	virtual void ForEachLiveComponent(
		TFunctionRef<void(
			FSeinEntityHandle /*Handle*/,
			const void* /*RawComponent*/)> Visitor) const = 0;

	/** Alias for RemoveComponent — clearer intent when cleaning up a destroyed entity. */
	virtual void RemoveAllForEntity(FSeinEntityHandle Handle) = 0;

	/** Grow internal arrays to accommodate a larger entity pool. */
	virtual void Grow(int32 NewCapacity) = 0;

	/**
	 * Walk every alive slot's payload and report its reflected UObject references
	 * to the GC. Without this, any TObjectPtr / UPROPERTY-tagged object ref stored
	 * inside a component struct is invisible to the collector (the backing buffer
	 * is a raw byte array) and the referenced UObject gets collected mid-play,
	 * leaving dangling pointers in ActiveAbility / AbilityInstances / Resolver / etc.
	 *
	 * Owner is the UObject the subsystem passes through for diagnostic attribution.
	 */
	virtual void CollectReferences(FReferenceCollector& Collector, UObject* Owner) = 0;

	/**
	 * Snapshot serialize (Phase 4 architecture). Writes (or reads) every alive
	 * slot's payload through the archive. Wire format:
	 *   int32 EntryCount
	 *   for each:
	 *     int32 SlotIndex
	 *     int32 Generation
	 *     UScriptStruct::SerializeBin(payload, archive)
	 *
	 * On restore, the storage is cleared first; entries then re-populated. The
	 * caller (USeinWorldSubsystem) is responsible for resizing the storage to
	 * match the restored entity pool capacity BEFORE calling this with a load
	 * archive. The returned int is the number of entries written/read.
	 */
	virtual int32 SerializeFromArchive(FArchive& Ar) = 0;
};

/**
 * Component storage for reflection-based access, used by the runtime.
 *
 * Component types are discovered at entity spawn by walking the Blueprint CDO's
 * entity bridge (USeinEntityComponent) ComponentData and injecting each
 * FInstancedStruct payload struct directly.
 * Payloads are stored as raw bytes, with construction/copy/destroy dispatched
 * through UScriptStruct so TArrays, UPROPERTY references, etc. are handled
 * correctly.
 */
class SEINARTSCOREENTITY_API FSeinGenericComponentStorage : public ISeinComponentStorage
{
public:
	FSeinGenericComponentStorage(UScriptStruct* InStructType, int32 InitialCapacity = 0)
		: StructType(InStructType)
		, StructSize(InStructType ? InStructType->GetStructureSize() : 0)
		, ComponentCount(0)
	{
		check(StructType && StructSize > 0);
		if (InitialCapacity > 0 && InitialCapacity < MAX_int32
			&& (static_cast<int64>(InitialCapacity) + 1) * StructSize <= MAX_int32)
		{
			const int32 TotalSlots = InitialCapacity + 1; // +1 for reserved slot 0
			Data.SetNumZeroed(TotalSlots * StructSize);
			HasComponentBits.Init(false, TotalSlots);
			StoredGenerations.Init(0, TotalSlots);
			SlotMutationRevisions.Init(0, TotalSlots);
			SlotCapacity = TotalSlots;

			// Initialize all slots with default-constructed structs
			for (int32 i = 0; i < TotalSlots; ++i)
			{
				StructType->InitializeStruct(GetSlotPtr(i));
			}
		}
	}

	virtual ~FSeinGenericComponentStorage() override
	{
		// Destroy all initialized structs
		for (int32 i = 0; i < SlotCapacity; ++i)
		{
			StructType->DestroyStruct(GetSlotPtr(i));
		}
	}

	virtual void Grow(int32 NewCapacity) override
	{
		if (NewCapacity < 0 || NewCapacity == MAX_int32
			|| (static_cast<int64>(NewCapacity) + 1) * StructSize > MAX_int32)
		{
			return;
		}
		const int32 NewTotalSlots = NewCapacity + 1;
		if (NewTotalSlots <= SlotCapacity)
		{
			return;
		}

		const int32 OldCapacity = SlotCapacity;
		Data.SetNumZeroed(NewTotalSlots * StructSize);

		const int32 BitsToAdd = NewTotalSlots - HasComponentBits.Num();
		if (BitsToAdd > 0)
		{
			HasComponentBits.Add(false, BitsToAdd);
		}
		StoredGenerations.SetNumZeroed(NewTotalSlots);
		SlotMutationRevisions.SetNumZeroed(NewTotalSlots);

		SlotCapacity = NewTotalSlots;

		// Initialize newly allocated slots
		for (int32 i = OldCapacity; i < NewTotalSlots; ++i)
		{
			StructType->InitializeStruct(GetSlotPtr(i));
		}
		BumpTopologyRevision();
	}

	virtual void AddComponent(FSeinEntityHandle Handle, const void* ComponentData) override
	{
		if (!Handle.IsValid())
		{
			return;
		}

		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		if (!EnsureSlotCapacity(SlotIndex))
		{
			return;
		}

		const bool bHadComponent = HasComponentBits[SlotIndex];
		const bool bRecycledSlot = bHadComponent
			&& StoredGenerations[SlotIndex] != Handle.Generation;
		if (!bHadComponent)
		{
			ComponentCount++;
		}
		else if (bRecycledSlot)
		{
			ResetSlot(SlotIndex);
		}

		uint8* SlotPtr = GetSlotPtr(SlotIndex);
		if (ComponentData)
		{
			StructType->CopyScriptStruct(SlotPtr, ComponentData);
		}
		else
		{
			if (!bRecycledSlot)
			{
				ResetSlot(SlotIndex);
			}
		}

		HasComponentBits[SlotIndex] = true;
		StoredGenerations[SlotIndex] = Handle.Generation;
		TouchSlot(SlotIndex);
		if (!bHadComponent || bRecycledSlot)
		{
			BumpTopologyRevision();
		}
	}

	virtual void RemoveComponent(FSeinEntityHandle Handle) override
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		if (IsStoredHandle(Handle))
		{
			HasComponentBits[SlotIndex] = false;
			StoredGenerations[SlotIndex] = 0;
			ResetSlot(SlotIndex);
			ComponentCount--;
			TouchSlot(SlotIndex);
			BumpTopologyRevision();
		}
	}

	virtual void RemoveAllForEntity(FSeinEntityHandle Handle) override
	{
		RemoveComponent(Handle);
	}

	virtual bool HasComponent(FSeinEntityHandle Handle) const override
	{
		return IsStoredHandle(Handle);
	}

	virtual void* GetComponentRaw(FSeinEntityHandle Handle) override
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		if (IsStoredHandle(Handle))
		{
			TouchSlot(SlotIndex);
			return GetSlotPtr(SlotIndex);
		}
		return nullptr;
	}

	virtual const void* GetComponentRaw(FSeinEntityHandle Handle) const override
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		if (IsStoredHandle(Handle))
		{
			return GetSlotPtr(SlotIndex);
		}
		return nullptr;
	}

	virtual void* GetComponentRawForDeferredMutation(
		FSeinEntityHandle Handle) override
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		return IsStoredHandle(Handle) ? GetSlotPtr(SlotIndex) : nullptr;
	}

	virtual void CommitDeferredMutation(
		FSeinEntityHandle Handle) override
	{
		if (IsStoredHandle(Handle))
		{
			TouchSlot(static_cast<int32>(Handle.Index));
		}
	}

	virtual uint32 ComputeHash() const override
	{
		uint32 Hash = 0;
		if (!StructType) return Hash;

		// Walk reflected UPROPERTY fields per slot instead of raw memory so this
		// legacy local fingerprint ignores padding and scratch bytes. This path
		// is intentionally not the cross-process determinism contract:
		// GetValueTypeHash is process-local for some reflected types and the
		// fallback below omits unsupported values. Canonical world-root capture
		// uses its own exact, fail-closed reflected encoder.
		// Helper: should this property be skipped entirely (non-deterministic
		// value across processes, or not state-relevant)?
		auto IsNonDeterministicOrSkip = [](const FProperty* P) -> bool
		{
			if (!P) return true;
			if (P->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly | CPF_Deprecated))
			{
				return true;
			}
			// UObject references: pointer addresses differ between PIE
			// windows / shipped processes. `GetValueTypeHash` on object
			// properties returns the pointer hash → guaranteed cross-
			// process desync. Component data shouldn't carry UObject
			// refs at all (violates the components-are-pure-data rule), but until those are
			// migrated to FSeinEntityHandle / class-by-FName lookups,
			// skip them here so the hash remains stable.
			if (P->IsA<FObjectPropertyBase>() || P->IsA<FInterfaceProperty>())
			{
				return true;
			}
			return false;
		};

		for (TConstSetBitIterator<> It(HasComponentBits); It; ++It)
		{
			const int32 SlotIndex = It.GetIndex();
			Hash = HashCombine(Hash, GetTypeHash(static_cast<uint32>(SlotIndex)));
			Hash = HashCombine(Hash, GetTypeHash(StoredGenerations[SlotIndex]));

			const uint8* SlotPtr = GetSlotPtr(SlotIndex);
			for (TFieldIterator<FProperty> PropIt(StructType); PropIt; ++PropIt)
			{
				FProperty* Prop = *PropIt;
				if (IsNonDeterministicOrSkip(Prop)) continue;

				// Mix in the property name so reordering / renaming changes
				// the hash; keeps replay file bumps detectable.
				Hash = HashCombine(Hash, GetTypeHash(Prop->GetFName()));

				const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(SlotPtr);

				// Arrays: handle explicitly so we can detect arrays-of-objects
				// (skip element values, hash count only) vs arrays-of-hashable
				// (hash count + each element).
				if (FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
				{
					FScriptArrayHelper Helper(ArrProp, ValuePtr);
					const int32 Num = Helper.Num();
					Hash = HashCombine(Hash, GetTypeHash(Num));
					const FProperty* Inner = ArrProp->Inner;
					if (Inner && !IsNonDeterministicOrSkip(Inner) && (Inner->PropertyFlags & CPF_HasGetValueTypeHash))
					{
						for (int32 i = 0; i < Num; ++i)
						{
							Hash = HashCombine(Hash, Inner->GetValueTypeHash(Helper.GetRawPtr(i)));
						}
					}
					continue;
				}

				// Maps: order-INDEPENDENT content hash. TMap iteration order is
				// not guaranteed identical across processes for the same logical
				// contents, so combine per-entry hashes COMMUTATIVELY (sum) before
				// mixing in. Only when BOTH key and value are hashable; otherwise
				// hash count only (CollectUnhashedStateFields flags it).
				if (const FMapProperty* MapProp = CastField<FMapProperty>(Prop))
				{
					FScriptMapHelper Helper(MapProp, ValuePtr);
					Hash = HashCombine(Hash, GetTypeHash(Helper.Num()));
					const FProperty* KeyP = MapProp->KeyProp;
					const FProperty* ValP = MapProp->ValueProp;
					if (KeyP && ValP
						&& !IsNonDeterministicOrSkip(KeyP) && !IsNonDeterministicOrSkip(ValP)
						&& (KeyP->PropertyFlags & CPF_HasGetValueTypeHash)
						&& (ValP->PropertyFlags & CPF_HasGetValueTypeHash))
					{
						uint32 Acc = 0;
						const int32 MaxIndex = Helper.GetMaxIndex();
						for (int32 i = 0; i < MaxIndex; ++i)
						{
							if (!Helper.IsValidIndex(i)) continue;
							const uint32 KeyH = KeyP->GetValueTypeHash(Helper.GetKeyPtr(i));
							const uint32 ValH = ValP->GetValueTypeHash(Helper.GetValuePtr(i));
							Acc += HashCombine(KeyH, ValH);   // per-entry order fixed (key,value); cross-entry commutative
						}
						Hash = HashCombine(Hash, Acc);
					}
					continue;
				}

				// Sets: same order-independent commutative-sum approach as maps.
				if (const FSetProperty* SetProp = CastField<FSetProperty>(Prop))
				{
					FScriptSetHelper Helper(SetProp, ValuePtr);
					Hash = HashCombine(Hash, GetTypeHash(Helper.Num()));
					const FProperty* ElemP = SetProp->ElementProp;
					if (ElemP && !IsNonDeterministicOrSkip(ElemP)
						&& (ElemP->PropertyFlags & CPF_HasGetValueTypeHash))
					{
						uint32 Acc = 0;
						const int32 MaxIndex = Helper.GetMaxIndex();
						for (int32 i = 0; i < MaxIndex; ++i)
						{
							if (!Helper.IsValidIndex(i)) continue;
							Acc += ElemP->GetValueTypeHash(Helper.GetElementPtr(i));
						}
						Hash = HashCombine(Hash, Acc);
					}
					continue;
				}

				if (Prop->PropertyFlags & CPF_HasGetValueTypeHash)
				{
					Hash = HashCombine(Hash, Prop->GetValueTypeHash(ValuePtr));
				}
				// else: no GetTypeHash and not a handled container — e.g. a nested
				// struct without WithGetTypeHash, or a map/set/array whose key/value/
				// element is itself non-hashable (count-only above). Value is dropped;
				// only the property name is mixed. CollectUnhashedStateFields flags
				// these. Add per-type branches above if a real case surfaces.
			}
		}
		return Hash;
	}

	/** Collect the names of UPROPERTY fields whose VALUE is NOT mixed into
	 *  ComputeHash() — structs without WithGetTypeHash, plus TMap/TSet/arrays
	 *  whose key/value/element is itself non-hashable. Such fields can hold
	 *  per-instance state that silently won't contribute to desync detection.
	 *  Mirrors ComputeHash's skip/hashable logic so the two can't drift. Dev aid. */
	static void CollectUnhashedStateFields(const UScriptStruct* InStruct, TArray<FString>& OutFieldNames)
	{
		if (!InStruct) return;
		auto IsNonDeterministicOrSkip = [](const FProperty* P) -> bool
		{
			if (!P) return true;
			if (P->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly | CPF_Deprecated)) return true;
			if (P->IsA<FObjectPropertyBase>() || P->IsA<FInterfaceProperty>()) return true;
			return false;
		};
		for (TFieldIterator<FProperty> It(InStruct); It; ++It)
		{
			const FProperty* Prop = *It;
			if (IsNonDeterministicOrSkip(Prop)) continue;
			if (const FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
			{
				const FProperty* Inner = ArrProp->Inner;
				if (Inner && !IsNonDeterministicOrSkip(Inner) && !(Inner->PropertyFlags & CPF_HasGetValueTypeHash))
				{
					OutFieldNames.Add(Prop->GetName() + TEXT(" (array elements)"));
				}
				continue;
			}
			if (const FMapProperty* MapProp = CastField<FMapProperty>(Prop))
			{
				const FProperty* KeyP = MapProp->KeyProp;
				const FProperty* ValP = MapProp->ValueProp;
				const bool bHashable = KeyP && ValP
					&& !IsNonDeterministicOrSkip(KeyP) && !IsNonDeterministicOrSkip(ValP)
					&& (KeyP->PropertyFlags & CPF_HasGetValueTypeHash)
					&& (ValP->PropertyFlags & CPF_HasGetValueTypeHash);
				if (!bHashable) OutFieldNames.Add(Prop->GetName() + TEXT(" (map key/value)"));
				continue;
			}
			if (const FSetProperty* SetProp = CastField<FSetProperty>(Prop))
			{
				const FProperty* ElemP = SetProp->ElementProp;
				const bool bHashable = ElemP && !IsNonDeterministicOrSkip(ElemP)
					&& (ElemP->PropertyFlags & CPF_HasGetValueTypeHash);
				if (!bHashable) OutFieldNames.Add(Prop->GetName() + TEXT(" (set elements)"));
				continue;
			}
			if (!(Prop->PropertyFlags & CPF_HasGetValueTypeHash))
			{
				OutFieldNames.Add(Prop->GetName());
			}
		}
	}

	virtual int32 GetComponentCount() const override
	{
		return ComponentCount;
	}

	virtual uint64 GetMutationRevision(
		FSeinEntityHandle Handle) const override
	{
		return IsStoredHandle(Handle)
			&& SlotMutationRevisions.IsValidIndex(Handle.Index)
				? SlotMutationRevisions[Handle.Index]
				: 0;
	}

	virtual uint64 GetTopologyRevision() const override
	{
		return TopologyRevision;
	}

	virtual uint64 GetLatestMutationRevision() const override
	{
		return MutationRevisionCounter;
	}

	virtual void Clear() override
	{
		for (TConstSetBitIterator<> It(HasComponentBits); It; ++It)
		{
			ResetSlot(It.GetIndex());
		}
		HasComponentBits.Init(false, HasComponentBits.Num());
		StoredGenerations.Init(0, StoredGenerations.Num());
		ComponentCount = 0;
		BumpTopologyRevision();
	}

	virtual void ForEachLiveComponent(
		TFunctionRef<void(FSeinEntityHandle /*Handle*/, void* /*RawComponent*/)> Visitor) override
	{
		// TConstSetBitIterator yields set-bit indices in ascending order, which
		// matches FSeinEntityPool::ForEachEntity's slot order — see the
		// interface docstring for why that determinism guarantee matters.
		for (TConstSetBitIterator<> It(HasComponentBits); It; ++It)
		{
			const int32 SlotIndex = It.GetIndex();
			TouchSlot(SlotIndex);
			Visitor(
				FSeinEntityHandle(SlotIndex, StoredGenerations[SlotIndex]),
				GetSlotPtr(SlotIndex));
		}
	}

	virtual void ForEachLiveComponent(
		TFunctionRef<void(
			FSeinEntityHandle /*Handle*/,
			const void* /*RawComponent*/)> Visitor) const override
	{
		for (TConstSetBitIterator<> It(HasComponentBits); It; ++It)
		{
			const int32 SlotIndex = It.GetIndex();
			Visitor(
				FSeinEntityHandle(
					SlotIndex,
					StoredGenerations[SlotIndex]),
				GetSlotPtr(SlotIndex));
		}
	}

	virtual void CollectReferences(FReferenceCollector& Collector, UObject* Owner) override
	{
		if (!StructType) return;
		// The registry map is intentionally keyed by raw UScriptStruct* for its
		// public lookup API. Keep the actual type as a collector-visible
		// TObjectPtr here so a UUserDefinedStruct cannot be collected while its
		// storage remains live.
		Collector.AddReferencedObject(StructType, Owner);
		for (TConstSetBitIterator<> It(HasComponentBits); It; ++It)
		{
			Collector.AddPropertyReferencesWithStructARO(
				StructType.Get(), GetSlotPtr(It.GetIndex()), Owner);
		}
	}

	virtual int32 SerializeFromArchive(FArchive& Ar) override
	{
		if (!StructType) { int32 Zero = 0; Ar << Zero; return 0; }

		if (Ar.IsSaving())
		{
			// Count alive slots first; write count, then per-entry write
			// (slot, generation, payload).
			int32 EntryCount = ComponentCount;
			Ar << EntryCount;
			for (TConstSetBitIterator<> It(HasComponentBits); It; ++It)
			{
				int32 Slot = It.GetIndex();
				int32 Generation = StoredGenerations[Slot];
				if (Generation <= 0)
				{
					Ar.SetError();
					return 0;
				}
				Ar << Slot;
				Ar << Generation;
				StructType->SerializeBin(Ar, GetSlotPtr(Slot));
			}
			return EntryCount;
		}
		else
		{
			// Loading: caller has already resized capacity (via Grow). Clear
			// existing slots, then read EntryCount entries.
			Clear();
			int32 EntryCount = 0;
			Ar << EntryCount;
			if (Ar.IsError() || EntryCount < 0
				|| EntryCount > FMath::Max(0, SlotCapacity - 1))
			{
				Ar.SetError();
				return 0;
			}
			for (int32 i = 0; i < EntryCount; ++i)
			{
				int32 Slot = 0;
				int32 Generation = 0;
				Ar << Slot;
				Ar << Generation;
				const FSeinEntityHandle Handle(Slot, Generation);
				if (Ar.IsError() || !Handle.IsValid()
					|| !HasComponentBits.IsValidIndex(Slot)
					|| HasComponentBits[Slot])
				{
					Ar.SetError();
					return i;
				}
				HasComponentBits[Slot] = true;
				StoredGenerations[Slot] = Generation;
				++ComponentCount;
				StructType->SerializeBin(Ar, GetSlotPtr(Slot));
				if (Ar.IsError())
				{
					return i;
				}
				TouchSlot(Slot);
			}
			BumpTopologyRevision();
			return EntryCount;
		}
	}

	UScriptStruct* GetStructType() const { return StructType.Get(); }

private:
	bool IsStoredHandle(FSeinEntityHandle Handle) const
	{
		return Handle.IsValid()
			&& HasComponentBits.IsValidIndex(Handle.Index)
			&& HasComponentBits[Handle.Index]
			&& StoredGenerations[Handle.Index] == Handle.Generation;
	}

	void ResetSlot(int32 SlotIndex)
	{
		// ClearScriptStruct destroys and reconstructs the payload. Calling
		// InitializeStruct again would double-construct resource-owning fields.
		StructType->ClearScriptStruct(GetSlotPtr(SlotIndex));
	}

	void TouchSlot(int32 SlotIndex)
	{
		if (!SlotMutationRevisions.IsValidIndex(SlotIndex))
		{
			return;
		}
		++MutationRevisionCounter;
		if (MutationRevisionCounter == 0)
		{
			++MutationRevisionCounter;
		}
		SlotMutationRevisions[SlotIndex] = MutationRevisionCounter;
	}

	void BumpTopologyRevision()
	{
		++TopologyRevision;
		if (TopologyRevision == 0)
		{
			++TopologyRevision;
		}
	}

	bool EnsureSlotCapacity(int32 SlotIndex)
	{
		if (SlotIndex <= 0 || SlotIndex == MAX_int32
			|| (static_cast<int64>(SlotIndex) + 1) * StructSize > MAX_int32)
		{
			return false;
		}
		const int32 RequiredSize = SlotIndex + 1;
		if (RequiredSize > SlotCapacity)
		{
			Grow(RequiredSize - 1);
		}
		return HasComponentBits.IsValidIndex(SlotIndex);
	}

	uint8* GetSlotPtr(int32 SlotIndex)
	{
		return Data.GetData() + (SlotIndex * StructSize);
	}

	const uint8* GetSlotPtr(int32 SlotIndex) const
	{
		return Data.GetData() + (SlotIndex * StructSize);
	}

	TObjectPtr<UScriptStruct> StructType;
	int32 StructSize;
	TArray<uint8> Data;
	TBitArray<> HasComponentBits;
	TArray<int32> StoredGenerations;
	TArray<uint64> SlotMutationRevisions;
	uint64 MutationRevisionCounter = 0;
	uint64 TopologyRevision = 1;
	int32 SlotCapacity = 0;
	int32 ComponentCount;
};

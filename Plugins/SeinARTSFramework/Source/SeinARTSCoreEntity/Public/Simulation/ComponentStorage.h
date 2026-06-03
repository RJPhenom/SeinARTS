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
	virtual uint32 ComputeHash() const = 0;
	virtual int32 GetComponentCount() const = 0;
	virtual void Clear() = 0;

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
 * Component types are discovered at entity spawn by walking the Blueprint
 * CDO's USeinActorComponents and injecting the struct each Resolve() returns.
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
		if (InitialCapacity > 0)
		{
			const int32 TotalSlots = InitialCapacity + 1; // +1 for reserved slot 0
			Data.SetNumZeroed(TotalSlots * StructSize);
			HasComponentBits.Init(false, TotalSlots);
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

		SlotCapacity = NewTotalSlots;

		// Initialize newly allocated slots
		for (int32 i = OldCapacity; i < NewTotalSlots; ++i)
		{
			StructType->InitializeStruct(GetSlotPtr(i));
		}
	}

	virtual void AddComponent(FSeinEntityHandle Handle, const void* ComponentData) override
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		EnsureSlotCapacity(SlotIndex);

		if (!HasComponentBits[SlotIndex])
		{
			ComponentCount++;
		}

		uint8* SlotPtr = GetSlotPtr(SlotIndex);
		if (ComponentData)
		{
			StructType->CopyScriptStruct(SlotPtr, ComponentData);
		}
		else
		{
			StructType->ClearScriptStruct(SlotPtr);
			StructType->InitializeStruct(SlotPtr);
		}

		HasComponentBits[SlotIndex] = true;
	}

	virtual void RemoveComponent(FSeinEntityHandle Handle) override
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		if (SlotIndex < SlotCapacity && HasComponentBits[SlotIndex])
		{
			HasComponentBits[SlotIndex] = false;
			uint8* SlotPtr = GetSlotPtr(SlotIndex);
			StructType->ClearScriptStruct(SlotPtr);
			StructType->InitializeStruct(SlotPtr);
			ComponentCount--;
		}
	}

	virtual void RemoveAllForEntity(FSeinEntityHandle Handle) override
	{
		RemoveComponent(Handle);
	}

	virtual bool HasComponent(FSeinEntityHandle Handle) const override
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		return SlotIndex < SlotCapacity && HasComponentBits[SlotIndex];
	}

	virtual void* GetComponentRaw(FSeinEntityHandle Handle) override
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		if (SlotIndex < SlotCapacity && HasComponentBits[SlotIndex])
		{
			return GetSlotPtr(SlotIndex);
		}
		return nullptr;
	}

	virtual const void* GetComponentRaw(FSeinEntityHandle Handle) const override
	{
		const int32 SlotIndex = static_cast<int32>(Handle.Index);
		if (SlotIndex < SlotCapacity && HasComponentBits[SlotIndex])
		{
			return GetSlotPtr(SlotIndex);
		}
		return nullptr;
	}

	virtual uint32 ComputeHash() const override
	{
		uint32 Hash = 0;
		if (!StructType) return Hash;

		// Walk reflected UPROPERTY fields per slot. Using reflection (not raw
		// memory iteration) is critical for cross-process determinism: padding
		// bytes between fields, transient/non-UPROPERTY internal members, and
		// uninitialized scratch memory ALL differ across machines even when
		// the deterministic state is identical. Reflection-driven hashing
		// covers exactly the fields the designer marked as state, and uses
		// each property's own GetValueTypeHash which is content-based
		// (string for FString, raw int64 for FFixedPoint, etc.).
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

				if (Prop->PropertyFlags & CPF_HasGetValueTypeHash)
				{
					Hash = HashCombine(Hash, Prop->GetValueTypeHash(ValuePtr));
				}
				// else: the property type has no GetTypeHash (some nested
				// struct without WithGetTypeHash, or TMap/TSet). We hash
				// only the property name — value is dropped. Stable across
				// processes (consistent zero) but may miss state drift in
				// those fields. Add per-type branches above if a real case
				// surfaces.
			}
		}
		return Hash;
	}

	virtual int32 GetComponentCount() const override
	{
		return ComponentCount;
	}

	virtual void Clear() override
	{
		for (TConstSetBitIterator<> It(HasComponentBits); It; ++It)
		{
			uint8* SlotPtr = GetSlotPtr(It.GetIndex());
			StructType->ClearScriptStruct(SlotPtr);
			StructType->InitializeStruct(SlotPtr);
		}
		HasComponentBits.Init(false, HasComponentBits.Num());
		ComponentCount = 0;
	}

	virtual void CollectReferences(FReferenceCollector& Collector, UObject* Owner) override
	{
		if (!StructType) return;
		for (TConstSetBitIterator<> It(HasComponentBits); It; ++It)
		{
			Collector.AddPropertyReferencesWithStructARO(StructType, GetSlotPtr(It.GetIndex()), Owner);
		}
	}

	virtual int32 SerializeFromArchive(FArchive& Ar) override
	{
		if (!StructType) { int32 Zero = 0; Ar << Zero; return 0; }

		if (Ar.IsSaving())
		{
			// Count alive slots first; write count, then per-entry write
			// (slot, payload).
			int32 EntryCount = ComponentCount;
			Ar << EntryCount;
			for (TConstSetBitIterator<> It(HasComponentBits); It; ++It)
			{
				int32 Slot = It.GetIndex();
				Ar << Slot;
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
			for (int32 i = 0; i < EntryCount; ++i)
			{
				int32 Slot = 0;
				Ar << Slot;
				EnsureSlotCapacity(Slot);
				if (!HasComponentBits[Slot])
				{
					HasComponentBits[Slot] = true;
					++ComponentCount;
				}
				StructType->SerializeBin(Ar, GetSlotPtr(Slot));
			}
			return EntryCount;
		}
	}

	UScriptStruct* GetStructType() const { return StructType; }

private:
	void EnsureSlotCapacity(int32 SlotIndex)
	{
		const int32 RequiredSize = SlotIndex + 1;
		if (RequiredSize > SlotCapacity)
		{
			Grow(RequiredSize - 1);
		}
	}

	uint8* GetSlotPtr(int32 SlotIndex)
	{
		return Data.GetData() + (SlotIndex * StructSize);
	}

	const uint8* GetSlotPtr(int32 SlotIndex) const
	{
		return Data.GetData() + (SlotIndex * StructSize);
	}

	UScriptStruct* StructType;
	int32 StructSize;
	TArray<uint8> Data;
	TBitArray<> HasComponentBits;
	int32 SlotCapacity = 0;
	int32 ComponentCount;
};

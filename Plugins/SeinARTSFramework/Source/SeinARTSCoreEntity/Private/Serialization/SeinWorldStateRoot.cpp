/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWorldStateRoot.cpp
 * @brief   Exact fallible live-world root capture.
 */

#include "Simulation/SeinWorldSubsystem.h"

#include "Abilities/SeinAbility.h"
#include "Abilities/SeinLatentAction.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Async/TaskGraphInterfaces.h"
#include "Actor/SeinActor.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "Core/SeinParallel.h"
#include "Core/SeinSimContext.h"
#include "Data/SeinFaction.h"
#include "Data/SeinWorldSnapshot.h"
#include "Effects/SeinEffect.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Serialization/SeinCanonicalDigestTree.h"
#include "Serialization/SeinCanonicalReflectedStateDigest.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "Serialization/SeinCanonicalStateRoot.h"
#include "Serialization/SeinLatentActionCodecRegistry.h"
#include "Serialization/SeinPoolObjectCodecRegistry.h"
#include "SeinARTSCoreEntityLog.h"

/** Process-local acceleration state. None of these revisions or tree nodes is
 * authoritative; a full rebuild from live canonical values must produce the
 * same CoreAcceleratorDigest. */
struct FSeinWorldStateRootCache
{
	struct FEntityPoolCache
	{
		uint64 TopologyRevision = 0;
		uint64 LatestMutationRevision = 0;
		TArray<uint64> SlotRevisions;
		FSeinCanonicalDigestTree Tree;
		FGuid SectionDigest;
	};

	struct FComponentCache
	{
		UScriptStruct* Type = nullptr;
		FGuid SchemaDigest;
		uint64 TopologyRevision = 0;
		uint64 LatestMutationRevision = 0;
		int32 ComponentCount = 0;
		TArray<uint64> SlotRevisions;
		FSeinCanonicalDigestTree Tree;
		FGuid SectionDigest;
	};

	struct FObjectPoolCache
	{
		uint64 TopologyRevision = 0;
		uint64 LatestMutationRevision = 0;
		TArray<uint64> SlotRevisions;
		FSeinCanonicalDigestTree Tree;
		FGuid SectionDigest;
	};

	struct FLatentActionCache
	{
		struct FEntry
		{
			uint64 Revision = 0;
			FSeinSnapshotLatentActionRecord Record;
		};
		uint64 TopologyRevision = 0;
		uint64 LatestMutationRevision = 0;
		int64 NextActionID = 0;
		int64 NextAbilityActivationID = 0;
		TMap<int64, FEntry> Entries;
		TArray<FSeinSnapshotLatentActionRecord> OrderedRecords;
		FGuid SequenceDigest;
	};

	FEntityPoolCache EntityPool;
	TMap<FString, FComponentCache> Components;
	FObjectPoolCache AbilityPool;
	FObjectPoolCache ResolverPool;
	FLatentActionCache LatentActions;
	FGuid CoreAcceleratorDigest;
	uint64 AuxiliaryMutationRevision = 0;
	FGuid AuxiliaryDigest;
	FGuid SealedRoutineRoot;
	int32 SealedRoutineTick = INDEX_NONE;
	FString SealedRoutineError;
	TMap<const UScriptStruct*, FGuid> RoutineSchemaDigests;
	int64 ComponentValueDigestsComputed = 0;
	int64 PoolObjectsCaptured = 0;
	int64 EntitySlotsProjected = 0;
	int64 LatentActionsCaptured = 0;
};

namespace
{
	constexpr uint32 CoreAuthoritativeSchemaVersion = 8;
	constexpr uint32 CoreContinuationSchemaVersion = 2;
	constexpr uint32 RoutineCoreAuthoritativeSchemaVersion = 3;
	constexpr uint32 RoutineCoreContinuationSchemaVersion = 1;
	constexpr uint32 RoutineAuxiliarySchemaVersion = 2;

	TAutoConsoleVariable<int32> CVarSeinStateRootProfile(
		TEXT("Sein.Sim.StateRoot.Profile"),
		0,
		TEXT("Canonical world-root profiling. 1 logs explicit exact captures; 2 also logs every maintained multiplayer root."),
		ECVF_Default);

	bool Fail(FString& OutError, const FString& Message)
	{
		OutError = Message;
		return false;
	}

	bool WriteHandle(
		FSeinCanonicalDigestWriter& Writer,
		const FSeinEntityHandle Handle)
	{
		return Writer.WriteInt32(Handle.Index)
			&& Writer.WriteInt32(Handle.Generation);
	}

	bool WriteFixedVector(
		FSeinCanonicalDigestWriter& Writer,
		const FFixedVector& Value)
	{
		return Writer.WriteInt64(Value.X.Value)
			&& Writer.WriteInt64(Value.Y.Value)
			&& Writer.WriteInt64(Value.Z.Value);
	}

	bool WriteFixedTransform(
		FSeinCanonicalDigestWriter& Writer,
		const FFixedTransform& Value)
	{
		return WriteFixedVector(Writer, Value.Location)
			&& Writer.WriteInt64(Value.Rotation.X.Value)
			&& Writer.WriteInt64(Value.Rotation.Y.Value)
			&& Writer.WriteInt64(Value.Rotation.Z.Value)
			&& Writer.WriteInt64(Value.Rotation.W.Value)
			&& WriteFixedVector(Writer, Value.Scale);
	}

	bool ComputeIncrementalEntitySlotDigest(
		int32 SlotIndex,
		const FSeinEntityPoolSlotState& Slot,
		const UClass* ActorClass,
		FGuid& OutDigest,
		FString& OutError)
	{
		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.LiveWorld.Incremental.EntitySlot"), 1);
		return Writer.WriteInt32(SlotIndex)
			&& Writer.WriteInt32(Slot.Entity.ID.Value)
			&& WriteFixedTransform(Writer, Slot.Entity.Transform)
			&& Writer.WriteInt32(Slot.Entity.Flags)
			&& Writer.WriteInt32(Slot.Generation)
			&& Writer.WriteUInt8(Slot.Owner.Value)
			&& Writer.WriteBool(Slot.bRetired)
			&& Writer.WriteBool(ActorClass != nullptr)
			&& (!ActorClass || Writer.WriteString(ActorClass->GetPathName()))
			&& Writer.Finalize(OutDigest, OutError);
	}

	bool ComputeIncrementalComponentSlotDigest(
		const UScriptStruct* Type,
		const FGuid& SchemaDigest,
		FSeinEntityHandle Handle,
		const void* Value,
		FGuid& OutDigest,
		FString& OutError)
	{
		if (!Type || !Value || !SchemaDigest.IsValid())
		{
			return Fail(
				OutError,
				TEXT("Incremental component leaf requires an exact type, schema, and value."));
		}

		FGuid ValueDigest;
		FSeinCanonicalReflectedStateLimits Limits;
		if (!FSeinCanonicalReflectedStateDigest::ComputeStructValueDigest(
			Type,
			Value,
			SchemaDigest,
			Limits,
			ValueDigest,
			OutError))
		{
			return false;
		}

		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.LiveWorld.Incremental.ComponentSlot"), 1);
		return Writer.WriteString(Type->GetPathName())
			&& Writer.WriteGuid(SchemaDigest)
			&& WriteHandle(Writer, Handle)
			&& Writer.WriteGuid(ValueDigest)
			&& Writer.Finalize(OutDigest, OutError);
	}

	bool ComputeIncrementalObjectPoolSlotDigest(
		const FSeinPoolObjectCodecManifest& Manifest,
		const UObject& Object,
		ESeinPoolObjectKind Kind,
		FStringView PoolName,
		int32 PoolIndex,
		FGuid& OutDigest,
		FString& OutError)
	{
		TArray<uint8> StateBytes;
		FGuid RootClassContractDigest;
		if (!FSeinPoolObjectCodecRegistry::CaptureObjectForVerifiedRoot(
			Manifest,
			Object,
			Kind,
			StateBytes,
			RootClassContractDigest,
			OutError))
		{
			return false;
		}

		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.LiveWorld.Incremental.ObjectPoolSlot"), 1);
		return Writer.WriteString(FString(PoolName))
			&& Writer.WriteInt32(PoolIndex)
			&& Writer.WriteGuid(RootClassContractDigest)
			&& Writer.WriteBytes(StateBytes)
			&& Writer.Finalize(OutDigest, OutError);
	}

	bool ComputeEntityPoolExactValueDigest(
		const FSeinEntityPoolExactState& State,
		FGuid& OutDigest,
		FString& OutError)
	{
		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.LiveWorld.EntityPoolExactState"), 1);
		if (!Writer.WriteInt32(State.Capacity)
			|| !Writer.WriteUInt32(static_cast<uint32>(State.Slots.Num())))
		{
			return Fail(OutError, Writer.GetError());
		}
		for (const FSeinEntityPoolSlotState& Slot : State.Slots)
		{
			if (!Writer.WriteInt32(Slot.Entity.ID.Value)
				|| !WriteFixedTransform(Writer, Slot.Entity.Transform)
				|| !Writer.WriteInt32(Slot.Entity.Flags)
				|| !Writer.WriteInt32(Slot.Generation)
				|| !Writer.WriteUInt8(Slot.Owner.Value)
				|| !Writer.WriteBool(Slot.bRetired))
			{
				return Fail(OutError, Writer.GetError());
			}
		}
		if (!Writer.WriteUInt32(static_cast<uint32>(State.FreeList.Num())))
		{
			return Fail(OutError, Writer.GetError());
		}
		for (const int32 FreeSlot : State.FreeList)
		{
			if (!Writer.WriteInt32(FreeSlot))
			{
				return Fail(OutError, Writer.GetError());
			}
		}
		return Writer.Finalize(OutDigest, OutError);
	}

	bool WriteClassPath(
		FSeinCanonicalDigestWriter& Writer,
		const UClass* Class)
	{
		return Writer.WriteString(Class ? Class->GetPathName() : FString());
	}

	bool WriteGameplayTag(
		FSeinCanonicalDigestWriter& Writer,
		const FGameplayTag Tag)
	{
		return Writer.WriteString(Tag.IsValid() ? Tag.ToString() : FString());
	}

	bool WriteTagContainer(
		FSeinCanonicalDigestWriter& Writer,
		const FGameplayTagContainer& Container)
	{
		TArray<FGameplayTag> Tags;
		Container.GetGameplayTagArray(Tags);
		Tags.Sort([](const FGameplayTag A, const FGameplayTag B)
		{
			return A.ToString() < B.ToString();
		});
		if (!Writer.WriteUInt32(static_cast<uint32>(Tags.Num())))
		{
			return false;
		}
		for (const FGameplayTag Tag : Tags)
		{
			if (!WriteGameplayTag(Writer, Tag))
			{
				return false;
			}
		}
		return true;
	}

	bool WritePairCapabilityGrants(
		FSeinCanonicalDigestWriter& Writer,
		const TArray<FSeinPairCapabilityGrantRecord>& Grants)
	{
		if (!Writer.WriteUInt32(static_cast<uint32>(Grants.Num())))
		{
			return false;
		}
		for (const FSeinPairCapabilityGrantRecord& Grant : Grants)
		{
			if (!Writer.WriteUInt8(Grant.SourcePlayer.Value)
				|| !Writer.WriteUInt8(Grant.TargetPlayer.Value)
				|| !WriteGameplayTag(Writer, Grant.CapabilityTag)
				|| !WriteGameplayTag(Writer, Grant.SourceKindTag)
				|| !Writer.WriteInt64(Grant.SourceInstanceID)
				|| !Writer.WriteInt32(Grant.RefCount))
			{
				return false;
			}
		}
		return true;
	}

	FString CanonicalName(const FName Name)
	{
		return Name.IsNone()
			? FString()
			: Name.ToString().ToLower();
	}

	struct FReflectedDigestContext
	{
		explicit FReflectedDigestContext(FString& InError)
			: Error(InError)
		{
		}

		bool ResolveSchema(
			const UStruct* Type,
			const FString& Context,
			FGuid& OutSchema)
		{
			if (!Type)
			{
				return Fail(
					Error, Context + TEXT(": reflected type is null."));
			}
			if (const FGuid* Existing = Schemas.Find(Type))
			{
				OutSchema = *Existing;
				return true;
			}
			FString SchemaError;
			if (!FSeinCanonicalReflectedStateDigest::ComputeSchemaDigest(
				Type, Limits, OutSchema, SchemaError))
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("%s schema '%s' is not canonical: %s"),
						*Context,
						*Type->GetPathName(),
						*SchemaError));
			}
			Schemas.Add(Type, OutSchema);
			return true;
		}

		bool DigestStruct(
			const UScriptStruct* Type,
			const void* Memory,
			const FString& Context,
			FGuid& OutSchema,
			FGuid& OutValue)
		{
			if (!Memory || !ResolveSchema(Type, Context, OutSchema))
			{
				if (!Memory && Error.IsEmpty())
				{
					Error = Context + TEXT(": reflected value is null.");
				}
				return false;
			}
			FString ValueError;
			if (!FSeinCanonicalReflectedStateDigest::ComputeStructValueDigest(
				Type, Memory, OutSchema, Limits, OutValue, ValueError))
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("%s value is not canonical: %s"),
						*Context,
						*ValueError));
			}
			return true;
		}

		bool DigestStructSequence(
			const UScriptStruct* Type,
			TConstArrayView<const void*> Values,
			const FString& Context,
			FGuid& OutSchema,
			FGuid& OutValue)
		{
			if (!ResolveSchema(Type, Context, OutSchema))
			{
				return false;
			}
			FString ValueError;
			if (!FSeinCanonicalReflectedStateDigest::
				ComputeStructSequenceValueDigest(
					Type,
					Values,
					OutSchema,
					Limits,
					OutValue,
					ValueError))
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("%s sequence is not canonical: %s"),
						*Context,
						*ValueError));
			}
			return true;
		}

		bool DigestObject(
			const UObject* Object,
			const FString& Context,
			FGuid& OutSchema,
			FGuid& OutValue)
		{
			if (!Object
				|| !ResolveSchema(Object->GetClass(), Context, OutSchema))
			{
				if (!Object && Error.IsEmpty())
				{
					Error = Context + TEXT(": object is null.");
				}
				return false;
			}
			FString ValueError;
			if (!FSeinCanonicalReflectedStateDigest::ComputeObjectValueDigest(
				Object, OutSchema, Limits, OutValue, ValueError))
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("%s value is not canonical: %s"),
						*Context,
						*ValueError));
			}
			return true;
		}

		FSeinCanonicalReflectedStateLimits Limits;
		TMap<const UStruct*, FGuid> Schemas;
		FString& Error;
	};

	bool BuildLeafContract(
		const FString& SectionId,
		const ESeinSnapshotSectionRole Role,
		const uint32 SchemaVersion,
		FGuid& OutSchemaDigest,
		FGuid& OutDescriptorDigest,
		FString& OutError)
	{
		FSeinCanonicalDigestWriter SchemaWriter(
			TEXT("SeinARTS.LiveWorld.BuiltInSchema"), 1);
		if (!SchemaWriter.WriteString(SectionId)
			|| !SchemaWriter.WriteUInt32(SchemaVersion)
			|| !SchemaWriter.Finalize(OutSchemaDigest, OutError))
		{
			return false;
		}

		FSeinCanonicalDigestWriter DescriptorWriter(
			TEXT("SeinARTS.LiveWorld.BuiltInDescriptor"), 1);
		return DescriptorWriter.WriteString(SectionId)
			&& DescriptorWriter.WriteUInt8(static_cast<uint8>(Role))
			&& DescriptorWriter.WriteUInt32(SchemaVersion)
			&& DescriptorWriter.WriteGuid(OutSchemaDigest)
			&& DescriptorWriter.Finalize(
				OutDescriptorDigest, OutError);
	}

	ESeinSnapshotSectionRole ToSnapshotRole(
		const ESeinCanonicalStateRole Role)
	{
		switch (Role)
		{
		case ESeinCanonicalStateRole::Authoritative:
			return ESeinSnapshotSectionRole::Authoritative;
		case ESeinCanonicalStateRole::Continuation:
			return ESeinSnapshotSectionRole::Continuation;
		case ESeinCanonicalStateRole::DerivedCache:
			return ESeinSnapshotSectionRole::DerivedCache;
		default:
			return static_cast<ESeinSnapshotSectionRole>(0);
		}
	}

	template<typename ObjectType>
	bool WriteObjectPool(
		FSeinCanonicalDigestWriter& Writer,
		const TCHAR* PoolName,
		const TArray<TObjectPtr<ObjectType>>& Pool,
		const TArray<int32>& FreeList,
		const FSeinPoolObjectCodecManifest& CodecManifest,
		const ESeinPoolObjectKind ObjectKind,
		TSet<const UObject*>& SeenObjects,
		FString& OutError)
	{
		uint64 ProfileStateBytes = 0;
		int32 ProfileLiveObjects = 0;
		if (FreeList.Num() > Pool.Num()
			|| !Writer.WriteString(PoolName)
			|| !Writer.WriteUInt32(static_cast<uint32>(Pool.Num())))
		{
			return FreeList.Num() > Pool.Num()
				? Fail(
					OutError,
					FString::Printf(
						TEXT("%s free list exceeds its pool."),
						PoolName))
				: false;
		}

		TSet<int32> FreeSlots;
		for (const int32 FreeIndex : FreeList)
		{
			if (!Pool.IsValidIndex(FreeIndex)
				|| FreeSlots.Contains(FreeIndex)
				|| Pool[FreeIndex] != nullptr)
			{
				return Fail(
					OutError,
					FString::Printf(
						TEXT("%s free list contains invalid, duplicate, or live slot %d."),
						PoolName,
						FreeIndex));
			}
			FreeSlots.Add(FreeIndex);
		}

		struct FPoolCaptureWork
		{
			int32 PoolIndex = INDEX_NONE;
			const UObject* Object = nullptr;
			bool bParallelReflected = false;
			TArray<uint8> StateBytes;
			FGuid RootClassContractDigest;
			FString Error;
		};
		TArray<FPoolCaptureWork> CaptureWork;
		CaptureWork.Reserve(Pool.Num() - FreeList.Num());

		for (int32 Index = 0; Index < Pool.Num(); ++Index)
		{
			const UObject* Object = Pool[Index].Get();
			if ((Object == nullptr) != FreeSlots.Contains(Index)
				|| !Writer.WriteInt32(Index)
				|| !Writer.WriteBool(Object != nullptr))
			{
				return (Object == nullptr) != FreeSlots.Contains(Index)
					? Fail(
						OutError,
						FString::Printf(
							TEXT("%s pool/free-list topology disagrees at slot %d."),
							PoolName,
							Index))
					: false;
			}
			if (!Object)
			{
				continue;
			}
			if (SeenObjects.Contains(Object))
			{
				return Fail(
					OutError,
					FString::Printf(
						TEXT("Live object '%s' appears more than once in the deterministic pools."),
						*Object->GetPathName()));
			}
			SeenObjects.Add(Object);
			FPoolCaptureWork& Work = CaptureWork.AddDefaulted_GetRef();
			Work.PoolIndex = Index;
			Work.Object = Object;
			if (!FSeinPoolObjectCodecRegistry::
					ResolveVerifiedRootCaptureMode(
						CodecManifest,
						*Object,
						ObjectKind,
						Work.bParallelReflected,
						Work.Error))
			{
				return Fail(
					OutError,
					FString::Printf(
						TEXT("%s slot %d canonical preflight failed: %s"),
						PoolName,
						Index,
						*Work.Error));
			}
		}

		const auto CaptureOne = [
			&CaptureWork,
			&CodecManifest,
			ObjectKind](const int32 WorkIndex)
		{
			FPoolCaptureWork& Work = CaptureWork[WorkIndex];
			if (!Work.Object)
			{
				Work.Error = TEXT("Pool capture work lost its object.");
				return;
			}
			FSeinPoolObjectCodecRegistry::CaptureObjectForVerifiedRoot(
				CodecManifest,
				*Work.Object,
				ObjectKind,
				Work.StateBytes,
				Work.RootClassContractDigest,
				Work.Error);
		};

		// Explicit provider callbacks retain their documented game-thread-only
		// contract. Built-in reflected captures are immutable reads at this stable
		// boundary, write disjoint byte arrays, and may run in parallel while the
		// game thread waits (so neither sim mutation nor GC can overlap them).
		for (int32 WorkIndex = 0;
			WorkIndex < CaptureWork.Num();
			++WorkIndex)
		{
			if (!CaptureWork[WorkIndex].bParallelReflected)
			{
				CaptureOne(WorkIndex);
			}
		}
		if (SeinSimParallelEnabled())
		{
#if !UE_BUILD_SHIPPING
			SeinSetInParallelSection(true);
#endif
			ParallelFor(CaptureWork.Num(),
				[&CaptureWork, &CaptureOne](const int32 WorkIndex)
				{
					if (CaptureWork[WorkIndex].bParallelReflected)
					{
						CaptureOne(WorkIndex);
					}
				});
#if !UE_BUILD_SHIPPING
			SeinSetInParallelSection(false);
#endif
		}
		else
		{
			for (int32 WorkIndex = 0;
				WorkIndex < CaptureWork.Num();
				++WorkIndex)
			{
				if (CaptureWork[WorkIndex].bParallelReflected)
				{
					CaptureOne(WorkIndex);
				}
			}
		}

		for (const FPoolCaptureWork& Work : CaptureWork)
		{
			if (!Work.Error.IsEmpty()
				|| !Work.RootClassContractDigest.IsValid()
				|| !Writer.WriteGuid(Work.RootClassContractDigest)
				|| !Writer.WriteBytes(Work.StateBytes))
			{
				return Fail(
					OutError,
					Work.Error.IsEmpty()
						? FString::Printf(
							TEXT("%s slot %d produced an invalid canonical pool record."),
							PoolName,
							Work.PoolIndex)
						: FString::Printf(
							TEXT("%s slot %d canonical capture failed: %s"),
							PoolName,
							Work.PoolIndex,
							*Work.Error));
			}
			ProfileStateBytes +=
				static_cast<uint64>(Work.StateBytes.Num());
			++ProfileLiveObjects;
		}

		if (!Writer.WriteUInt32(static_cast<uint32>(FreeList.Num())))
		{
			return false;
		}
		for (const int32 FreeIndex : FreeList)
		{
			if (!Writer.WriteInt32(FreeIndex))
			{
				return false;
			}
		}
		if (CVarSeinStateRootProfile.GetValueOnGameThread() != 0)
		{
			UE_LOG(
				LogSeinSim,
				Display,
				TEXT("Canonical state root pool profile: %s live=%d state_bytes=%llu"),
				PoolName,
				ProfileLiveObjects,
				static_cast<unsigned long long>(ProfileStateBytes));
		}
		return true;
	}

	bool WriteCommandQueue(
		FSeinCanonicalDigestWriter& Writer,
		const TCHAR* LaneId,
		const TArray<FSeinCommand>& Commands,
		FReflectedDigestContext& Reflected)
	{
		if (!Writer.WriteString(LaneId)
			|| !Writer.WriteUInt32(static_cast<uint32>(Commands.Num())))
		{
			return false;
		}
		for (int32 Index = 0; Index < Commands.Num(); ++Index)
		{
			FGuid SchemaDigest;
			FGuid ValueDigest;
			if (!Reflected.DigestStruct(
				FSeinCommand::StaticStruct(),
				&Commands[Index],
				FString::Printf(
					TEXT("%s command %d"), LaneId, Index),
				SchemaDigest,
				ValueDigest)
				|| !Writer.WriteGuid(SchemaDigest)
				|| !Writer.WriteGuid(ValueDigest))
			{
				return false;
			}
		}
		return true;
	}
}

bool USeinWorldSubsystem::RefreshCanonicalStateRootCacheCore(
	bool bForceFullRebuild,
	FString& OutError) const
{
	OutError.Reset();
	if (!IsInGameThread())
	{
		return Fail(
			OutError,
			TEXT("Incremental canonical-state maintenance requires the game thread."));
	}
	if (!CanonicalStateRootCache.IsValid())
	{
		CanonicalStateRootCache = MakeShared<FSeinWorldStateRootCache>();
		bForceFullRebuild = true;
	}
	FSeinWorldStateRootCache& Cache = *CanonicalStateRootCache;

	TArray<FSeinEntityHandle> Entities;
	Entities.Reserve(EntityPool.GetActiveCount());
	EntityPool.ForEachEntity(
		[&Entities](const FSeinEntityHandle Handle, const FSeinEntity&)
		{
			Entities.Add(Handle);
		});
	Entities.Sort();
	if (Entities.Num() != EntityPool.GetActiveCount())
	{
		return Fail(
			OutError,
			TEXT("Incremental root found an entity-pool active-count mismatch."));
	}

	// Entity slots include allocator metadata and actor-class identity. Ordinary
	// transform/flag/owner writes replace only that slot. Spawn, destroy, pool
	// growth, generation advance, or class-map topology rebuilds the slot tree
	// and exact free-list frame.
	FSeinWorldStateRootCache::FEntityPoolCache& EntityCache =
		Cache.EntityPool;
	const bool bEntityTopologyChanged = bForceFullRebuild
		|| EntityCache.TopologyRevision != EntityPool.GetTopologyRevision()
		|| EntityCache.Tree.Num() != EntityPool.GetCapacity() + 1;
	bool bEntitySectionChanged = bEntityTopologyChanged;
	if (bEntityTopologyChanged)
	{
		FSeinEntityPoolExactState ExactState;
		if (!EntityPool.CaptureExactState(
			ExactState,
			OutError,
			/*bAllowDeferredDestroyTombstones=*/true))
		{
			return false;
		}
		if (!EntityCache.Tree.Reset(
			TEXT("core.entity-pool.slots"),
			ExactState.Slots.Num(),
			OutError))
		{
			return false;
		}
		EntityCache.SlotRevisions.Init(0, ExactState.Slots.Num());
		for (int32 SlotIndex = 0;
			SlotIndex < ExactState.Slots.Num();
			++SlotIndex)
		{
			const FSeinEntityPoolSlotState& Slot =
				ExactState.Slots[SlotIndex];
			const FSeinEntityHandle Handle(
				SlotIndex, Slot.Generation);
			const UClass* ActorClass = nullptr;
			if (Slot.Entity.IsAlive())
			{
				if (const TSubclassOf<ASeinActor>* Found =
					EntityActorClassMap.Find(Handle))
				{
					ActorClass = Found->Get();
				}
			}
			FGuid SlotDigest;
			if (!ComputeIncrementalEntitySlotDigest(
				SlotIndex,
				Slot,
				ActorClass,
				SlotDigest,
				OutError)
				|| !EntityCache.Tree.SetLeafDigest(
					SlotIndex, SlotDigest, OutError))
			{
				return false;
			}
			if (Slot.Entity.IsAlive())
			{
				EntityCache.SlotRevisions[SlotIndex] =
					EntityPool.GetMutationRevision(Handle);
			}
			++Cache.EntitySlotsProjected;
		}
		if (!EntityCache.Tree.FinalizeUpdates(OutError))
		{
			return false;
		}
		EntityCache.TopologyRevision =
			EntityPool.GetTopologyRevision();
	}
	else if (EntityCache.LatestMutationRevision
		!= EntityPool.GetLatestMutationRevision())
	{
		for (const FSeinEntityHandle Handle : Entities)
		{
			const uint64 Revision =
				EntityPool.GetMutationRevision(Handle);
			if (!EntityCache.SlotRevisions.IsValidIndex(Handle.Index)
				|| EntityCache.SlotRevisions[Handle.Index] == Revision)
			{
				continue;
			}
			const FSeinEntity* Entity = EntityPool.Get(Handle);
			if (!Entity)
			{
				return Fail(
					OutError,
					TEXT("Incremental entity projection lost a live slot."));
			}
			FSeinEntityPoolSlotState Slot;
			Slot.Entity = *Entity;
			Slot.Generation = Handle.Generation;
			Slot.Owner = EntityPool.GetOwner(Handle);
			const TSubclassOf<ASeinActor>* Found =
				EntityActorClassMap.Find(Handle);
			FGuid SlotDigest;
			if (!ComputeIncrementalEntitySlotDigest(
				Handle.Index,
				Slot,
				Found ? Found->Get() : nullptr,
				SlotDigest,
				OutError)
				|| !EntityCache.Tree.SetLeafDigest(
					Handle.Index, SlotDigest, OutError))
			{
				return false;
			}
			EntityCache.SlotRevisions[Handle.Index] = Revision;
			bEntitySectionChanged = true;
			++Cache.EntitySlotsProjected;
		}
		if (!EntityCache.Tree.FinalizeUpdates(OutError))
		{
			return false;
		}
	}
	EntityCache.LatestMutationRevision =
		EntityPool.GetLatestMutationRevision();
	if (bEntitySectionChanged || !EntityCache.SectionDigest.IsValid())
	{
		FSeinEntityPoolExactState ExactMetadata;
		if (!EntityPool.CaptureExactState(
			ExactMetadata,
			OutError,
			/*bAllowDeferredDestroyTombstones=*/true))
		{
			return false;
		}
		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.LiveWorld.Incremental.EntityPool"), 1);
		if (!Writer.WriteInt32(ExactMetadata.Capacity)
			|| !Writer.WriteInt32(EntityPool.GetActiveCount())
			|| !Writer.WriteUInt32(
				static_cast<uint32>(ExactMetadata.FreeList.Num())))
		{
			return Fail(OutError, Writer.GetError());
		}
		for (const int32 FreeSlot : ExactMetadata.FreeList)
		{
			if (!Writer.WriteInt32(FreeSlot))
			{
				return Fail(OutError, Writer.GetError());
			}
		}
		if (!Writer.WriteGuid(EntityCache.Tree.GetRoot())
			|| !Writer.Finalize(EntityCache.SectionDigest, OutError))
		{
			return false;
		}
	}

	// Component types are stable schema families. Their indexed trees use the
	// entity slot as the leaf position, so add/remove/generation changes rebuild
	// only that type while ordinary writes replace the affected values.
	TArray<UScriptStruct*> ComponentTypes;
	ComponentStorages.GetKeys(ComponentTypes);
	ComponentTypes.Sort([](const UScriptStruct& A, const UScriptStruct& B)
	{
		return A.GetPathName() < B.GetPathName();
	});
	TSet<FString> CurrentComponentPaths;
	for (const UScriptStruct* Type : ComponentTypes)
	{
		if (Type)
		{
			CurrentComponentPaths.Add(Type->GetPathName());
		}
	}
	if (bForceFullRebuild
		|| Cache.Components.Num() != CurrentComponentPaths.Num())
	{
		Cache.Components.Reset();
		bForceFullRebuild = true;
	}
	else
	{
		for (const auto& Existing : Cache.Components)
		{
			if (!CurrentComponentPaths.Contains(Existing.Key))
			{
				Cache.Components.Reset();
				bForceFullRebuild = true;
				break;
			}
		}
	}

	FSeinCanonicalReflectedStateLimits ReflectedLimits;
	for (UScriptStruct* Type : ComponentTypes)
	{
		const ISeinComponentStorage* Storage = Type
			? ComponentStorages.FindRef(Type)
			: nullptr;
		if (!Type || !Storage)
		{
			return Fail(
				OutError,
				TEXT("Incremental component registry contains a null type or storage."));
		}
		const FString TypePath = Type->GetPathName();
		FSeinWorldStateRootCache::FComponentCache& ComponentCache =
			Cache.Components.FindOrAdd(TypePath);
		if (!ComponentCache.SchemaDigest.IsValid()
			&& !FSeinCanonicalReflectedStateDigest::ComputeSchemaDigest(
				Type,
				ReflectedLimits,
				ComponentCache.SchemaDigest,
				OutError))
		{
			return false;
		}
		ComponentCache.Type = Type;
		const bool bTopologyChanged = bForceFullRebuild
			|| ComponentCache.TopologyRevision
				!= Storage->GetTopologyRevision()
			|| ComponentCache.Tree.Num()
				!= EntityPool.GetCapacity() + 1;
		bool bSectionChanged = bTopologyChanged;
		if (bTopologyChanged)
		{
			if (!ComponentCache.Tree.Reset(
				TEXT("component/") + TypePath,
				EntityPool.GetCapacity() + 1,
				OutError))
			{
				return false;
			}
			ComponentCache.SlotRevisions.Init(
				0, EntityPool.GetCapacity() + 1);
			int32 OccupiedCount = 0;
			for (const FSeinEntityHandle Handle : Entities)
			{
				if (!Storage->HasComponent(Handle))
				{
					continue;
				}
				const void* Value = Storage->GetComponentRaw(Handle);
				FGuid SlotDigest;
				if (!Value
					|| !ComputeIncrementalComponentSlotDigest(
						Type,
						ComponentCache.SchemaDigest,
						Handle,
						Value,
						SlotDigest,
						OutError)
					|| !ComponentCache.Tree.SetLeafDigest(
						Handle.Index, SlotDigest, OutError))
				{
					return false;
				}
				ComponentCache.SlotRevisions[Handle.Index] =
					Storage->GetMutationRevision(Handle);
				++OccupiedCount;
				++Cache.ComponentValueDigestsComputed;
			}
			if (OccupiedCount != Storage->GetComponentCount()
				|| !ComponentCache.Tree.FinalizeUpdates(OutError))
			{
				return OccupiedCount != Storage->GetComponentCount()
					? Fail(
						OutError,
						FString::Printf(
							TEXT("Incremental component storage '%s' contains orphaned state."),
							*TypePath))
					: false;
			}
			ComponentCache.ComponentCount = OccupiedCount;
			ComponentCache.TopologyRevision =
				Storage->GetTopologyRevision();
		}
		else if (ComponentCache.LatestMutationRevision
			!= Storage->GetLatestMutationRevision())
		{
			for (const FSeinEntityHandle Handle : Entities)
			{
				if (!Storage->HasComponent(Handle))
				{
					continue;
				}
				const uint64 Revision =
					Storage->GetMutationRevision(Handle);
				if (!ComponentCache.SlotRevisions.IsValidIndex(
						Handle.Index)
					|| ComponentCache.SlotRevisions[Handle.Index]
						== Revision)
				{
					continue;
				}
				FGuid SlotDigest;
				if (!ComputeIncrementalComponentSlotDigest(
					Type,
					ComponentCache.SchemaDigest,
					Handle,
					Storage->GetComponentRaw(Handle),
					SlotDigest,
					OutError)
					|| !ComponentCache.Tree.SetLeafDigest(
						Handle.Index, SlotDigest, OutError))
				{
					return false;
				}
				ComponentCache.SlotRevisions[Handle.Index] = Revision;
				bSectionChanged = true;
				++Cache.ComponentValueDigestsComputed;
			}
			if (!ComponentCache.Tree.FinalizeUpdates(OutError))
			{
				return false;
			}
		}
		ComponentCache.LatestMutationRevision =
			Storage->GetLatestMutationRevision();
		if (bSectionChanged || !ComponentCache.SectionDigest.IsValid())
		{
			FSeinCanonicalDigestWriter Writer(
				TEXT("SeinARTS.LiveWorld.Incremental.ComponentStorage"), 1);
			if (!Writer.WriteString(TypePath)
				|| !Writer.WriteGuid(ComponentCache.SchemaDigest)
				|| !Writer.WriteInt32(ComponentCache.ComponentCount)
				|| !Writer.WriteGuid(ComponentCache.Tree.GetRoot())
				|| !Writer.Finalize(
					ComponentCache.SectionDigest, OutError))
			{
				return false;
			}
		}
	}

	if (!PoolObjectCodecManifest.VerifyProviderLeases(OutError))
	{
		return false;
	}
	const auto RefreshObjectPool = [
		this,
		&Cache,
		bForceFullRebuild,
		&OutError](
			auto& Pool,
			const TArray<int32>& FreeList,
			const TArray<uint64>& StateRevisions,
			uint64 LatestMutationRevision,
			uint64 TopologyRevision,
			ESeinPoolObjectKind Kind,
			const TCHAR* PoolName,
			FSeinWorldStateRootCache::FObjectPoolCache& PoolCache)
			-> bool
	{
		const bool bTopologyChanged = bForceFullRebuild
			|| PoolCache.TopologyRevision != TopologyRevision
			|| PoolCache.Tree.Num() != Pool.Num();
		bool bSectionChanged = bTopologyChanged;
		TSet<int32> FreeSlots;
		if (FreeList.Num() > Pool.Num())
		{
			return Fail(
				OutError,
				FString::Printf(
					TEXT("Incremental %s free list exceeds its pool."),
					PoolName));
		}
		for (const int32 FreeIndex : FreeList)
		{
			if (!Pool.IsValidIndex(FreeIndex)
				|| Pool[FreeIndex] != nullptr
				|| FreeSlots.Contains(FreeIndex))
			{
				return Fail(
					OutError,
					FString::Printf(
						TEXT("Incremental %s free list contains invalid slot %d."),
						PoolName,
						FreeIndex));
			}
			FreeSlots.Add(FreeIndex);
		}

		if (bTopologyChanged)
		{
			if (!PoolCache.Tree.Reset(
				FString(TEXT("pool/")) + PoolName,
				Pool.Num(),
				OutError))
			{
				return false;
			}
			PoolCache.SlotRevisions.Init(0, Pool.Num());
		}
		for (int32 Index = 0; Index < Pool.Num(); ++Index)
		{
			const UObject* Object = Pool[Index].Get();
			if ((Object == nullptr) != FreeSlots.Contains(Index))
			{
				return Fail(
					OutError,
					FString::Printf(
						TEXT("Incremental %s topology disagrees at slot %d."),
						PoolName,
						Index));
			}
			if (!Object)
			{
				continue;
			}
			const uint64 Revision = StateRevisions.IsValidIndex(Index)
				? StateRevisions[Index]
				: 0;
			if (!bTopologyChanged
				&& PoolCache.LatestMutationRevision
					== LatestMutationRevision)
			{
				continue;
			}
			if (!bTopologyChanged
				&& PoolCache.SlotRevisions.IsValidIndex(Index)
				&& PoolCache.SlotRevisions[Index] == Revision)
			{
				continue;
			}
			FGuid SlotDigest;
			if (!ComputeIncrementalObjectPoolSlotDigest(
				PoolObjectCodecManifest,
				*Object,
				Kind,
				PoolName,
				Index,
				SlotDigest,
				OutError)
				|| !PoolCache.Tree.SetLeafDigest(
					Index, SlotDigest, OutError))
			{
				return false;
			}
			PoolCache.SlotRevisions[Index] = Revision;
			bSectionChanged = true;
			++Cache.PoolObjectsCaptured;
		}
		if (!PoolCache.Tree.FinalizeUpdates(OutError))
		{
			return false;
		}
		PoolCache.TopologyRevision = TopologyRevision;
		PoolCache.LatestMutationRevision = LatestMutationRevision;
		if (bSectionChanged || !PoolCache.SectionDigest.IsValid())
		{
			FSeinCanonicalDigestWriter Writer(
				TEXT("SeinARTS.LiveWorld.Incremental.ObjectPool"), 1);
			if (!Writer.WriteString(PoolName)
				|| !Writer.WriteInt32(Pool.Num())
				|| !Writer.WriteUInt32(
					static_cast<uint32>(FreeList.Num())))
			{
				return Fail(OutError, Writer.GetError());
			}
			for (const int32 FreeIndex : FreeList)
			{
				if (!Writer.WriteInt32(FreeIndex))
				{
					return Fail(OutError, Writer.GetError());
				}
			}
			if (!Writer.WriteGuid(PoolCache.Tree.GetRoot())
				|| !Writer.Finalize(PoolCache.SectionDigest, OutError))
			{
				return false;
			}
		}
		return true;
	};

	if (!RefreshObjectPool(
		AbilityPool,
		AbilityPoolFreeList,
		AbilityPoolStateRevisions,
		AbilityPoolMutationRevision,
		AbilityPoolTopologyRevision,
		ESeinPoolObjectKind::Ability,
		TEXT("AbilityPool"),
		Cache.AbilityPool)
		|| !RefreshObjectPool(
			CommandBrokerResolverPool,
			CommandBrokerResolverPoolFreeList,
			CommandBrokerResolverPoolStateRevisions,
			CommandBrokerResolverPoolMutationRevision,
			CommandBrokerResolverPoolTopologyRevision,
			ESeinPoolObjectKind::CommandBrokerResolver,
			TEXT("CommandBrokerResolverPool"),
			Cache.ResolverPool))
	{
		return false;
	}

	FSeinCanonicalDigestWriter AcceleratorWriter(
		TEXT("SeinARTS.LiveWorld.Incremental.CoreAccelerator"), 1);
	if (!AcceleratorWriter.WriteGuid(EntityCache.SectionDigest)
		|| !AcceleratorWriter.WriteUInt32(
			static_cast<uint32>(ComponentTypes.Num())))
	{
		return Fail(OutError, AcceleratorWriter.GetError());
	}
	for (const UScriptStruct* Type : ComponentTypes)
	{
		const FSeinWorldStateRootCache::FComponentCache* Found =
			Type ? Cache.Components.Find(Type->GetPathName()) : nullptr;
		if (!Found
			|| !AcceleratorWriter.WriteString(Type->GetPathName())
			|| !AcceleratorWriter.WriteGuid(Found->SectionDigest))
		{
			return Fail(
				OutError,
				Found
					? AcceleratorWriter.GetError()
					: TEXT("Incremental component cache lost a canonical type."));
		}
	}
	return AcceleratorWriter.WriteGuid(Cache.AbilityPool.SectionDigest)
		&& AcceleratorWriter.WriteGuid(Cache.ResolverPool.SectionDigest)
		&& AcceleratorWriter.Finalize(
			Cache.CoreAcceleratorDigest, OutError);
}

bool USeinWorldSubsystem::RefreshCanonicalStateRootCacheContinuation(
	bool bForceFullRebuild,
	FString& OutError) const
{
	OutError.Reset();
	if (!CanonicalStateRootCache.IsValid())
	{
		CanonicalStateRootCache = MakeShared<FSeinWorldStateRootCache>();
		bForceFullRebuild = true;
	}
	FSeinWorldStateRootCache& Cache = *CanonicalStateRootCache;
	FSeinWorldStateRootCache::FLatentActionCache& LatentCache =
		Cache.LatentActions;
	const TConstArrayView<TObjectPtr<USeinLatentAction>> Actions =
		LatentActionManager
			? LatentActionManager->GetActiveActions()
			: TConstArrayView<TObjectPtr<USeinLatentAction>>();
	if (Actions.Num() > FSeinLatentActionCodecRegistry::MaxActiveActions)
	{
		return Fail(
			OutError,
			TEXT("Incremental latent-action count exceeds the checkpoint bound."));
	}

	const uint64 TopologyRevision = LatentActionManager
		? LatentActionManager->GetTopologyRevision()
		: 0;
	const uint64 LatestMutationRevision = LatentActionManager
		? LatentActionManager->GetLatestMutationRevision()
		: 0;
	const bool bTopologyChanged = bForceFullRebuild
		|| LatentCache.TopologyRevision != TopologyRevision;
	const bool bStateChanged = bForceFullRebuild
		|| LatentCache.LatestMutationRevision
			!= LatestMutationRevision;
	const bool bCursorChanged =
		LatentCache.NextActionID
			!= (LatentActionManager
				? LatentActionManager->GetNextActionID()
				: 1)
		|| LatentCache.NextAbilityActivationID
			!= NextAbilityActivationID;
	if (!bTopologyChanged && !bStateChanged && !bCursorChanged
		&& LatentCache.SequenceDigest.IsValid())
	{
		return true;
	}

	TMap<int64, FSeinWorldStateRootCache::FLatentActionCache::FEntry>
		UpdatedEntries;
	UpdatedEntries.Reserve(Actions.Num());
	TArray<FSeinSnapshotLatentActionRecord> UpdatedRecords;
	UpdatedRecords.Reserve(Actions.Num());
	int64 PreviousActionID = 0;
	int64 AggregatePayloadBytes = 0;
	for (int32 Ordinal = 0; Ordinal < Actions.Num(); ++Ordinal)
	{
		const USeinLatentAction* Action = Actions[Ordinal].Get();
		if (!Action
			|| Action->GetActionID() <= PreviousActionID)
		{
			return Fail(
				OutError,
				TEXT("Incremental latent manager contains a null, duplicate, or reordered action."));
		}
		PreviousActionID = Action->GetActionID();
		const uint64 Revision =
			Action->GetCanonicalMutationRevision();
		const FSeinWorldStateRootCache::FLatentActionCache::FEntry*
			Existing = LatentCache.Entries.Find(Action->GetActionID());
		FSeinWorldStateRootCache::FLatentActionCache::FEntry Entry;
		if (!bForceFullRebuild && Existing
			&& Existing->Revision == Revision)
		{
			Entry = *Existing;
			if (Entry.Record.Ordinal != Ordinal)
			{
				Entry.Record.Ordinal = Ordinal;
				if (!FSeinLatentActionCodecRegistry::
					RecomputeRecordDigestForVerifiedRoot(
						Entry.Record, OutError))
				{
					return false;
				}
			}
		}
		else
		{
			Entry.Revision = Revision;
			if (!FSeinLatentActionCodecRegistry::
				CaptureRecordForVerifiedRoot(
					LatentActionCodecManifest,
					*this,
					*Action,
					CurrentTick,
					Ordinal,
					LatentActionManager
						? LatentActionManager->GetNextActionID()
						: 1,
					NextAbilityActivationID,
					Entry.Record,
					OutError))
			{
				return false;
			}
			++Cache.LatentActionsCaptured;
		}
		AggregatePayloadBytes += Entry.Record.PayloadBytes.Num();
		if (AggregatePayloadBytes
			> FSeinLatentActionCodecRegistry::MaxAggregatePayloadBytes)
		{
			return Fail(
				OutError,
				TEXT("Incremental latent payload aggregate exceeds its checkpoint bound."));
		}
		UpdatedRecords.Add(Entry.Record);
		UpdatedEntries.Add(Action->GetActionID(), MoveTemp(Entry));
	}

	const int64 NextActionID = LatentActionManager
		? LatentActionManager->GetNextActionID()
		: 1;
	FGuid SequenceDigest;
	if (!FSeinLatentActionCodecRegistry::
		ComputeSequenceDigestForVerifiedRoot(
			NextActionID,
			NextAbilityActivationID,
			UpdatedRecords,
			SequenceDigest,
			OutError))
	{
		return false;
	}

	LatentCache.Entries = MoveTemp(UpdatedEntries);
	LatentCache.OrderedRecords = MoveTemp(UpdatedRecords);
	LatentCache.SequenceDigest = SequenceDigest;
	LatentCache.TopologyRevision = TopologyRevision;
	LatentCache.LatestMutationRevision = LatestMutationRevision;
	LatentCache.NextActionID = NextActionID;
	LatentCache.NextAbilityActivationID = NextAbilityActivationID;
	return true;
}

bool USeinWorldSubsystem::SealRoutineCanonicalStateRoot(
	int32 CompletedTick,
	bool bForceFullRebuild,
	FGuid& OutRoot,
	FString& OutError) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_World_RoutineCanonicalRoot);
	const double RoutineProfileStart = FPlatformTime::Seconds();
	const int64 ComponentDigestsBefore = CanonicalStateRootCache.IsValid()
		? CanonicalStateRootCache->ComponentValueDigestsComputed
		: 0;
	const int64 PoolCapturesBefore = CanonicalStateRootCache.IsValid()
		? CanonicalStateRootCache->PoolObjectsCaptured
		: 0;
	const int64 EntityProjectionsBefore = CanonicalStateRootCache.IsValid()
		? CanonicalStateRootCache->EntitySlotsProjected
		: 0;
	const int64 LatentCapturesBefore = CanonicalStateRootCache.IsValid()
		? CanonicalStateRootCache->LatentActionsCaptured
		: 0;
	OutRoot.Invalidate();
	OutError.Reset();
	if (!IsInGameThread() || CompletedTick != CurrentTick)
	{
		return Fail(
			OutError,
			TEXT("Routine canonical root requires the exact completed tick on the game thread."));
	}
	if (SeinIsInSimContext(this)
		|| OwnerTransitionDepth != 0
		|| bSnapshotCaptureInProgress
		|| bSnapshotRestoreInProgress
		|| bDestroyNotificationInProgress
		|| DeferredTeardownHandle.IsValid()
		|| bDispatchingPauseControlFrame
		|| bPauseControlDispatchProtocolViolation
		|| ActivePauseControlCommandIndex != INDEX_NONE
		|| ActivePauseControlCommandCount != 0)
	{
		return Fail(
			OutError,
			TEXT("Routine canonical root refused an in-flight simulation transaction."));
	}
	if (!ValidatePairCapabilityState())
	{
		return Fail(
			OutError,
			TEXT("Routine canonical root refused inconsistent pair-capability source records or effective cache."));
	}
	FString ContainmentError;
	if (!ValidateContainmentState(ContainmentError))
	{
		return Fail(
			OutError,
			FString::Printf(
				TEXT("Routine canonical root refused invalid containment state (%s)."),
				*ContainmentError));
	}
	const bool bCanonicalTimelineReady =
		bIsRunning
		|| (bSimulationSchedulerReserved && TickerHandle.IsValid());
	if (!bCanonicalTimelineReady
		|| MatchBootstrapState != ESeinMatchBootstrapState::Consumed
		|| !MatchBootstrapReceipt.IsValid()
		|| !MatchBootstrapAuthorizationContextDigest.IsValid()
		|| !CommandProtocolDigest.IsValid()
		|| !SimulationContentDigest.IsValid()
		|| !MatchSettingsDigest.IsValid()
		|| !NativeCanonicalStateSchema.IsValid()
		|| !CanonicalStateValues.IsSealed()
		|| CanonicalStateValues.GetContractDigest()
			!= MatchBootstrapReceipt.StateContractDigest)
	{
		return Fail(
			OutError,
			TEXT("Routine canonical root requires a consumed frozen match bootstrap."));
	}
	if (bReplayOwnsExternalCommandIngress)
	{
		return Fail(
			OutError,
			TEXT("Routine canonical root requires checkpointable replay continuation ownership."));
	}
	if (!RefreshCanonicalStateRootCacheCore(
			bForceFullRebuild, OutError)
		|| !RefreshCanonicalStateRootCacheContinuation(
			bForceFullRebuild, OutError)
		|| !CanonicalStateRootCache.IsValid())
	{
		return false;
	}
	FSeinWorldStateRootCache& Cache = *CanonicalStateRootCache;

	FSeinCanonicalDigestWriter CompatibilityWriter(
		TEXT("SeinARTS.LiveWorld.Compatibility"), 1);
	FGuid CompatibilityDigest;
	if (!CompatibilityWriter.WriteInt32(
			MatchBootstrapReceipt.FormatVersion)
		|| !CompatibilityWriter.WriteGuid(
			MatchBootstrapReceipt.ContractDigest)
		|| !CompatibilityWriter.WriteGuid(
			MatchBootstrapReceipt.SimulationContentDigest)
		|| !CompatibilityWriter.WriteGuid(
			MatchBootstrapReceipt.StateContractDigest)
		|| !CompatibilityWriter.WriteGuid(
			MatchBootstrapReceipt.PlanDigest)
		|| !CompatibilityWriter.WriteGuid(
			MatchBootstrapReceipt.InitialStateDigest)
		|| !CompatibilityWriter.WriteGuid(
			MatchBootstrapAuthorizationContextDigest)
		|| !CompatibilityWriter.WriteInt32(ConfigFingerprint)
		|| !CompatibilityWriter.Finalize(
			CompatibilityDigest, OutError))
	{
		return false;
	}

	if (bForceFullRebuild
		|| Cache.AuxiliaryMutationRevision
			!= CanonicalAuxiliaryMutationRevision
		|| !Cache.AuxiliaryDigest.IsValid())
	{
		FReflectedDigestContext Reflected(OutError);
		FSeinCanonicalDigestWriter AuxiliaryWriter(
			TEXT("SeinARTS.LiveWorld.Routine.Auxiliary"),
			RoutineAuxiliarySchemaVersion);
		FGuid MatchSettingsSchema;
		FGuid MatchSettingsValue;
		if (!Reflected.DigestStruct(
				FSeinMatchSettings::StaticStruct(),
				&CurrentMatchSettings,
				TEXT("Match settings"),
				MatchSettingsSchema,
				MatchSettingsValue)
			|| !AuxiliaryWriter.WriteGuid(MatchSettingsSchema)
			|| !AuxiliaryWriter.WriteGuid(MatchSettingsValue))
		{
			return false;
		}

		TArray<FSeinPlayerID> PlayerIds;
		PlayerStates.GetKeys(PlayerIds);
		PlayerIds.Sort();
		if (!AuxiliaryWriter.WriteUInt32(
			static_cast<uint32>(PlayerIds.Num())))
		{
			return Fail(OutError, AuxiliaryWriter.GetError());
		}
		for (const FSeinPlayerID PlayerId : PlayerIds)
		{
			const FSeinPlayerState& State =
				PlayerStates.FindChecked(PlayerId);
			FGuid SchemaDigest;
			FGuid ValueDigest;
			if (State.PlayerID != PlayerId
				|| !Reflected.DigestStruct(
					FSeinPlayerState::StaticStruct(),
					&State,
					FString::Printf(
						TEXT("Player state %u"), PlayerId.Value),
					SchemaDigest,
					ValueDigest)
				|| !AuxiliaryWriter.WriteUInt8(PlayerId.Value)
				|| !AuxiliaryWriter.WriteGuid(SchemaDigest)
				|| !AuxiliaryWriter.WriteGuid(ValueDigest))
			{
				return false;
			}
		}
		if (!WritePairCapabilityGrants(
				AuxiliaryWriter, GetPairCapabilityGrantRecords()))
		{
			return Fail(OutError, AuxiliaryWriter.GetError().IsEmpty()
				? TEXT("Routine pair-capability encoding failed.")
				: AuxiliaryWriter.GetError());
		}

		TArray<FSeinEntityHandle> TagStateHandles;
		EntityTagStates.GetKeys(TagStateHandles);
		TagStateHandles.Sort();
		if (!AuxiliaryWriter.WriteUInt32(
			static_cast<uint32>(TagStateHandles.Num())))
		{
			return Fail(OutError, AuxiliaryWriter.GetError());
		}
		for (const FSeinEntityHandle Handle : TagStateHandles)
		{
			const FSeinEntityTagState& State =
				EntityTagStates.FindChecked(Handle);
			if (!EntityPool.IsValid(Handle)
				|| !WriteHandle(AuxiliaryWriter, Handle)
				|| !WriteTagContainer(
					AuxiliaryWriter, State.BaseTags))
			{
				return Fail(
					OutError,
					TEXT("Routine tag registry contains stale or invalid state."));
			}
			TArray<FGameplayTag> RefCountTags;
			State.TagRefCounts.GetKeys(RefCountTags);
			RefCountTags.Sort([](
				const FGameplayTag A, const FGameplayTag B)
			{
				return A.ToString() < B.ToString();
			});
			if (!AuxiliaryWriter.WriteUInt32(
				static_cast<uint32>(RefCountTags.Num())))
			{
				return Fail(OutError, AuxiliaryWriter.GetError());
			}
			for (const FGameplayTag Tag : RefCountTags)
			{
				if (!WriteGameplayTag(AuxiliaryWriter, Tag)
					|| !AuxiliaryWriter.WriteInt32(
						State.TagRefCounts.FindChecked(Tag)))
				{
					return Fail(OutError, AuxiliaryWriter.GetError());
				}
			}
			if (!WriteTagContainer(
				AuxiliaryWriter, State.CombinedTags))
			{
				return Fail(OutError, AuxiliaryWriter.GetError());
			}
		}

		// The tag index is a derived acceleration structure, but it is consumed by
		// gameplay queries. Bind it whenever its source tag state changes so an
		// incoherent local index cannot silently survive a peer check.
		TArray<FGameplayTag> IndexedTags;
		EntityTagIndex.GetKeys(IndexedTags);
		IndexedTags.Sort([](
			const FGameplayTag A, const FGameplayTag B)
		{
			return A.ToString() < B.ToString();
		});
		if (!AuxiliaryWriter.WriteUInt32(
			static_cast<uint32>(IndexedTags.Num())))
		{
			return Fail(OutError, AuxiliaryWriter.GetError());
		}
		for (const FGameplayTag Tag : IndexedTags)
		{
			const TArray<FSeinEntityHandle>& Bucket =
				EntityTagIndex.FindChecked(Tag);
			if (!WriteGameplayTag(AuxiliaryWriter, Tag)
				|| !AuxiliaryWriter.WriteUInt32(
					static_cast<uint32>(Bucket.Num())))
			{
				return Fail(OutError, AuxiliaryWriter.GetError());
			}
			for (const FSeinEntityHandle Handle : Bucket)
			{
				if (!EntityPool.IsValid(Handle)
					|| !WriteHandle(AuxiliaryWriter, Handle))
				{
					return Fail(
						OutError,
						TEXT("Routine tag index contains a stale handle."));
				}
			}
		}

		TArray<FName> EntityNames;
		NamedEntityRegistry.GetKeys(EntityNames);
		EntityNames.Sort([](const FName A, const FName B)
		{
			return CanonicalName(A) < CanonicalName(B);
		});
		if (!AuxiliaryWriter.WriteUInt32(
			static_cast<uint32>(EntityNames.Num())))
		{
			return Fail(OutError, AuxiliaryWriter.GetError());
		}
		for (const FName Name : EntityNames)
		{
			const FSeinEntityHandle Handle =
				NamedEntityRegistry.FindChecked(Name);
			if (!EntityPool.IsValid(Handle)
				|| !AuxiliaryWriter.WriteName(Name)
				|| !WriteHandle(AuxiliaryWriter, Handle))
			{
				return Fail(
					OutError,
					TEXT("Routine named-entity registry contains stale state."));
			}
		}

		TArray<FGameplayTag> VoteTypes;
		ActiveVotes.GetKeys(VoteTypes);
		VoteTypes.Sort([](
			const FGameplayTag A, const FGameplayTag B)
		{
			return A.ToString() < B.ToString();
		});
		if (!AuxiliaryWriter.WriteUInt32(
			static_cast<uint32>(VoteTypes.Num())))
		{
			return Fail(OutError, AuxiliaryWriter.GetError());
		}
		for (const FGameplayTag VoteType : VoteTypes)
		{
			const FSeinVoteState& Vote =
				ActiveVotes.FindChecked(VoteType);
			FGuid SchemaDigest;
			FGuid ValueDigest;
			if (!Reflected.DigestStruct(
					FSeinVoteState::StaticStruct(),
					&Vote,
					FString::Printf(
						TEXT("Vote '%s'"), *VoteType.ToString()),
					SchemaDigest,
					ValueDigest)
				|| !WriteGameplayTag(AuxiliaryWriter, VoteType)
				|| !AuxiliaryWriter.WriteGuid(SchemaDigest)
				|| !AuxiliaryWriter.WriteGuid(ValueDigest))
			{
				return false;
			}
		}

		TArray<FSeinFactionID> FactionIds;
		Factions.GetKeys(FactionIds);
		FactionIds.Sort();
		if (!AuxiliaryWriter.WriteUInt32(
			static_cast<uint32>(FactionIds.Num())))
		{
			return Fail(OutError, AuxiliaryWriter.GetError());
		}
		for (const FSeinFactionID FactionId : FactionIds)
		{
			const USeinFaction* Faction =
				Factions.FindChecked(FactionId).Get();
			FGuid SchemaDigest;
			FGuid ValueDigest;
			if (!Faction
				|| Faction->FactionID != FactionId
				|| !Reflected.DigestObject(
					Faction,
					FString::Printf(
						TEXT("Faction %u"), FactionId.Value),
					SchemaDigest,
					ValueDigest)
				|| !AuxiliaryWriter.WriteUInt8(FactionId.Value)
				|| !AuxiliaryWriter.WriteString(Faction->GetPathName())
				|| !WriteClassPath(
					AuxiliaryWriter, Faction->GetClass())
				|| !AuxiliaryWriter.WriteGuid(SchemaDigest)
				|| !AuxiliaryWriter.WriteGuid(ValueDigest))
			{
				return false;
			}
		}
		if (!AuxiliaryWriter.Finalize(
			Cache.AuxiliaryDigest, OutError))
		{
			return false;
		}
		Cache.AuxiliaryMutationRevision =
			CanonicalAuxiliaryMutationRevision;
	}

	const int64 NextLatentActionID = LatentActionManager
		? LatentActionManager->GetNextActionID()
		: 1;
	FSeinCanonicalDigestWriter CoreWriter(
		TEXT("SeinARTS.LiveWorld.Routine.Core"),
		RoutineCoreAuthoritativeSchemaVersion);
	if (!CoreWriter.WriteInt32(CompletedTick)
		|| !CoreWriter.WriteInt64(SimSessionSeed)
		|| !CoreWriter.WriteUInt64(SimRandom.State0)
		|| !CoreWriter.WriteUInt64(SimRandom.State1)
		|| !CoreWriter.WriteInt64(NextEffectInstanceID)
		|| !CoreWriter.WriteInt64(NextLatentActionID)
		|| !CoreWriter.WriteInt64(NextAbilityActivationID)
		|| !CoreWriter.WriteUInt8(static_cast<uint8>(MatchState))
		|| !CoreWriter.WriteInt32(StartingStateDeadlineTick)
		|| !CoreWriter.WriteInt32(MatchStartTick)
		|| !CoreWriter.WriteBool(bSimPaused)
		|| !CoreWriter.WriteBool(bSimPausedHard)
		|| !CoreWriter.WriteInt64(PauseEpoch)
		|| !CoreWriter.WriteInt32(PauseFrozenTick)
		|| !CoreWriter.WriteInt64(LastAppliedPauseControlSequence)
		|| !CoreWriter.WriteGuid(MatchSettingsDigest)
		|| !CoreWriter.WriteGuid(SimulationContentDigest)
		|| !CoreWriter.WriteInt32(ConfigFingerprint)
		|| !CoreWriter.WriteGuid(Cache.CoreAcceleratorDigest)
		|| !CoreWriter.WriteGuid(Cache.AuxiliaryDigest))
	{
		return Fail(OutError, CoreWriter.GetError());
	}
	FGuid CoreDigest;
	if (!CoreWriter.Finalize(CoreDigest, OutError))
	{
		return false;
	}

	FSeinCanonicalDigestWriter ContinuationWriter(
		TEXT("SeinARTS.LiveWorld.Routine.Continuation"),
		RoutineCoreContinuationSchemaVersion);
	FReflectedDigestContext ContinuationReflected(OutError);
	if (!WriteCommandQueue(
			ContinuationWriter,
			TEXT("pending"),
			PendingCommands.GetCommands(),
			ContinuationReflected)
		|| !WriteCommandQueue(
			ContinuationWriter,
			TEXT("replay"),
			PendingReplayCommands.GetCommands(),
			ContinuationReflected)
		|| !WriteCommandQueue(
			ContinuationWriter,
			TEXT("pause"),
			PendingStandalonePauseControlCommands,
			ContinuationReflected)
		|| !ContinuationWriter.WriteUInt32(
			static_cast<uint32>(PendingDestroy.Num())))
	{
		return OutError.IsEmpty()
			? Fail(OutError, ContinuationWriter.GetError())
			: false;
	}
	for (const FSeinEntityHandle Handle : PendingDestroy)
	{
		if (!WriteHandle(ContinuationWriter, Handle))
		{
			return Fail(OutError, ContinuationWriter.GetError());
		}
	}
	if (!ContinuationWriter.WriteUInt32(
		static_cast<uint32>(PendingEffectApplies.Num())))
	{
		return Fail(OutError, ContinuationWriter.GetError());
	}
	for (int32 Index = 0; Index < PendingEffectApplies.Num(); ++Index)
	{
		const FSeinPendingEffectApply& Pending =
			PendingEffectApplies[Index];
		if (!Pending.EffectClass
			|| !WriteHandle(ContinuationWriter, Pending.Target)
			|| !WriteClassPath(
				ContinuationWriter, Pending.EffectClass.Get())
			|| !WriteHandle(ContinuationWriter, Pending.Source))
		{
			return Fail(
				OutError,
				TEXT("Routine continuation contains an invalid pending effect."));
		}
	}
	if (!ContinuationWriter.WriteUInt32(
			static_cast<uint32>(
				Cache.LatentActions.OrderedRecords.Num()))
		|| !ContinuationWriter.WriteGuid(
			Cache.LatentActions.SequenceDigest))
	{
		return Fail(OutError, ContinuationWriter.GetError());
	}
	for (const FSeinSnapshotLatentActionRecord& Record :
		Cache.LatentActions.OrderedRecords)
	{
		if (!ContinuationWriter.WriteGuid(Record.RecordDigest))
		{
			return Fail(OutError, ContinuationWriter.GetError());
		}
	}
	FGuid ContinuationDigest;
	if (!ContinuationWriter.Finalize(
		ContinuationDigest, OutError))
	{
		return false;
	}

	TArray<FSeinCanonicalStateRootLeaf> Leaves;
	Leaves.Reserve(
		2
		+ NativeCanonicalStateSchema.GetContributorCount()
		+ CanonicalStateValues.Num());
	FSeinCanonicalStateRootLeaf& CoreLeaf =
		Leaves.AddDefaulted_GetRef();
	CoreLeaf.SectionId = TEXT("routine/core.world");
	CoreLeaf.Role = ESeinSnapshotSectionRole::Authoritative;
	CoreLeaf.SchemaVersion = RoutineCoreAuthoritativeSchemaVersion;
	CoreLeaf.LeafDigest = CoreDigest;
	if (!BuildLeafContract(
		CoreLeaf.SectionId,
		CoreLeaf.Role,
		CoreLeaf.SchemaVersion,
		CoreLeaf.SchemaDigest,
		CoreLeaf.DescriptorDigest,
		OutError))
	{
		return false;
	}
	FSeinCanonicalStateRootLeaf& ContinuationLeaf =
		Leaves.AddDefaulted_GetRef();
	ContinuationLeaf.SectionId = TEXT("routine/core.continuation");
	ContinuationLeaf.Role = ESeinSnapshotSectionRole::Continuation;
	ContinuationLeaf.SchemaVersion =
		RoutineCoreContinuationSchemaVersion;
	ContinuationLeaf.LeafDigest = ContinuationDigest;
	if (!BuildLeafContract(
		ContinuationLeaf.SectionId,
		ContinuationLeaf.Role,
		ContinuationLeaf.SchemaVersion,
		ContinuationLeaf.SchemaDigest,
		ContinuationLeaf.DescriptorDigest,
		OutError))
	{
		return false;
	}

	TArray<FSeinCanonicalStateRoutineRootRecord> NativeRecords;
	int32 SynchronousFallbackCount = 0;
	if (!FSeinCanonicalStateRegistry::CaptureRoutineRootRecords(
			NativeCanonicalStateSchema,
			{ *this, CompletedTick },
			bForceFullRebuild,
			NativeRecords,
			SynchronousFallbackCount,
			OutError)
		|| SynchronousFallbackCount != 0)
	{
		if (OutError.IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("Routine canonical root refused %d synchronous contributor fallback(s)."),
				SynchronousFallbackCount);
		}
		return false;
	}
	TArray<const FSeinFrozenCanonicalStateContributor*>
		NativeLeafContributors;
	for (const FSeinFrozenCanonicalStateContributor& Contributor :
		NativeCanonicalStateSchema.GetContributors())
	{
		if (Contributor.Descriptor.Role
			!= ESeinCanonicalStateRole::DerivedCache)
		{
			NativeLeafContributors.Add(&Contributor);
		}
	}
	NativeLeafContributors.Sort(
		[](const FSeinFrozenCanonicalStateContributor& A,
			const FSeinFrozenCanonicalStateContributor& B)
		{
			return FSeinCanonicalStateRegistry::CanonicalKey(
				A.Descriptor.Key)
				< FSeinCanonicalStateRegistry::CanonicalKey(
					B.Descriptor.Key);
		});
	if (NativeLeafContributors.Num() != NativeRecords.Num())
	{
		return Fail(
			OutError,
			TEXT("Routine native contributor set is incomplete."));
	}
	auto ResolveRoutineSchema =
		[&Cache, &OutError](
			const UScriptStruct* Type,
			FGuid& OutSchema) -> bool
		{
			if (!Type)
			{
				return Fail(OutError, TEXT("Routine leaf has no payload type."));
			}
			if (const FGuid* Found =
				Cache.RoutineSchemaDigests.Find(Type))
			{
				OutSchema = *Found;
				return true;
			}
			if (!FSeinCanonicalStateCodec::ComputeSchemaDigest(
				Type, OutSchema, OutError))
			{
				return false;
			}
			Cache.RoutineSchemaDigests.Add(Type, OutSchema);
			return true;
		};
	for (int32 Index = 0; Index < NativeRecords.Num(); ++Index)
	{
		const FSeinFrozenCanonicalStateContributor& Contributor =
			*NativeLeafContributors[Index];
		const FSeinCanonicalStateRoutineRootRecord& Record =
			NativeRecords[Index];
		if (Record.Key != Contributor.Descriptor.Key)
		{
			return Fail(
				OutError,
				TEXT("Routine native records do not match the frozen key set."));
		}
		FSeinCanonicalStateRootLeaf& Leaf =
			Leaves.AddDefaulted_GetRef();
		Leaf.SectionId =
			TEXT("routine/native/")
			+ FSeinCanonicalStateRegistry::CanonicalKey(Record.Key);
		Leaf.Role = ToSnapshotRole(Contributor.Descriptor.Role);
		Leaf.SchemaVersion = Contributor.Descriptor.SchemaVersion;
		Leaf.DescriptorDigest = Record.DescriptorDigest;
		Leaf.PayloadBytes = Record.ProjectedPayloadBytes;
		Leaf.LeafDigest = Record.LeafDigest;
		if (!ResolveRoutineSchema(
			Contributor.Descriptor.PayloadStruct,
			Leaf.SchemaDigest))
		{
			return false;
		}
	}

	TArray<const FSeinCanonicalStateValueStore::FSlot*> ValueSlots;
	ValueSlots.Reserve(CanonicalStateValues.Slots.Num());
	for (const auto& Pair : CanonicalStateValues.Slots)
	{
		ValueSlots.Add(&Pair.Value);
	}
	ValueSlots.Sort(
		[](const FSeinCanonicalStateValueStore::FSlot& A,
			const FSeinCanonicalStateValueStore::FSlot& B)
		{
			return FSeinCanonicalStateRegistry::CanonicalKey(
				A.Descriptor.Key)
				< FSeinCanonicalStateRegistry::CanonicalKey(
					B.Descriptor.Key);
		});
	for (const FSeinCanonicalStateValueStore::FSlot* Slot : ValueSlots)
	{
		if (!Slot || !Slot->LeafDigest.IsValid())
		{
			return Fail(
				OutError,
				TEXT("Routine Blueprint state slot has no cached digest."));
		}
		FSeinCanonicalStateRootLeaf& Leaf =
			Leaves.AddDefaulted_GetRef();
		Leaf.SectionId =
			TEXT("routine/blueprint/")
			+ FSeinCanonicalStateRegistry::CanonicalKey(
				Slot->Descriptor.Key);
		Leaf.Role = ESeinSnapshotSectionRole::Authoritative;
		Leaf.SchemaVersion = Slot->Descriptor.SchemaVersion;
		Leaf.DescriptorDigest = Slot->DescriptorDigest;
		Leaf.PayloadBytes =
			static_cast<uint64>(Slot->PayloadBytes.Num());
		Leaf.LeafDigest = Slot->LeafDigest;
		if (!ResolveRoutineSchema(
			Slot->Descriptor.PayloadStruct,
			Leaf.SchemaDigest))
		{
			return false;
		}
	}

	FSeinCanonicalStateRootIdentity Identity;
	Identity.Tick = CompletedTick;
	Identity.CommandProtocolDigest = CommandProtocolDigest;
	Identity.CompatibilityDigest = CompatibilityDigest;
	FGuid CandidateRoot;
	if (!FSeinCanonicalStateRootComposer::Compose(
		Identity, Leaves, CandidateRoot, OutError))
	{
		return false;
	}
	Cache.SealedRoutineTick = CompletedTick;
	Cache.SealedRoutineRoot = CandidateRoot;
	Cache.SealedRoutineError.Reset();
	OutRoot = CandidateRoot;
	if (CVarSeinStateRootProfile.GetValueOnGameThread() >= 2)
	{
		UE_LOG(
			LogSeinSim,
			Display,
			TEXT("Routine canonical root: tick=%d total=%.3f ms component_values=%lld pool_objects=%lld entity_slots=%lld latent_actions=%lld native_contributors=%d"),
			CompletedTick,
			(FPlatformTime::Seconds() - RoutineProfileStart) * 1000.0,
			Cache.ComponentValueDigestsComputed - ComponentDigestsBefore,
			Cache.PoolObjectsCaptured - PoolCapturesBefore,
			Cache.EntitySlotsProjected - EntityProjectionsBefore,
			Cache.LatentActionsCaptured - LatentCapturesBefore,
			NativeRecords.Num());
	}
	return true;
}

bool USeinWorldSubsystem::GetSealedRoutineCanonicalStateRoot(
	int32 ExpectedCompletedTick,
	FGuid& OutRoot,
	FString& OutError) const
{
	OutRoot.Invalidate();
	OutError.Reset();
	if (!CanonicalStateRootCache.IsValid()
		|| CanonicalStateRootCache->SealedRoutineTick
			!= ExpectedCompletedTick
		|| !CanonicalStateRootCache->SealedRoutineRoot.IsValid())
	{
		OutError = FString::Printf(
			TEXT("No routine canonical root is sealed for completed tick %d."),
			ExpectedCompletedTick);
		return false;
	}
	OutRoot = CanonicalStateRootCache->SealedRoutineRoot;
	return true;
}

bool USeinWorldSubsystem::VerifyIncrementalCanonicalStateRoot(
	FGuid& OutRoot,
	FString& OutError) const
{
	OutRoot.Invalidate();
	OutError.Reset();
	FGuid IncrementalRoot;
	if (!SealRoutineCanonicalStateRoot(
			CurrentTick,
			/*bForceFullRebuild=*/false,
			IncrementalRoot,
			OutError))
	{
		return false;
	}
	FGuid RebuiltRoot;
	if (!SealRoutineCanonicalStateRoot(
			CurrentTick,
			/*bForceFullRebuild=*/true,
			RebuiltRoot,
			OutError))
	{
		return false;
	}
	if (IncrementalRoot != RebuiltRoot)
	{
		OutError = FString::Printf(
			TEXT("Incremental canonical root %s disagrees with forced rebuild %s at tick %d."),
			*IncrementalRoot.ToString(EGuidFormats::Digits),
			*RebuiltRoot.ToString(EGuidFormats::Digits),
			CurrentTick);
		return false;
	}
	OutRoot = RebuiltRoot;
	return true;
}

bool USeinWorldSubsystem::ComputeCanonicalStateRoot(
	FGuid& OutRoot,
	FString& OutError) const
{
	const double ProfileStart = FPlatformTime::Seconds();
	double ProfileLast = ProfileStart;
	double ProfileLatentMs = 0.0;
	double ProfileCorePreludeMs = 0.0;
	double ProfileEntityPoolMs = 0.0;
	double ProfileComponentsMs = 0.0;
	double ProfileObjectPoolsMs = 0.0;
	double ProfileCoreTailMs = 0.0;
	double ProfileContinuationMs = 0.0;
	double ProfileContributorsMs = 0.0;
	const auto ProfileMark = [&ProfileLast](double& OutMilliseconds)
	{
		const double Now = FPlatformTime::Seconds();
		OutMilliseconds = (Now - ProfileLast) * 1000.0;
		ProfileLast = Now;
	};

	OutError.Reset();
	if (!IsInGameThread())
	{
		return Fail(
			OutError,
			TEXT("Canonical world-state capture requires the game thread."));
	}
	if (SeinIsInSimContext(this)
		|| OwnerTransitionDepth != 0
		|| bSnapshotCaptureInProgress
		|| bSnapshotRestoreInProgress
		|| bDestroyNotificationInProgress
		|| DeferredTeardownHandle.IsValid()
		|| bDispatchingPauseControlFrame
		|| bPauseControlDispatchProtocolViolation
		|| ActivePauseControlCommandIndex != INDEX_NONE
		|| ActivePauseControlCommandCount != 0)
	{
		return Fail(
			OutError,
			TEXT("Canonical world-state capture refused an in-flight simulation, ownership, snapshot, destroy, or pause-control transaction."));
	}
	if (!ValidatePairCapabilityState())
	{
		return Fail(
			OutError,
			TEXT("Canonical world-state capture refused inconsistent pair-capability source records or effective cache."));
	}
	FString ContainmentError;
	if (!ValidateContainmentState(ContainmentError))
	{
		return Fail(
			OutError,
			FString::Printf(
				TEXT("Canonical world-state capture refused invalid containment state (%s)."),
				*ContainmentError));
	}
	const bool bCanonicalTimelineReady =
		bIsRunning
		|| (bSimulationSchedulerReserved && TickerHandle.IsValid());
	if (!bCanonicalTimelineReady
		|| MatchBootstrapState != ESeinMatchBootstrapState::Consumed
		|| !MatchBootstrapReceipt.IsValid()
		|| !MatchBootstrapAuthorizationContextDigest.IsValid()
		|| !CommandProtocolDigest.IsValid()
		|| !SimulationContentDigest.IsValid()
		|| !MatchSettingsDigest.IsValid()
		|| !NativeCanonicalStateSchema.IsValid()
		|| !CanonicalStateValues.IsSealed()
		|| CanonicalStateValues.GetContractDigest()
			!= MatchBootstrapReceipt.StateContractDigest)
	{
		return Fail(
			OutError,
			TEXT("Canonical world-state capture requires an active or dormant-ready consumed bootstrap with frozen protocol, content, settings, and state contracts."));
	}
	if (bReplayOwnsExternalCommandIngress)
	{
		return Fail(
			OutError,
			TEXT("Canonical world-state capture requires replay command ingress to stop until replay continuation ownership is checkpointable."));
	}

	const int64 NextLatentActionID = LatentActionManager
		? LatentActionManager->GetNextActionID()
		: 1;
	TArray<FSeinSnapshotLatentActionRecord> LatentRecords;
	FGuid LatentSequenceDigest;
	if (!FSeinLatentActionCodecRegistry::CaptureRecords(
		LatentActionCodecManifest,
		*this,
		LatentActionManager,
		CurrentTick,
		NextLatentActionID,
		NextAbilityActivationID,
		LatentRecords,
		LatentSequenceDigest,
		OutError))
	{
		return false;
	}
	ProfileMark(ProfileLatentMs);

	FSeinCanonicalDigestWriter CompatibilityWriter(
		TEXT("SeinARTS.LiveWorld.Compatibility"), 1);
	FGuid CompatibilityDigest;
	if (!CompatibilityWriter.WriteInt32(
			MatchBootstrapReceipt.FormatVersion)
		|| !CompatibilityWriter.WriteGuid(
			MatchBootstrapReceipt.ContractDigest)
		|| !CompatibilityWriter.WriteGuid(
			MatchBootstrapReceipt.SimulationContentDigest)
		|| !CompatibilityWriter.WriteGuid(
			MatchBootstrapReceipt.StateContractDigest)
		|| !CompatibilityWriter.WriteGuid(
			MatchBootstrapReceipt.PlanDigest)
		|| !CompatibilityWriter.WriteGuid(
			MatchBootstrapReceipt.InitialStateDigest)
		|| !CompatibilityWriter.WriteGuid(
			MatchBootstrapAuthorizationContextDigest)
		|| !CompatibilityWriter.WriteInt32(ConfigFingerprint)
		|| !CompatibilityWriter.Finalize(
			CompatibilityDigest, OutError))
	{
		return false;
	}

	FReflectedDigestContext Reflected(OutError);
	FSeinCanonicalDigestWriter CoreWriter(
		TEXT("SeinARTS.LiveWorld.Core.Authoritative"),
		CoreAuthoritativeSchemaVersion);
	if (!CoreWriter.WriteInt32(CurrentTick)
		|| !CoreWriter.WriteInt64(SimSessionSeed)
		|| !CoreWriter.WriteUInt64(SimRandom.State0)
		|| !CoreWriter.WriteUInt64(SimRandom.State1)
		|| !CoreWriter.WriteInt64(NextEffectInstanceID)
		|| !CoreWriter.WriteInt64(NextLatentActionID)
		|| !CoreWriter.WriteInt64(NextAbilityActivationID)
		|| !CoreWriter.WriteUInt8(static_cast<uint8>(MatchState))
		|| !CoreWriter.WriteInt32(StartingStateDeadlineTick)
		|| !CoreWriter.WriteInt32(MatchStartTick)
		|| !CoreWriter.WriteBool(bSimPaused)
		|| !CoreWriter.WriteBool(bSimPausedHard)
		|| !CoreWriter.WriteInt64(PauseEpoch)
		|| !CoreWriter.WriteInt32(PauseFrozenTick)
		|| !CoreWriter.WriteInt64(LastAppliedPauseControlSequence)
		|| !CoreWriter.WriteGuid(MatchSettingsDigest)
		|| !CoreWriter.WriteGuid(SimulationContentDigest)
		|| !CoreWriter.WriteInt32(ConfigFingerprint))
	{
		return Fail(OutError, CoreWriter.GetError());
	}

	FGuid MatchSettingsSchema;
	FGuid MatchSettingsValue;
	if (!Reflected.DigestStruct(
		FSeinMatchSettings::StaticStruct(),
		&CurrentMatchSettings,
		TEXT("Match settings"),
		MatchSettingsSchema,
		MatchSettingsValue)
		|| !CoreWriter.WriteGuid(MatchSettingsSchema)
		|| !CoreWriter.WriteGuid(MatchSettingsValue))
	{
		return false;
	}

	TArray<FSeinPlayerID> PlayerIds;
	PlayerStates.GetKeys(PlayerIds);
	PlayerIds.Sort();
	if (!CoreWriter.WriteUInt32(static_cast<uint32>(PlayerIds.Num())))
	{
		return Fail(OutError, CoreWriter.GetError());
	}
	for (const FSeinPlayerID PlayerId : PlayerIds)
	{
		const FSeinPlayerState& State = PlayerStates.FindChecked(PlayerId);
		FGuid SchemaDigest;
		FGuid ValueDigest;
		if (State.PlayerID != PlayerId
			|| !Reflected.DigestStruct(
				FSeinPlayerState::StaticStruct(),
				&State,
				FString::Printf(
					TEXT("Player state %u"), PlayerId.Value),
				SchemaDigest,
				ValueDigest)
			|| !CoreWriter.WriteUInt8(PlayerId.Value)
			|| !CoreWriter.WriteGuid(SchemaDigest)
			|| !CoreWriter.WriteGuid(ValueDigest))
		{
			if (OutError.IsEmpty() && State.PlayerID != PlayerId)
			{
				OutError =
					TEXT("Player registry key and payload identity disagree.");
			}
			return false;
		}
	}
	if (!WritePairCapabilityGrants(
			CoreWriter, GetPairCapabilityGrantRecords()))
	{
		return Fail(OutError, CoreWriter.GetError().IsEmpty()
			? TEXT("Core pair-capability encoding failed.")
			: CoreWriter.GetError());
	}
	ProfileMark(ProfileCorePreludeMs);

	FSeinEntityPoolExactState ExactPool;
	FString PoolError;
	if (!EntityPool.CaptureExactState(
		ExactPool,
		PoolError,
		/*bAllowDeferredDestroyTombstones=*/true))
	{
		return Fail(
			OutError,
			FString::Printf(
				TEXT("Entity-pool capture failed: %s"), *PoolError));
	}
	FGuid EntityPoolSchema;
	FGuid EntityPoolValue;
	if (!Reflected.ResolveSchema(
			FSeinEntityPoolExactState::StaticStruct(),
			TEXT("Entity pool"),
			EntityPoolSchema)
		|| !ComputeEntityPoolExactValueDigest(
			ExactPool, EntityPoolValue, OutError)
		|| !CoreWriter.WriteInt32(EntityPool.GetActiveCount())
		|| !CoreWriter.WriteGuid(EntityPoolSchema)
		|| !CoreWriter.WriteGuid(EntityPoolValue))
	{
		return false;
	}

	TArray<FSeinEntityHandle> Entities;
	EntityPool.ForEachEntity(
		[&Entities](const FSeinEntityHandle Handle, const FSeinEntity&)
		{
			Entities.Add(Handle);
		});
	Entities.Sort();
	if (Entities.Num() != EntityPool.GetActiveCount()
		|| !CoreWriter.WriteUInt32(static_cast<uint32>(Entities.Num())))
	{
		return Fail(
			OutError,
			TEXT("Entity pool active-count/topology mismatch."));
	}
	for (const FSeinEntityHandle Handle : Entities)
	{
		const TSubclassOf<ASeinActor>* ActorClass =
			EntityActorClassMap.Find(Handle);
		if (!WriteHandle(CoreWriter, Handle)
			|| !CoreWriter.WriteBool(ActorClass != nullptr)
			|| (ActorClass
				&& !WriteClassPath(CoreWriter, ActorClass->Get())))
		{
			return Fail(OutError, CoreWriter.GetError());
		}
	}
	for (const auto& Pair : EntityActorClassMap)
	{
		if (!EntityPool.IsValid(Pair.Key))
		{
			return Fail(
				OutError,
				TEXT("Entity actor-class registry contains a stale handle."));
		}
	}
	ProfileMark(ProfileEntityPoolMs);

	TArray<UScriptStruct*> ComponentTypes;
	ComponentStorages.GetKeys(ComponentTypes);
	ComponentTypes.Sort([](const UScriptStruct& A, const UScriptStruct& B)
	{
		return A.GetPathName() < B.GetPathName();
	});
	if (!CoreWriter.WriteUInt32(
		static_cast<uint32>(ComponentTypes.Num())))
	{
		return Fail(OutError, CoreWriter.GetError());
	}
	struct FComponentDigestWork
	{
		UScriptStruct* Type = nullptr;
		FString TypePath;
		FGuid SchemaDigest;
		TArray<FSeinEntityHandle> Occupied;
		TArray<const void*> OrderedValues;
		FGuid StorageDigest;
		FString Error;
	};

	TArray<FComponentDigestWork> ComponentWork;
	ComponentWork.SetNum(ComponentTypes.Num());
	for (int32 TypeIndex = 0;
		TypeIndex < ComponentTypes.Num();
		++TypeIndex)
	{
		UScriptStruct* ComponentType = ComponentTypes[TypeIndex];
		const ISeinComponentStorage* Storage = ComponentType
			? ComponentStorages.FindChecked(ComponentType)
			: nullptr;
		FComponentDigestWork& Work = ComponentWork[TypeIndex];
		Work.Type = ComponentType;
		Work.TypePath = ComponentType
			? ComponentType->GetPathName()
			: FString();
		if (!ComponentType || !Storage
			|| !Reflected.ResolveSchema(
				ComponentType,
				TEXT("Component storage"),
				Work.SchemaDigest))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Component registry contains a null type or storage.");
			}
			return false;
		}

		for (const FSeinEntityHandle Handle : Entities)
		{
			if (Storage->HasComponent(Handle))
			{
				Work.Occupied.Add(Handle);
			}
		}
		if (Work.Occupied.Num() != Storage->GetComponentCount())
		{
			return Fail(
				OutError,
				FString::Printf(
					TEXT("Component storage '%s' contains an orphaned or stale slot."),
					*Work.TypePath));
		}

		Work.OrderedValues.Reserve(Work.Occupied.Num());
		for (const FSeinEntityHandle Handle : Work.Occupied)
		{
			const void* Value = Storage->GetComponentRaw(Handle);
			if (!Value)
			{
				return Fail(
					OutError,
					FString::Printf(
						TEXT("Component storage '%s' lost an occupied value."),
						*Work.TypePath));
			}
			Work.OrderedValues.Add(Value);
		}
	}

	const auto DigestComponentStorage = [&ComponentWork](const int32 Index)
	{
		FComponentDigestWork& Work = ComponentWork[Index];
		FSeinCanonicalDigestWriter StorageWriter(
			TEXT("SeinARTS.LiveWorld.ComponentStorage"), 1);
		if (!StorageWriter.WriteString(Work.TypePath)
			|| !StorageWriter.WriteGuid(Work.SchemaDigest)
			|| !StorageWriter.WriteUInt32(
				static_cast<uint32>(Work.Occupied.Num())))
		{
			Work.Error = StorageWriter.GetError();
			return;
		}
		for (const FSeinEntityHandle Handle : Work.Occupied)
		{
			if (!WriteHandle(StorageWriter, Handle))
			{
				Work.Error = StorageWriter.GetError();
				return;
			}
		}

		FGuid SequenceDigest;
		FString SequenceError;
		FSeinCanonicalReflectedStateLimits Limits;
		if (!FSeinCanonicalReflectedStateDigest::
				ComputeStructSequenceValueDigest(
					Work.Type,
					Work.OrderedValues,
					Work.SchemaDigest,
					Limits,
					SequenceDigest,
					SequenceError)
			|| !StorageWriter.WriteGuid(SequenceDigest)
			|| !StorageWriter.Finalize(
				Work.StorageDigest, Work.Error))
		{
			if (Work.Error.IsEmpty())
			{
				Work.Error = MoveTemp(SequenceError);
			}
		}
	};

	// Component storages and pooled objects are independent immutable reads at
	// this boundary. Dispatch each storage without waiting, capture the pool
	// contract concurrently, then merge both results canonically on the game
	// thread. This removes a serial phase barrier without changing evidence.
	const double ComponentWindowStart = FPlatformTime::Seconds();
	FGraphEventArray ComponentTasks;
	const bool bParallelRoot =
		ComponentWork.Num() > 1 && SeinSimParallelEnabled();
	if (bParallelRoot)
	{
		ComponentTasks.Reserve(ComponentWork.Num());
		for (int32 Index = 0; Index < ComponentWork.Num(); ++Index)
		{
			ComponentTasks.Add(
				FFunctionGraphTask::CreateAndDispatchWhenReady(
					[&DigestComponentStorage, Index]()
					{
#if !UE_BUILD_SHIPPING
						SeinSetInParallelSection(true);
#endif
						DigestComponentStorage(Index);
#if !UE_BUILD_SHIPPING
						SeinSetInParallelSection(false);
#endif
					},
					TStatId(),
					nullptr,
					ENamedThreads::AnyBackgroundThreadNormalTask));
		}
	}
	else
	{
		for (int32 Index = 0; Index < ComponentWork.Num(); ++Index)
		{
			DigestComponentStorage(Index);
		}
	}

	const double PoolStart = FPlatformTime::Seconds();
	FSeinCanonicalDigestWriter PoolWriter(
		TEXT("SeinARTS.LiveWorld.ObjectPools"), 1);
	FGuid ObjectPoolsDigest;
	bool bPoolsOK =
		PoolObjectCodecManifest.VerifyProviderLeases(OutError);
	TSet<const UObject*> SeenPoolObjects;
	if (bPoolsOK)
	{
		bPoolsOK = WriteObjectPool(
			PoolWriter,
			TEXT("AbilityPool"),
			AbilityPool,
			AbilityPoolFreeList,
			PoolObjectCodecManifest,
			ESeinPoolObjectKind::Ability,
			SeenPoolObjects,
			OutError)
			&& WriteObjectPool(
				PoolWriter,
				TEXT("CommandBrokerResolverPool"),
				CommandBrokerResolverPool,
				CommandBrokerResolverPoolFreeList,
				PoolObjectCodecManifest,
				ESeinPoolObjectKind::CommandBrokerResolver,
				SeenPoolObjects,
				OutError)
			&& PoolWriter.Finalize(ObjectPoolsDigest, OutError);
	}
	ProfileObjectPoolsMs =
		(FPlatformTime::Seconds() - PoolStart) * 1000.0;

	if (!ComponentTasks.IsEmpty())
	{
		FTaskGraphInterface::Get().WaitUntilTasksComplete(
			ComponentTasks, ENamedThreads::GameThread);
	}
	ProfileComponentsMs =
		(FPlatformTime::Seconds() - ComponentWindowStart) * 1000.0;
	ProfileLast = FPlatformTime::Seconds();

	for (const FComponentDigestWork& Work : ComponentWork)
	{
		if (!Work.Error.IsEmpty()
			|| !Work.StorageDigest.IsValid()
			|| !CoreWriter.WriteString(Work.TypePath)
			|| !CoreWriter.WriteGuid(Work.SchemaDigest)
			|| !CoreWriter.WriteUInt32(
				static_cast<uint32>(Work.Occupied.Num()))
			|| !CoreWriter.WriteGuid(Work.StorageDigest))
		{
			return Fail(
				OutError,
				Work.Error.IsEmpty()
					? CoreWriter.GetError()
					: FString::Printf(
						TEXT("Component storage '%s' is not canonical: %s"),
						*Work.TypePath,
						*Work.Error));
		}
	}
	if (!bPoolsOK
		|| !ObjectPoolsDigest.IsValid()
		|| !CoreWriter.WriteGuid(ObjectPoolsDigest))
	{
		return OutError.IsEmpty()
			? Fail(OutError, CoreWriter.GetError())
			: false;
	}

	TArray<FSeinEntityHandle> TagStateHandles;
	EntityTagStates.GetKeys(TagStateHandles);
	TagStateHandles.Sort();
	if (!CoreWriter.WriteUInt32(
		static_cast<uint32>(TagStateHandles.Num())))
	{
		return Fail(OutError, CoreWriter.GetError());
	}
	for (const FSeinEntityHandle Handle : TagStateHandles)
	{
		const FSeinEntityTagState& State =
			EntityTagStates.FindChecked(Handle);
		if (!EntityPool.IsValid(Handle)
			|| !WriteHandle(CoreWriter, Handle)
			|| !WriteTagContainer(CoreWriter, State.BaseTags))
		{
			return Fail(
				OutError,
				TEXT("Entity-tag registry contains a stale handle or invalid value."));
		}

		TArray<FGameplayTag> RefCountTags;
		State.TagRefCounts.GetKeys(RefCountTags);
		RefCountTags.Sort([](
			const FGameplayTag A, const FGameplayTag B)
		{
			return A.ToString() < B.ToString();
		});
		if (!CoreWriter.WriteUInt32(
			static_cast<uint32>(RefCountTags.Num())))
		{
			return Fail(OutError, CoreWriter.GetError());
		}
		for (const FGameplayTag Tag : RefCountTags)
		{
			if (!WriteGameplayTag(CoreWriter, Tag)
				|| !CoreWriter.WriteInt32(
					State.TagRefCounts.FindChecked(Tag)))
			{
				return Fail(OutError, CoreWriter.GetError());
			}
		}
		if (!WriteTagContainer(CoreWriter, State.CombinedTags))
		{
			return Fail(OutError, CoreWriter.GetError());
		}
	}

	TArray<FGameplayTag> IndexedTags;
	EntityTagIndex.GetKeys(IndexedTags);
	IndexedTags.Sort([](
		const FGameplayTag A, const FGameplayTag B)
	{
		return A.ToString() < B.ToString();
	});
	if (!CoreWriter.WriteUInt32(static_cast<uint32>(IndexedTags.Num())))
	{
		return Fail(OutError, CoreWriter.GetError());
	}
	for (const FGameplayTag Tag : IndexedTags)
	{
		const TArray<FSeinEntityHandle>& Bucket =
			EntityTagIndex.FindChecked(Tag);
		if (!WriteGameplayTag(CoreWriter, Tag)
			|| !CoreWriter.WriteUInt32(
				static_cast<uint32>(Bucket.Num())))
		{
			return Fail(OutError, CoreWriter.GetError());
		}
		for (const FSeinEntityHandle Handle : Bucket)
		{
			if (!EntityPool.IsValid(Handle)
				|| !WriteHandle(CoreWriter, Handle))
			{
				return Fail(
					OutError,
					TEXT("Global tag index contains a stale handle."));
			}
		}
	}

	TArray<FName> EntityNames;
	NamedEntityRegistry.GetKeys(EntityNames);
	EntityNames.Sort([](const FName A, const FName B)
	{
		return CanonicalName(A) < CanonicalName(B);
	});
	if (!CoreWriter.WriteUInt32(static_cast<uint32>(EntityNames.Num())))
	{
		return Fail(OutError, CoreWriter.GetError());
	}
	for (const FName Name : EntityNames)
	{
		const FSeinEntityHandle Handle =
			NamedEntityRegistry.FindChecked(Name);
		if (!EntityPool.IsValid(Handle)
			|| !CoreWriter.WriteName(Name)
			|| !WriteHandle(CoreWriter, Handle))
		{
			return Fail(
				OutError,
				TEXT("Named-entity registry contains a stale handle."));
		}
	}

	TArray<FGameplayTag> VoteTypes;
	ActiveVotes.GetKeys(VoteTypes);
	VoteTypes.Sort([](const FGameplayTag A, const FGameplayTag B)
	{
		return A.ToString() < B.ToString();
	});
	if (!CoreWriter.WriteUInt32(static_cast<uint32>(VoteTypes.Num())))
	{
		return Fail(OutError, CoreWriter.GetError());
	}
	for (const FGameplayTag VoteType : VoteTypes)
	{
		const FSeinVoteState& Vote = ActiveVotes.FindChecked(VoteType);
		FGuid SchemaDigest;
		FGuid ValueDigest;
		if (!Reflected.DigestStruct(
			FSeinVoteState::StaticStruct(),
			&Vote,
			FString::Printf(
				TEXT("Vote '%s'"), *VoteType.ToString()),
			SchemaDigest,
			ValueDigest)
			|| !WriteGameplayTag(CoreWriter, VoteType)
			|| !CoreWriter.WriteGuid(SchemaDigest)
			|| !CoreWriter.WriteGuid(ValueDigest))
		{
			return false;
		}
	}

	TArray<FSeinFactionID> FactionIds;
	Factions.GetKeys(FactionIds);
	FactionIds.Sort();
	if (!CoreWriter.WriteUInt32(static_cast<uint32>(FactionIds.Num())))
	{
		return Fail(OutError, CoreWriter.GetError());
	}
	for (const FSeinFactionID FactionId : FactionIds)
	{
		const USeinFaction* Faction =
			Factions.FindChecked(FactionId).Get();
		FGuid SchemaDigest;
		FGuid ValueDigest;
		if (!Faction
			|| Faction->FactionID != FactionId
			|| !Reflected.DigestObject(
				Faction,
				FString::Printf(
					TEXT("Faction %u"), FactionId.Value),
				SchemaDigest,
				ValueDigest)
			|| !CoreWriter.WriteUInt8(FactionId.Value)
			|| !CoreWriter.WriteString(Faction->GetPathName())
			|| !WriteClassPath(CoreWriter, Faction->GetClass())
			|| !CoreWriter.WriteGuid(SchemaDigest)
			|| !CoreWriter.WriteGuid(ValueDigest))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Faction registry contains an invalid or mismatched entry.");
			}
			return false;
		}
	}

	FGuid CoreDigest;
	if (!CoreWriter.Finalize(CoreDigest, OutError))
	{
		return false;
	}
	ProfileMark(ProfileCoreTailMs);

	FSeinCanonicalDigestWriter ContinuationWriter(
		TEXT("SeinARTS.LiveWorld.Core.Continuation"),
		CoreContinuationSchemaVersion);
	if (!WriteCommandQueue(
		ContinuationWriter,
		TEXT("pending"),
		PendingCommands.GetCommands(),
		Reflected)
		|| !WriteCommandQueue(
			ContinuationWriter,
			TEXT("replay"),
			PendingReplayCommands.GetCommands(),
			Reflected)
		|| !WriteCommandQueue(
			ContinuationWriter,
			TEXT("pause"),
			PendingStandalonePauseControlCommands,
			Reflected)
		|| !ContinuationWriter.WriteUInt32(
			static_cast<uint32>(PendingDestroy.Num())))
	{
		return OutError.IsEmpty()
			? Fail(OutError, ContinuationWriter.GetError())
			: false;
	}
	for (const FSeinEntityHandle Handle : PendingDestroy)
	{
		if (!WriteHandle(ContinuationWriter, Handle))
		{
			return Fail(OutError, ContinuationWriter.GetError());
		}
	}

	if (!ContinuationWriter.WriteUInt32(
		static_cast<uint32>(PendingEffectApplies.Num())))
	{
		return Fail(OutError, ContinuationWriter.GetError());
	}
	for (int32 Index = 0; Index < PendingEffectApplies.Num(); ++Index)
	{
		const FSeinPendingEffectApply& Pending =
			PendingEffectApplies[Index];
		if (!Pending.EffectClass
			|| !WriteHandle(ContinuationWriter, Pending.Target)
			|| !WriteClassPath(
				ContinuationWriter, Pending.EffectClass.Get())
			|| !WriteHandle(ContinuationWriter, Pending.Source))
		{
			return Fail(
				OutError,
				FString::Printf(
					TEXT("Pending effect apply %d has an invalid class or value."),
					Index));
		}
	}

	if (!ContinuationWriter.WriteUInt32(
			static_cast<uint32>(LatentRecords.Num()))
		|| !ContinuationWriter.WriteGuid(LatentSequenceDigest))
	{
		return Fail(OutError, ContinuationWriter.GetError());
	}
	for (const FSeinSnapshotLatentActionRecord& Record : LatentRecords)
	{
		if (!ContinuationWriter.WriteGuid(Record.RecordDigest))
		{
			return Fail(OutError, ContinuationWriter.GetError());
		}
	}

	FGuid ContinuationDigest;
	if (!ContinuationWriter.Finalize(
		ContinuationDigest, OutError))
	{
		return false;
	}
	ProfileMark(ProfileContinuationMs);

	TArray<FSeinCanonicalStateRootLeaf> Leaves;
	Leaves.Reserve(
		2
		+ NativeCanonicalStateSchema.GetContributorCount()
		+ CanonicalStateValues.Num());

	FSeinCanonicalStateRootLeaf& CoreLeaf =
		Leaves.AddDefaulted_GetRef();
	CoreLeaf.SectionId = TEXT("core.world");
	CoreLeaf.Role = ESeinSnapshotSectionRole::Authoritative;
	CoreLeaf.SchemaVersion = CoreAuthoritativeSchemaVersion;
	CoreLeaf.LeafDigest = CoreDigest;
	if (!BuildLeafContract(
		CoreLeaf.SectionId,
		CoreLeaf.Role,
		CoreLeaf.SchemaVersion,
		CoreLeaf.SchemaDigest,
		CoreLeaf.DescriptorDigest,
		OutError))
	{
		return false;
	}

	FSeinCanonicalStateRootLeaf& ContinuationLeaf =
		Leaves.AddDefaulted_GetRef();
	ContinuationLeaf.SectionId = TEXT("core.continuation");
	ContinuationLeaf.Role =
		ESeinSnapshotSectionRole::Continuation;
	ContinuationLeaf.SchemaVersion =
		CoreContinuationSchemaVersion;
	ContinuationLeaf.LeafDigest = ContinuationDigest;
	if (!BuildLeafContract(
		ContinuationLeaf.SectionId,
		ContinuationLeaf.Role,
		ContinuationLeaf.SchemaVersion,
		ContinuationLeaf.SchemaDigest,
		ContinuationLeaf.DescriptorDigest,
		OutError))
	{
		return false;
	}

	TArray<FSeinCanonicalStateContributorRecord> NativeRecords;
	if (!FSeinCanonicalStateRegistry::CaptureContributorRecords(
		NativeCanonicalStateSchema,
		{ *this, CurrentTick },
		NativeRecords,
		OutError))
	{
		return false;
	}
	TArray<const FSeinFrozenCanonicalStateContributor*>
		NativeLeafContributors;
	for (const FSeinFrozenCanonicalStateContributor& Contributor :
		NativeCanonicalStateSchema.GetContributors())
	{
		if (Contributor.Descriptor.Role
			!= ESeinCanonicalStateRole::DerivedCache)
		{
			NativeLeafContributors.Add(&Contributor);
		}
	}
	NativeLeafContributors.Sort(
		[](const FSeinFrozenCanonicalStateContributor& A,
			const FSeinFrozenCanonicalStateContributor& B)
		{
			return FSeinCanonicalStateRegistry::CanonicalKey(
					A.Descriptor.Key)
				< FSeinCanonicalStateRegistry::CanonicalKey(
					B.Descriptor.Key);
		});
	if (NativeLeafContributors.Num() != NativeRecords.Num())
	{
		return Fail(
			OutError,
			TEXT("Native canonical-state record set is incomplete or contains unexpected entries."));
	}
	for (int32 NativeRecordIndex = 0;
		NativeRecordIndex < NativeRecords.Num();
		++NativeRecordIndex)
	{
		const FSeinFrozenCanonicalStateContributor& Contributor =
			*NativeLeafContributors[NativeRecordIndex];
		const FSeinCanonicalStateContributorRecord& Record =
			NativeRecords[NativeRecordIndex];
		if (Record.Key != Contributor.Descriptor.Key)
		{
			return Fail(
				OutError,
				TEXT("Native canonical-state records do not match the frozen canonical key set."));
		}

		FSeinCanonicalStateRootLeaf& Leaf =
			Leaves.AddDefaulted_GetRef();
		Leaf.SectionId =
			TEXT("native/")
			+ FSeinCanonicalStateRegistry::CanonicalKey(Record.Key);
		Leaf.Role = ToSnapshotRole(Contributor.Descriptor.Role);
		Leaf.SchemaVersion =
			Contributor.Descriptor.SchemaVersion;
		Leaf.DescriptorDigest = Record.DescriptorDigest;
		Leaf.PayloadBytes =
			static_cast<uint64>(Record.PayloadBytes.Num());
		Leaf.LeafDigest = Record.LeafDigest;
		if (!FSeinCanonicalStateCodec::ComputeSchemaDigest(
			Contributor.Descriptor.PayloadStruct,
			Leaf.SchemaDigest,
			OutError))
		{
			return false;
		}
	}

	TArray<FSeinCanonicalStateValueRecord> ValueRecords;
	if (!CanonicalStateValues.CaptureRecords(
		ValueRecords, OutError))
	{
		return false;
	}
	for (const FSeinCanonicalStateValueRecord& Record : ValueRecords)
	{
		const FSeinCanonicalStateValueStore::FSlot* Slot =
			CanonicalStateValues.Slots.Find(Record.Key);
		if (!Slot
			|| !Slot->Descriptor.PayloadStruct
			|| Slot->DescriptorDigest != Record.DescriptorDigest)
		{
			return Fail(
				OutError,
				TEXT("Blueprint canonical-state record does not match its frozen local slot."));
		}

		FSeinCanonicalStateRootLeaf& Leaf =
			Leaves.AddDefaulted_GetRef();
		Leaf.SectionId =
			TEXT("blueprint/")
			+ FSeinCanonicalStateRegistry::CanonicalKey(Record.Key);
		Leaf.Role = ESeinSnapshotSectionRole::Authoritative;
		Leaf.SchemaVersion =
			static_cast<uint32>(Record.SchemaVersion);
		Leaf.DescriptorDigest = Record.DescriptorDigest;
		Leaf.PayloadBytes =
			static_cast<uint64>(Record.PayloadBytes.Num());
		Leaf.LeafDigest = Record.LeafDigest;
		if (!FSeinCanonicalStateCodec::ComputeSchemaDigest(
			Slot->Descriptor.PayloadStruct,
			Leaf.SchemaDigest,
			OutError))
		{
			return false;
		}
	}

	FSeinCanonicalStateRootIdentity Identity;
	Identity.Tick = CurrentTick;
	Identity.CommandProtocolDigest = CommandProtocolDigest;
	Identity.CompatibilityDigest = CompatibilityDigest;
	FGuid CandidateRoot;
	if (!FSeinCanonicalStateRootComposer::Compose(
		Identity, Leaves, CandidateRoot, OutError))
	{
		return false;
	}
	ProfileMark(ProfileContributorsMs);
	if (CVarSeinStateRootProfile.GetValueOnGameThread() != 0)
	{
		UE_LOG(
			LogSeinSim,
			Display,
			TEXT("Canonical state root profile: total=%.3f ms latent=%.3f prelude=%.3f entity_pool=%.3f components=%.3f object_pools=%.3f core_tail=%.3f continuation=%.3f contributors=%.3f"),
			(FPlatformTime::Seconds() - ProfileStart) * 1000.0,
			ProfileLatentMs,
			ProfileCorePreludeMs,
			ProfileEntityPoolMs,
			ProfileComponentsMs,
			ProfileObjectPoolsMs,
			ProfileCoreTailMs,
			ProfileContinuationMs,
			ProfileContributorsMs);
	}
	OutRoot = CandidateRoot;
	return true;
}

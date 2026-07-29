/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWorldStateRoot.cpp
 * @brief   Exact fallible live-world root capture.
 */

#include "Simulation/SeinWorldSubsystem.h"

#include "Abilities/SeinAbility.h"
#include "Abilities/SeinLatentAction.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Actor/SeinActor.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "Core/SeinSimContext.h"
#include "Data/SeinFaction.h"
#include "Data/SeinWorldSnapshot.h"
#include "Effects/SeinEffect.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Serialization/SeinCanonicalReflectedStateDigest.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "Serialization/SeinCanonicalStateRoot.h"
#include "Serialization/SeinLatentActionCodecRegistry.h"

namespace
{
	constexpr uint32 CoreAuthoritativeSchemaVersion = 2;
	constexpr uint32 CoreContinuationSchemaVersion = 2;

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
		TSet<const UObject*>& SeenObjects,
		FReflectedDigestContext& Reflected,
		FString& OutError)
	{
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

			FGuid SchemaDigest;
			FGuid ValueDigest;
			if (!Reflected.DigestObject(
				Object,
				FString::Printf(
					TEXT("%s slot %d"), PoolName, Index),
				SchemaDigest,
				ValueDigest)
				|| !WriteClassPath(Writer, Object->GetClass())
				|| !Writer.WriteGuid(SchemaDigest)
				|| !Writer.WriteGuid(ValueDigest))
			{
				return false;
			}
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

bool USeinWorldSubsystem::ComputeCanonicalStateRoot(
	FGuid& OutRoot,
	FString& OutError) const
{
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
	if (!Reflected.DigestStruct(
		FSeinEntityPoolExactState::StaticStruct(),
		&ExactPool,
		TEXT("Entity pool"),
		EntityPoolSchema,
		EntityPoolValue)
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
	for (UScriptStruct* ComponentType : ComponentTypes)
	{
		const ISeinComponentStorage* Storage =
			ComponentStorages.FindChecked(ComponentType);
		FGuid SchemaDigest;
		if (!ComponentType || !Storage
			|| !Reflected.ResolveSchema(
				ComponentType,
				TEXT("Component storage"),
				SchemaDigest))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Component registry contains a null type or storage.");
			}
			return false;
		}

		TArray<FSeinEntityHandle> Occupied;
		for (const FSeinEntityHandle Handle : Entities)
		{
			if (Storage->HasComponent(Handle))
			{
				Occupied.Add(Handle);
			}
		}
		if (Occupied.Num() != Storage->GetComponentCount())
		{
			return Fail(
				OutError,
				FString::Printf(
					TEXT("Component storage '%s' contains an orphaned or stale slot."),
					*ComponentType->GetPathName()));
		}

		FSeinCanonicalDigestWriter StorageWriter(
			TEXT("SeinARTS.LiveWorld.ComponentStorage"), 1);
		if (!StorageWriter.WriteString(ComponentType->GetPathName())
			|| !StorageWriter.WriteGuid(SchemaDigest)
			|| !StorageWriter.WriteUInt32(
				static_cast<uint32>(Occupied.Num())))
		{
			return Fail(OutError, StorageWriter.GetError());
		}
		for (const FSeinEntityHandle Handle : Occupied)
		{
			FGuid IgnoredSchema;
			FGuid ValueDigest;
			if (!Reflected.DigestStruct(
				ComponentType,
				Storage->GetComponentRaw(Handle),
				FString::Printf(
					TEXT("Component '%s' on %s"),
					*ComponentType->GetPathName(),
					*Handle.ToString()),
				IgnoredSchema,
				ValueDigest)
				|| !WriteHandle(StorageWriter, Handle)
				|| !StorageWriter.WriteGuid(ValueDigest))
			{
				return false;
			}
		}

		FGuid StorageDigest;
		if (!StorageWriter.Finalize(StorageDigest, OutError)
			|| !CoreWriter.WriteString(ComponentType->GetPathName())
			|| !CoreWriter.WriteGuid(SchemaDigest)
			|| !CoreWriter.WriteUInt32(
				static_cast<uint32>(Occupied.Num()))
			|| !CoreWriter.WriteGuid(StorageDigest))
		{
			return false;
		}
	}

	TSet<const UObject*> SeenPoolObjects;
	if (!WriteObjectPool(
		CoreWriter,
		TEXT("AbilityPool"),
		AbilityPool,
		AbilityPoolFreeList,
		SeenPoolObjects,
		Reflected,
		OutError)
		|| !WriteObjectPool(
			CoreWriter,
			TEXT("CommandBrokerResolverPool"),
			CommandBrokerResolverPool,
			CommandBrokerResolverPoolFreeList,
			SeenPoolObjects,
			Reflected,
			OutError))
	{
		return false;
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
	OutRoot = CandidateRoot;
	return true;
}

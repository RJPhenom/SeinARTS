/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWorldInitialStateDigest.cpp
 * @brief   Canonical Core tick-zero receipt implementation.
 */

#include "Simulation/SeinWorldSubsystem.h"

#include "Abilities/SeinAbility.h"
#include "Actor/SeinActor.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "Data/SeinFaction.h"
#include "Effects/SeinActiveEffect.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Serialization/SeinCanonicalReflectedStateDigest.h"

namespace
{
	bool WriteHandle(
		FSeinCanonicalDigestWriter& Writer,
		FSeinEntityHandle Handle)
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

	bool WriteClassPath(
		FSeinCanonicalDigestWriter& Writer,
		const UClass* Class)
	{
		return Writer.WriteString(Class ? Class->GetPathName() : FString());
	}

	bool WriteGameplayTag(
		FSeinCanonicalDigestWriter& Writer,
		FGameplayTag Tag)
	{
		return Writer.WriteString(Tag.IsValid() ? Tag.ToString() : FString());
	}

	bool WriteTagContainer(
		FSeinCanonicalDigestWriter& Writer,
		const FGameplayTagContainer& Container)
	{
		TArray<FGameplayTag> Tags;
		Container.GetGameplayTagArray(Tags);
		Tags.Sort([](const FGameplayTag& A, const FGameplayTag& B)
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

	template<typename TValue, typename WriteValue>
	bool WriteTagMap(
		FSeinCanonicalDigestWriter& Writer,
		const TMap<FGameplayTag, TValue>& Values,
		WriteValue&& Write)
	{
		TArray<FGameplayTag> Keys;
		Values.GetKeys(Keys);
		Keys.Sort([](const FGameplayTag& A, const FGameplayTag& B)
		{
			return A.ToString() < B.ToString();
		});
		if (!Writer.WriteUInt32(static_cast<uint32>(Keys.Num())))
		{
			return false;
		}
		for (const FGameplayTag Key : Keys)
		{
			if (!WriteGameplayTag(Writer, Key)
				|| !Write(Values.FindChecked(Key)))
			{
				return false;
			}
		}
		return true;
	}

	bool WriteActiveEffect(
		FSeinCanonicalDigestWriter& Writer,
		const FSeinActiveEffect& Effect,
		FString& OutError)
	{
		const UClass* EffectClass = Effect.EffectClass.Get();
		if (!EffectClass || Effect.EffectInstanceID <= 0)
		{
			OutError = TEXT("Tick-zero player effects contain an invalid class or instance ID.");
			return false;
		}
		if (!Writer.WriteInt64(Effect.EffectInstanceID)
			|| !WriteClassPath(Writer, EffectClass)
			|| !Writer.WriteInt64(Effect.RemainingDuration.Value)
			|| !Writer.WriteInt64(Effect.TimeSinceLastPeriodic.Value)
			|| !Writer.WriteInt32(Effect.CurrentStacks)
			|| !WriteHandle(Writer, Effect.Source)
			|| !WriteHandle(Writer, Effect.Target)
			|| !Writer.WriteUInt32(
				static_cast<uint32>(Effect.CommittedAbilityGrants.Num())))
		{
			return false;
		}
		for (const FSeinEffectAbilityGrant& Grant :
			Effect.CommittedAbilityGrants)
		{
			if (!WriteHandle(Writer, Grant.Recipient)
				|| !WriteClassPath(Writer, Grant.AbilityClass.Get()))
			{
				return false;
			}
		}
		return true;
	}

	bool WriteEffects(
		FSeinCanonicalDigestWriter& Writer,
		const TArray<FSeinActiveEffect>& Effects,
		FString& OutError)
	{
		if (!Writer.WriteUInt32(static_cast<uint32>(Effects.Num())))
		{
			return false;
		}
		for (const FSeinActiveEffect& Effect : Effects)
		{
			if (!WriteActiveEffect(Writer, Effect, OutError))
			{
				return false;
			}
		}
		return true;
	}

	struct FContributionFrame
	{
		FString CanonicalID;
		uint32 SchemaVersion = 0;
		FGuid PayloadDigest;
	};
}

bool USeinWorldSubsystem::ComputeCanonicalInitialStateDigest(
	FGuid& OutDigest,
	FString& OutError) const
{
	OutDigest.Invalidate();
	OutError.Reset();

	const bool bDigestiblePhase =
		MatchBootstrapState == ESeinMatchBootstrapState::Applying
		|| MatchBootstrapState == ESeinMatchBootstrapState::LocallyReady
		|| MatchBootstrapState == ESeinMatchBootstrapState::Authorized
		|| MatchBootstrapState == ESeinMatchBootstrapState::Consumed;
	// RemainStopped snapshot adoption deliberately reserves a dormant ticker
	// before committing authoritative state. It cannot execute while bIsRunning
	// is false, and tick-zero replay/resync must be able to revalidate the exact
	// initial root before flipping that prepared scheduler live.
	const bool bAllowedDormantConsumedScheduler =
		MatchBootstrapState == ESeinMatchBootstrapState::Consumed
		&& !bIsRunning
		&& bSimulationSchedulerReserved
		&& TickerHandle.IsValid();
	const bool bHasDisallowedSchedulerState =
		(bSimulationSchedulerReserved || TickerHandle.IsValid())
		&& !bAllowedDormantConsumedScheduler;
	if (!bDigestiblePhase || bIsRunning || bHasDisallowedSchedulerState
		|| CurrentTick != 0 || MatchState != ESeinMatchState::Starting)
	{
		OutError = TEXT("Canonical initial state exists only in a stopped tick-zero Starting bootstrap world with no scheduler or its exact dormant consumed reservation.");
		return false;
	}
	if (!CommandProtocolDigest.IsValid() || !MatchSettingsDigest.IsValid()
		|| !MatchBootstrapReceipt.ContractDigest.IsValid()
		|| !SimulationContentDigest.IsValid()
		|| MatchBootstrapReceipt.SimulationContentDigest
			!= SimulationContentDigest
		|| !MatchBootstrapAuthorizationContextDigest.IsValid()
		|| !CanonicalStateValues.IsSealed()
		|| !CanonicalStateValues.GetContractDigest().IsValid()
		|| MatchBootstrapReceipt.StateContractDigest
			!= CanonicalStateValues.GetContractDigest())
	{
		OutError = TEXT("Canonical initial state is missing protocol, content, settings, state-contract, or authorization identity.");
		return false;
	}

	// Receipt state must be a closed transaction, not merely a snapshot taken
	// while deferred work could still alter tick zero.
	if (PendingCommands.Num() != 0
		|| PendingReplayCommands.Num() != 0
		|| !PendingStandalonePauseControlCommands.IsEmpty()
		|| !PendingDestroy.IsEmpty()
		|| !PendingEffectApplies.IsEmpty()
		|| !ActiveVotes.IsEmpty()
		|| OwnerTransitionDepth != 0
		|| bDispatchingPauseControlFrame
		|| bPauseControlDispatchProtocolViolation
		|| ActivePauseControlCommandIndex != INDEX_NONE
		|| ActivePauseControlCommandCount != 0)
	{
		OutError = TEXT("Canonical initial-state digest refused a non-quiescent command, mutation, ownership, vote, or pause-control transaction.");
		return false;
	}

	FSeinCanonicalDigestWriter Writer(
		TEXT("SeinARTS.InitialState"),
		FSeinCanonicalInitialStateDigest::CurrentFormatVersion);
	if (!Writer.WriteGuid(CommandProtocolDigest)
		|| !Writer.WriteGuid(SimulationContentDigest)
		|| !Writer.WriteGuid(MatchSettingsDigest)
		|| !Writer.WriteGuid(MatchBootstrapReceipt.ContractDigest)
		|| !Writer.WriteGuid(
			CanonicalStateValues.GetContractDigest())
		|| !Writer.WriteInt32(ConfigFingerprint)
		|| !Writer.WriteInt64(SimSessionSeed)
		|| !Writer.WriteUInt64(SimRandom.State0)
		|| !Writer.WriteUInt64(SimRandom.State1)
		|| !Writer.WriteUInt8(static_cast<uint8>(MatchState))
		|| !Writer.WriteInt32(StartingStateDeadlineTick)
		|| !Writer.WriteInt32(MatchStartTick)
		|| !Writer.WriteBool(bSimPaused)
		|| !Writer.WriteBool(bSimPausedHard)
		|| !Writer.WriteInt64(PauseEpoch)
		|| !Writer.WriteInt32(PauseFrozenTick)
		|| !Writer.WriteInt64(LastAppliedPauseControlSequence)
		|| !Writer.WriteInt32(CommandCohesionOrderSequence)
		|| !Writer.WriteInt64(NextEffectInstanceID))
	{
		return Writer.Finalize(OutDigest, OutError);
	}

	// Player state is canonicalized by numeric player ID; TMap bucket layout is
	// deliberately irrelevant.
	TArray<FSeinPlayerID> PlayerIDs;
	PlayerStates.GetKeys(PlayerIDs);
	PlayerIDs.Sort();
	if (!Writer.WriteUInt32(static_cast<uint32>(PlayerIDs.Num())))
	{
		return Writer.Finalize(OutDigest, OutError);
	}
	for (const FSeinPlayerID PlayerID : PlayerIDs)
	{
		const FSeinPlayerState& Player = PlayerStates.FindChecked(PlayerID);
		// A factionless player (Faction 0) is legal — factions are an opt-in
		// catalog; the faction VALUE is digested below either way.
		if (Player.PlayerID != PlayerID
			|| !Writer.WriteUInt8(PlayerID.Value)
			|| !Writer.WriteUInt8(Player.FactionID.Value)
			|| !Writer.WriteUInt8(Player.TeamID)
			|| !Writer.WriteBool(Player.bEliminated)
			|| !Writer.WriteBool(Player.bReady)
			|| !Writer.WriteBool(Player.bIsSpectator)
			|| !Writer.WriteBool(Player.bIsAI)
			|| !WriteTagMap(Writer, Player.Resources,
				[&Writer](const FFixedPoint& Value)
				{
					return Writer.WriteInt64(Value.Value);
				})
			|| !WriteTagMap(Writer, Player.ResourceCaps,
				[&Writer](const FFixedPoint& Value)
				{
					return Writer.WriteInt64(Value.Value);
				})
			|| !WriteTagMap(Writer, Player.PlayerTagRefCounts,
				[&Writer](int32 Value)
				{
					return Writer.WriteInt32(Value);
				})
			|| !WriteTagContainer(Writer, Player.PlayerTags)
			|| !WriteEffects(Writer, Player.ClassEffects, OutError)
			|| !WriteEffects(Writer, Player.PlayerEffects, OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError = Writer.GetError().IsEmpty()
					? TEXT("Canonical player-state encoding failed.")
					: Writer.GetError();
			}
			return false;
		}
	}

	// Include every allocator slot and the exact LIFO free-list order. This is
	// future-affecting state: two identical live rosters can allocate different
	// handles on the next spawn if prior tick-zero churn differed.
	FSeinEntityPoolExactState ExactEntityPoolState;
	FString EntityPoolStateError;
	if (!EntityPool.CaptureExactState(
			ExactEntityPoolState, EntityPoolStateError))
	{
		OutError = FString::Printf(
			TEXT("Tick-zero entity-pool topology is invalid: %s"),
			*EntityPoolStateError);
		return false;
	}
	if (!Writer.WriteInt32(ExactEntityPoolState.Capacity)
		|| !Writer.WriteInt32(EntityPool.GetActiveCount())
		|| !Writer.WriteUInt32(
			static_cast<uint32>(ExactEntityPoolState.Slots.Num())))
	{
		return Writer.Finalize(OutDigest, OutError);
	}
	for (const FSeinEntityPoolSlotState& Slot :
		ExactEntityPoolState.Slots)
	{
		if (!Writer.WriteInt32(Slot.Entity.ID.Value)
			|| !WriteFixedTransform(Writer, Slot.Entity.Transform)
			|| !Writer.WriteInt32(Slot.Entity.Flags)
			|| !Writer.WriteInt32(Slot.Generation)
			|| !Writer.WriteUInt8(Slot.Owner.Value)
			|| !Writer.WriteBool(Slot.bRetired))
		{
			return Writer.Finalize(OutDigest, OutError);
		}
	}
	if (!Writer.WriteUInt32(
		static_cast<uint32>(ExactEntityPoolState.FreeList.Num())))
	{
		return Writer.Finalize(OutDigest, OutError);
	}
	for (const int32 FreeSlot : ExactEntityPoolState.FreeList)
	{
		if (!Writer.WriteInt32(FreeSlot))
		{
			return Writer.Finalize(OutDigest, OutError);
		}
	}

	TArray<FSeinEntityHandle> Entities;
	EntityPool.ForEachEntity(
		[&Entities](FSeinEntityHandle Handle, const FSeinEntity&)
		{
			Entities.Add(Handle);
		});
	Entities.Sort();
	if (Entities.Num() != EntityPool.GetActiveCount()
		|| !Writer.WriteUInt32(static_cast<uint32>(Entities.Num())))
	{
		OutError = TEXT("Entity pool active-count/topology mismatch while sealing tick zero.");
		return false;
	}
	for (const FSeinEntityHandle Handle : Entities)
	{
		const FSeinEntity* Entity = EntityPool.Get(Handle);
		const TSubclassOf<ASeinActor>* ActorClass =
			EntityActorClassMap.Find(Handle);
		if (!Entity || !Entity->IsAlive())
		{
			OutError = FString::Printf(
				TEXT("Tick-zero entity %s is missing live core state."),
				*Handle.ToString());
			return false;
		}
		if (!WriteHandle(Writer, Handle)
			|| !Writer.WriteInt32(Entity->ID.Value)
			|| !WriteFixedTransform(Writer, Entity->Transform)
			|| !Writer.WriteInt32(Entity->Flags)
			|| !Writer.WriteUInt8(EntityPool.GetOwner(Handle).Value)
			|| !WriteClassPath(
				Writer, ActorClass ? ActorClass->Get() : nullptr))
		{
			return Writer.Finalize(OutDigest, OutError);
		}
	}

	const FSeinCanonicalReflectedStateLimits ReflectedStateLimits;
	TMap<const UStruct*, FGuid> ReflectedSchemaDigests;
	const auto ResolveReflectedSchema =
		[&ReflectedSchemaDigests, &ReflectedStateLimits, &OutError](
			const UStruct* Type,
			const FString& StateContext,
			FGuid& OutSchemaDigest)
		{
			if (!Type)
			{
				OutError = StateContext + TEXT(": reflected type is null.");
				return false;
			}
			if (const FGuid* Existing =
				ReflectedSchemaDigests.Find(Type))
			{
				OutSchemaDigest = *Existing;
				return true;
			}
			FString SchemaError;
			if (!FSeinCanonicalReflectedStateDigest::ComputeSchemaDigest(
					Type,
					ReflectedStateLimits,
					OutSchemaDigest,
					SchemaError))
			{
				OutError = FString::Printf(
					TEXT("%s schema '%s' is not canonical: %s"),
					*StateContext,
					*Type->GetPathName(),
					*SchemaError);
				return false;
			}
			ReflectedSchemaDigests.Add(Type, OutSchemaDigest);
			return true;
		};

	// Bind every live component value, not just its reflected type and slot.
	// A per-storage leaf keeps the root bounded without introducing a second
	// reversible snapshot format.
	TArray<UScriptStruct*> ComponentTypes;
	ComponentStorages.GetKeys(ComponentTypes);
	ComponentTypes.Sort([](const UScriptStruct& A, const UScriptStruct& B)
	{
		return A.GetPathName() < B.GetPathName();
	});
	if (!Writer.WriteUInt32(static_cast<uint32>(ComponentTypes.Num())))
	{
		return Writer.Finalize(OutDigest, OutError);
	}
	for (UScriptStruct* ComponentType : ComponentTypes)
	{
		const ISeinComponentStorage* Storage =
			ComponentStorages.FindChecked(ComponentType);
		if (!ComponentType || !Storage)
		{
			OutError = TEXT("Tick-zero component registry contains a null type or storage.");
			return false;
		}
		FGuid SchemaDigest;
		if (!ResolveReflectedSchema(
			ComponentType,
			TEXT("Tick-zero component"),
			SchemaDigest))
		{
			return false;
		}
		TArray<FSeinEntityHandle> OccupiedHandles;
		for (const FSeinEntityHandle Handle : Entities)
		{
			if (Storage->HasComponent(Handle))
			{
				OccupiedHandles.Add(Handle);
			}
		}
		if (OccupiedHandles.Num() != Storage->GetComponentCount())
		{
			OutError = FString::Printf(
				TEXT("Component storage '%s' contains an orphaned or stale slot at tick zero."),
				*ComponentType->GetPathName());
			return false;
		}
		FSeinCanonicalDigestWriter StorageWriter(
			TEXT("SeinARTS.InitialState.ComponentStorage"), 1);
		if (!StorageWriter.WriteString(ComponentType->GetPathName())
			|| !StorageWriter.WriteGuid(SchemaDigest)
			|| !StorageWriter.WriteUInt32(
				static_cast<uint32>(OccupiedHandles.Num())))
		{
			OutError = StorageWriter.GetError();
			return false;
		}
		for (const FSeinEntityHandle Handle : OccupiedHandles)
		{
			const void* ComponentMemory =
				Storage->GetComponentRaw(Handle);
			FGuid ValueDigest;
			FString ValueError;
			if (!ComponentMemory
				|| !FSeinCanonicalReflectedStateDigest::
					ComputeStructValueDigest(
						ComponentType,
						ComponentMemory,
						SchemaDigest,
						ReflectedStateLimits,
						ValueDigest,
						ValueError))
			{
				OutError = FString::Printf(
					TEXT("Tick-zero component '%s' on entity %s is not canonical: %s"),
					*ComponentType->GetPathName(),
					*Handle.ToString(),
					ComponentMemory
						? *ValueError
						: TEXT("live storage returned null payload"));
				return false;
			}
			if (!WriteHandle(StorageWriter, Handle)
				|| !StorageWriter.WriteGuid(ValueDigest))
			{
				OutError = StorageWriter.GetError();
				return false;
			}
		}
		FGuid StorageDigest;
		if (!StorageWriter.Finalize(StorageDigest, OutError)
			|| !Writer.WriteString(ComponentType->GetPathName())
			|| !Writer.WriteGuid(SchemaDigest)
			|| !Writer.WriteUInt32(
				static_cast<uint32>(OccupiedHandles.Num()))
			|| !Writer.WriteGuid(StorageDigest))
		{
			return false;
		}
	}

	TSet<const UObject*> SeenPooledObjects;
	const auto WriteObjectPool = [
		&Writer,
		&ResolveReflectedSchema,
		&ReflectedStateLimits,
		&SeenPooledObjects,
		&OutError](
		const TCHAR* PoolName,
		const auto& Pool,
		const TArray<int32>& FreeList)
	{
		if (FreeList.Num() > Pool.Num())
		{
			OutError = FString::Printf(
				TEXT("Tick-zero %s free list exceeds its pool."),
				PoolName);
			return false;
		}
		TSet<int32> FreeSlots;
		for (const int32 FreeIndex : FreeList)
		{
			if (!Pool.IsValidIndex(FreeIndex)
				|| FreeSlots.Contains(FreeIndex)
				|| Pool[FreeIndex] != nullptr)
			{
				OutError = FString::Printf(
					TEXT("Tick-zero %s free list contains an invalid, duplicate, or live slot %d."),
					PoolName,
					FreeIndex);
				return false;
			}
			FreeSlots.Add(FreeIndex);
		}
		for (int32 Index = 0; Index < Pool.Num(); ++Index)
		{
			if ((Pool[Index] == nullptr) != FreeSlots.Contains(Index))
			{
				OutError = FString::Printf(
					TEXT("Tick-zero %s pool/free-list topology disagrees at slot %d."),
					PoolName,
					Index);
				return false;
			}
		}

		FSeinCanonicalDigestWriter PoolWriter(
			TEXT("SeinARTS.InitialState.ObjectPool"), 1);
		if (!PoolWriter.WriteString(PoolName)
			|| !PoolWriter.WriteUInt32(
				static_cast<uint32>(Pool.Num())))
		{
			OutError = PoolWriter.GetError();
			return false;
		}
		for (int32 Index = 0; Index < Pool.Num(); ++Index)
		{
			const UObject* Object = Pool[Index].Get();
			if (!PoolWriter.WriteInt32(Index)
				|| !PoolWriter.WriteBool(Object != nullptr))
			{
				OutError = PoolWriter.GetError();
				return false;
			}
			if (!Object)
			{
				continue;
			}
			if (SeenPooledObjects.Contains(Object))
			{
				OutError = FString::Printf(
					TEXT("Tick-zero object pools contain duplicate live UObject pointer '%s' at %s slot %d."),
					*Object->GetPathName(),
					PoolName,
					Index);
				return false;
			}
			SeenPooledObjects.Add(Object);
			FGuid SchemaDigest;
			FGuid ValueDigest;
			FString ValueError;
			if (!ResolveReflectedSchema(
					Object->GetClass(), PoolName, SchemaDigest)
				|| !FSeinCanonicalReflectedStateDigest::
					ComputeObjectValueDigest(
						Object,
						SchemaDigest,
						ReflectedStateLimits,
						ValueDigest,
						ValueError))
			{
				if (OutError.IsEmpty())
				{
					OutError = FString::Printf(
						TEXT("Tick-zero %s slot %d class '%s' is not canonical: %s"),
						PoolName,
						Index,
						*Object->GetClass()->GetPathName(),
						*ValueError);
				}
				return false;
			}
			if (!WriteClassPath(PoolWriter, Object->GetClass())
				|| !PoolWriter.WriteGuid(SchemaDigest)
				|| !PoolWriter.WriteGuid(ValueDigest))
			{
				OutError = PoolWriter.GetError();
				return false;
			}
		}
		if (!PoolWriter.WriteUInt32(
			static_cast<uint32>(FreeList.Num())))
		{
			OutError = PoolWriter.GetError();
			return false;
		}
		for (const int32 FreeIndex : FreeList)
		{
			if (!PoolWriter.WriteInt32(FreeIndex))
			{
				OutError = PoolWriter.GetError();
				return false;
			}
		}
		FGuid PoolDigest;
		return PoolWriter.Finalize(PoolDigest, OutError)
			&& Writer.WriteString(PoolName)
			&& Writer.WriteGuid(PoolDigest);
	};
	if (!WriteObjectPool(
			TEXT("AbilityPool"), AbilityPool, AbilityPoolFreeList)
		|| !WriteObjectPool(
			TEXT("CommandBrokerResolverPool"),
			CommandBrokerResolverPool,
			CommandBrokerResolverPoolFreeList))
	{
		return false;
	}

	// Stable tag/name registries are state, but their hash-table bucket order is
	// not. Keys are sorted explicitly. Tag-index value order is preserved:
	// LookupFirstEntityByTag exposes the first bucket element to gameplay.
	TArray<FSeinEntityHandle> TagStateHandles;
	EntityTagStates.GetKeys(TagStateHandles);
	TagStateHandles.Sort();
	if (!Writer.WriteUInt32(static_cast<uint32>(TagStateHandles.Num())))
	{
		return Writer.Finalize(OutDigest, OutError);
	}
	for (const FSeinEntityHandle Handle : TagStateHandles)
	{
		if (!EntityPool.IsValid(Handle))
		{
			OutError = TEXT("Tick-zero entity-tag registry contains a stale handle.");
			return false;
		}
		const FSeinEntityTagState& State = EntityTagStates.FindChecked(Handle);
		if (!WriteHandle(Writer, Handle)
			|| !WriteTagContainer(Writer, State.BaseTags)
			|| !WriteTagMap(Writer, State.TagRefCounts,
				[&Writer](int32 Value)
				{
					return Writer.WriteInt32(Value);
				})
			|| !WriteTagContainer(Writer, State.CombinedTags))
		{
			return Writer.Finalize(OutDigest, OutError);
		}
	}

	TArray<FGameplayTag> IndexedTags;
	EntityTagIndex.GetKeys(IndexedTags);
	IndexedTags.Sort([](const FGameplayTag& A, const FGameplayTag& B)
	{
		return A.ToString() < B.ToString();
	});
	if (!Writer.WriteUInt32(static_cast<uint32>(IndexedTags.Num())))
	{
		return Writer.Finalize(OutDigest, OutError);
	}
	for (const FGameplayTag Tag : IndexedTags)
	{
		const TArray<FSeinEntityHandle>& Handles =
			EntityTagIndex.FindChecked(Tag);
		if (!WriteGameplayTag(Writer, Tag)
			|| !Writer.WriteUInt32(static_cast<uint32>(Handles.Num())))
		{
			return Writer.Finalize(OutDigest, OutError);
		}
		for (const FSeinEntityHandle Handle : Handles)
		{
			if (!EntityPool.IsValid(Handle) || !WriteHandle(Writer, Handle))
			{
				OutError = TEXT("Tick-zero global tag index contains a stale handle.");
				return false;
			}
		}
	}

	TArray<FName> EntityNames;
	NamedEntityRegistry.GetKeys(EntityNames);
	EntityNames.Sort([](FName A, FName B)
	{
		return FSeinCanonicalInitialStateDigest::CanonicalContributorID(A)
			< FSeinCanonicalInitialStateDigest::CanonicalContributorID(B);
	});
	if (!Writer.WriteUInt32(static_cast<uint32>(EntityNames.Num())))
	{
		return Writer.Finalize(OutDigest, OutError);
	}
	for (const FName Name : EntityNames)
	{
		const FSeinEntityHandle Handle = NamedEntityRegistry.FindChecked(Name);
		if (!EntityPool.IsValid(Handle)
			|| !Writer.WriteName(Name)
			|| !WriteHandle(Writer, Handle))
		{
			OutError = TEXT("Tick-zero named-entity registry contains a stale handle.");
			return false;
		}
	}

	TArray<FSeinFactionID> FactionIDs;
	Factions.GetKeys(FactionIDs);
	FactionIDs.Sort();
	if (!Writer.WriteUInt32(static_cast<uint32>(FactionIDs.Num())))
	{
		return Writer.Finalize(OutDigest, OutError);
	}
	for (const FSeinFactionID FactionID : FactionIDs)
	{
		const USeinFaction* Faction = Factions.FindChecked(FactionID).Get();
		if (!Faction || Faction->FactionID != FactionID)
		{
			OutError = TEXT("Tick-zero faction registry contains an invalid or mismatched entry.");
			return false;
		}
		FGuid SchemaDigest;
		FGuid ValueDigest;
		FString ValueError;
		if (!ResolveReflectedSchema(
				Faction->GetClass(),
				TEXT("Tick-zero faction"),
				SchemaDigest)
			|| !FSeinCanonicalReflectedStateDigest::
				ComputeObjectValueDigest(
					Faction,
					SchemaDigest,
					ReflectedStateLimits,
					ValueDigest,
					ValueError))
		{
			if (OutError.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Tick-zero faction %d asset '%s' is not canonical: %s"),
					FactionID.Value,
					*Faction->GetPathName(),
					*ValueError);
			}
			return false;
		}
		const bool bStableAsset = Faction->IsAsset();
		if (!Writer.WriteUInt8(FactionID.Value)
			|| !Writer.WriteBool(bStableAsset)
			|| !Writer.WriteString(
				bStableAsset
					? Faction->GetPathName()
					: TEXT("TransientFactionDefinition"))
			|| !WriteClassPath(Writer, Faction->GetClass())
			|| !Writer.WriteGuid(SchemaDigest)
			|| !Writer.WriteGuid(ValueDigest))
		{
			return Writer.Finalize(OutDigest, OutError);
		}
	}

	// AI reasoning objects are intentionally host-only and therefore excluded.
	// Their deterministic player-slot projection above is shared by every peer.

	TArray<FSeinCanonicalStateContributorRecord> NativeStateRecords;
	if (!FSeinCanonicalStateRegistry::CaptureContributorRecords(
		NativeCanonicalStateSchema,
		{ *this, CurrentTick },
		NativeStateRecords,
		OutError))
	{
		return false;
	}
	if (!Writer.WriteUInt32(
			static_cast<uint32>(NativeStateRecords.Num())))
	{
		return Writer.Finalize(OutDigest, OutError);
	}
	for (const FSeinCanonicalStateContributorRecord& Record :
		NativeStateRecords)
	{
		if (!Writer.WriteString(
				FSeinCanonicalStateRegistry::CanonicalKey(Record.Key))
			|| !Writer.WriteInt32(Record.SchemaVersion)
			|| !Writer.WriteGuid(Record.DescriptorDigest)
			|| !Writer.WriteGuid(Record.LeafDigest))
		{
			return Writer.Finalize(OutDigest, OutError);
		}
	}

	TArray<FSeinCanonicalStateValueRecord> StateValueRecords;
	if (!CanonicalStateValues.CaptureRecords(
		StateValueRecords, OutError))
	{
		return false;
	}
	if (!Writer.WriteUInt32(
			static_cast<uint32>(StateValueRecords.Num())))
	{
		return Writer.Finalize(OutDigest, OutError);
	}
	for (const FSeinCanonicalStateValueRecord& Record :
		StateValueRecords)
	{
		if (!Writer.WriteString(
				FSeinCanonicalStateRegistry::CanonicalKey(Record.Key))
			|| !Writer.WriteInt32(Record.SchemaVersion)
			|| !Writer.WriteGuid(Record.DescriptorDigest)
			|| !Writer.WriteGuid(Record.LeafDigest))
		{
			return Writer.Finalize(OutDigest, OutError);
		}
	}

	TArray<FContributionFrame> Contributions;
	Contributions.Reserve(
		MatchBootstrapNativeContributors.Num()
		+ MatchBootstrapValueContributions.Num());
	for (const FSeinCanonicalInitialStateNativeContribution& Native :
		MatchBootstrapNativeContributors)
	{
		FSeinCanonicalDigestWriter PayloadWriter(
			TEXT("SeinARTS.InitialState.Contributor"),
			Native.SchemaVersion);
		FString CaptureError;
		if (!Native.Capture
			|| !Native.Capture(*this, PayloadWriter, CaptureError))
		{
			OutError = CaptureError.IsEmpty()
				? FString::Printf(
					TEXT("Initial-state contributor '%s' failed without a diagnostic."),
					*Native.StableContributorID.ToString())
				: MoveTemp(CaptureError);
			return false;
		}
		FContributionFrame& Frame = Contributions.AddDefaulted_GetRef();
		Frame.CanonicalID =
			FSeinCanonicalInitialStateDigest::CanonicalContributorID(
				Native.StableContributorID);
		Frame.SchemaVersion = Native.SchemaVersion;
		if (!PayloadWriter.Finalize(Frame.PayloadDigest, OutError))
		{
			return false;
		}
	}
	for (const FSeinCanonicalInitialStateValueContribution& Value :
		MatchBootstrapValueContributions)
	{
		FContributionFrame& Frame = Contributions.AddDefaulted_GetRef();
		Frame.CanonicalID =
			FSeinCanonicalInitialStateDigest::CanonicalContributorID(
				Value.StableContributorID);
		Frame.SchemaVersion = Value.SchemaVersion;
		Frame.PayloadDigest = Value.ValueDigest;
	}
	Contributions.Sort([](const FContributionFrame& A, const FContributionFrame& B)
	{
		return A.CanonicalID < B.CanonicalID;
	});
	for (int32 Index = 0; Index < Contributions.Num(); ++Index)
	{
		const FContributionFrame& Frame = Contributions[Index];
		if (Frame.CanonicalID.IsEmpty() || Frame.SchemaVersion == 0
			|| !Frame.PayloadDigest.IsValid()
			|| (Index > 0
				&& Contributions[Index - 1].CanonicalID == Frame.CanonicalID))
		{
			OutError = TEXT("Initial-state contribution set contains an invalid or duplicate stable ID.");
			return false;
		}
	}
	if (!Writer.WriteUInt32(static_cast<uint32>(Contributions.Num())))
	{
		return Writer.Finalize(OutDigest, OutError);
	}
	for (const FContributionFrame& Frame : Contributions)
	{
		if (!Writer.WriteString(Frame.CanonicalID)
			|| !Writer.WriteUInt32(Frame.SchemaVersion)
			|| !Writer.WriteGuid(Frame.PayloadDigest))
		{
			return Writer.Finalize(OutDigest, OutError);
		}
	}

	return Writer.Finalize(OutDigest, OutError);
}

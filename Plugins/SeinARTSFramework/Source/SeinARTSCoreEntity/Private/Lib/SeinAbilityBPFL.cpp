/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAbilityBPFL.cpp
 * @brief   Implementation of ability system Blueprint nodes.
 */

#include "Lib/SeinAbilityBPFL.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinCommandBrokerData.h"
#include "Core/SeinPlayerState.h"
#include "Core/SeinSimContext.h"
#include "Abilities/SeinAbility.h"
#include "Abilities/SeinAbilityValidation.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Input/SeinCommand.h"
#include "Lib/SeinResourceBPFL.h"

#include "SeinARTSCoreEntityLog.h"  // LogSeinBPFL (module-shared)

USeinWorldSubsystem* USeinAbilityBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

bool USeinAbilityBPFL::SeinGetAbilityData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinAbilityComponent& OutData)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("GetAbilityData: no SeinWorldSubsystem in this world context"));
		return false;
	}
	const FSeinAbilityComponent* Data = Subsystem->GetComponent<FSeinAbilityComponent>(EntityHandle);
	if (!Data)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("GetAbilityData: entity %s invalid or has no FSeinAbilityComponent"), *EntityHandle.ToString());
		return false;
	}
	OutData = *Data;
	return true;
}

TArray<FSeinAbilityComponent> USeinAbilityBPFL::SeinGetAbilityDataMany(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& EntityHandles)
{
	TArray<FSeinAbilityComponent> Result;
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return Result;

	Result.Reserve(EntityHandles.Num());
	for (const FSeinEntityHandle& Handle : EntityHandles)
	{
		if (const FSeinAbilityComponent* Data = Subsystem->GetComponent<FSeinAbilityComponent>(Handle))
		{
			Result.Add(*Data);
		}
		else
		{
			UE_LOG(LogSeinBPFL, Warning, TEXT("GetAbilityData (batch): skipping entity %s (invalid or no FSeinAbilityComponent)"), *Handle.ToString());
		}
	}
	return Result;
}

void USeinAbilityBPFL::SeinActivateAbility(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag, FSeinEntityHandle TargetEntity, FFixedVector TargetLocation)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem
		|| !Subsystem->RequireStateMutationAuthorization(TEXT("ActivateAbilityDirect")))
	{
		return;
	}

	FSeinAbilityComponent* AbilityComp =
		Subsystem->GetComponentMutable<FSeinAbilityComponent>(
			EntityHandle);
	if (!AbilityComp) return;

	USeinAbility* Ability = AbilityComp->FindAbilityByTag(*Subsystem, AbilityTag);
	if (!Ability || Ability->IsOnCooldown() || Ability->bIsActive) return;

	Ability->ActivateAbility(TargetEntity, TargetLocation);
}

void USeinAbilityBPFL::SeinIssueAbilityCommand(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag, FSeinEntityHandle TargetEntity, FFixedVector TargetLocation)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("IssueAbilityCommand: no SeinWorldSubsystem"));
		return;
	}

	// Derive the owning PlayerID from the entity — callers shouldn't have to
	// thread it explicitly. An invalid entity returns an invalid PlayerID;
	// ProcessCommands will reject the command on its own validity gate.
	const FSeinPlayerID Owner = Subsystem->GetEntityOwner(EntityHandle);

	UE_LOG(LogSeinBPFL, Log,
		TEXT("IssueAbilityCommand: enqueuing ActivateAbility[%s] caster=%s target=%s player=%s"),
		*AbilityTag.ToString(), *EntityHandle.ToString(), *TargetEntity.ToString(), *Owner.ToString());

	const FSeinCommand Cmd = FSeinCommand::MakeAbilityCommand(Owner, EntityHandle, AbilityTag, TargetEntity, TargetLocation);
	Subsystem->EnqueueDerivedCommand(Cmd);
}

void USeinAbilityBPFL::SeinIssueBrokerOrderFromEntity(
	const UObject* WorldContextObject,
	FSeinEntityHandle CallerEntity,
	FGameplayTag AbilityTag,
	FSeinEntityHandle TargetEntity,
	FFixedVector TargetLocation,
	bool bQueueCommand)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("IssueBrokerOrderFromEntity: no SeinWorldSubsystem"));
		return;
	}
	if (!SeinIsInSimContext(Subsystem))
	{
		UE_LOG(LogSeinBPFL, Error,
			TEXT("IssueBrokerOrderFromEntity rejected outside simulation context."));
		return;
	}
	if (!AbilityTag.IsValid())
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("IssueBrokerOrderFromEntity: invalid ability tag"));
		return;
	}

	// Resolve the caller's broker. Members carry FSeinBrokerMembershipData
	// pointing at their currently-assigned broker carrier entity (squad for
	// squad members, ephemeral selection broker for selection members,
	// nothing for never-commanded lone units).
	FSeinEntityHandle BrokerHandle;
	if (const FSeinBrokerMembershipData* Memb = Subsystem->GetComponent<FSeinBrokerMembershipData>(CallerEntity))
	{
		BrokerHandle = Memb->CurrentBrokerHandle;
	}

	// No broker → no fan-out target. Fall back to single-entity dispatch so
	// the chain doesn't silently break for lone units. (Lone right-clicks
	// spawn an ephemeral broker, but ability-chained calls from inside a
	// member's own OnActivate may fire before any such broker exists.)
	if (!BrokerHandle.IsValid() || !Subsystem->GetEntityPool().IsValid(BrokerHandle))
	{
		UE_LOG(LogSeinBPFL, Log,
			TEXT("IssueBrokerOrderFromEntity: caller %s has no broker — falling back to single-entity ActivateAbility[%s]"),
			*CallerEntity.ToString(), *AbilityTag.ToString());
		SeinIssueAbilityCommand(WorldContextObject, CallerEntity, AbilityTag, TargetEntity, TargetLocation);
		return;
	}

	FSeinCommandBrokerData* Broker =
		Subsystem->GetComponentMutable<FSeinCommandBrokerData>(
			BrokerHandle);
	if (!Broker)
	{
		UE_LOG(LogSeinBPFL, Warning,
			TEXT("IssueBrokerOrderFromEntity: caller %s membership points at broker %s but FSeinCommandBrokerData is missing — falling back to single-entity dispatch"),
			*CallerEntity.ToString(), *BrokerHandle.ToString());
		SeinIssueAbilityCommand(WorldContextObject, CallerEntity, AbilityTag, TargetEntity, TargetLocation);
		return;
	}

	// Build the order. TargetMembers stays empty (the resolver's
	// CapabilityMap + ability DispatchMode policy controls fan-out — passing
	// a subset here would override that). PredeterminedAbilityTag tells the
	// resolver "skip per-member smart-context resolution; the ability is
	// already chosen, just apply its dispatch policy."
	FSeinBrokerQueuedOrder Order;
	Order.Context.AddTag(AbilityTag);
	Order.PredeterminedAbilityTag = AbilityTag;
	Order.TargetEntity = TargetEntity;
	Order.TargetLocation = TargetLocation;
	Order.bIsInternalPrefix = false;

	// Queue position: bQueueCommand=true appends after existing entries
	// (shift-chain semantics). bQueueCommand=false inserts at the back of
	// the still-queued tail but ahead of any not-yet-started "appended"
	// orders. For now both behaviors append — the per-order parallelism
	// model means the dispatch loop reorders eligible orders naturally
	// based on member locks; the difference between "append" and "insert
	// next" matters less than under the old FIFO model. Keeping the param
	// reserved for future use if a strict-FIFO mode is needed.
	(void)bQueueCommand;
	Broker->OrderQueue.Add(Order);

	UE_LOG(LogSeinBPFL, Log,
		TEXT("IssueBrokerOrderFromEntity: caller %s → broker %s enqueued predetermined[%s] target=%s queue-depth=%d"),
		*CallerEntity.ToString(), *BrokerHandle.ToString(), *AbilityTag.ToString(),
		*TargetEntity.ToString(), Broker->OrderQueue.Num());
}

void USeinAbilityBPFL::SeinCancelAbility(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem
		|| !Subsystem->RequireStateMutationAuthorization(TEXT("CancelAbilityDirect")))
	{
		return;
	}

	FSeinAbilityComponent* AbilityComp =
		Subsystem->GetComponentMutable<FSeinAbilityComponent>(
			EntityHandle);
	if (!AbilityComp) return;

	if (USeinAbility* Active = AbilityComp->GetActiveAbility(*Subsystem))
	{
		Active->CancelAbility();
	}
}

// ─────────────────────────── Runtime grant / revoke ──────────────────────────

namespace SeinAbilityGrantLocal
{
	/** Mark the entity's broker (if any) capability-map dirty. Granting or
	 *  revoking an ability changes which tags the broker can fan-out to,
	 *  so the next dispatch tick needs to rebuild the cap map. Safe no-op
	 *  if the entity isn't a broker member. */
	static void DirtyBrokerCapability(USeinWorldSubsystem& World, FSeinEntityHandle Entity)
	{
		if (const FSeinBrokerMembershipData* Memb = World.GetComponent<FSeinBrokerMembershipData>(Entity))
		{
			if (FSeinCommandBrokerData* Broker =
				World.GetComponentMutable<FSeinCommandBrokerData>(
					Memb->CurrentBrokerHandle))
			{
				Broker->bCapabilityMapDirty = true;
			}
		}
		// Also mark the entity's own broker dirty if THIS entity is a broker
		// carrier (squad entity). Squad members route through the line above;
		// the squad entity itself is the broker carrier and isn't a member of
		// any other broker.
		if (FSeinCommandBrokerData* OwnBroker =
			World.GetComponentMutable<FSeinCommandBrokerData>(
				Entity))
		{
			OwnBroker->bCapabilityMapDirty = true;
		}
	}

	/** Find the index in `AbilityInstanceIDs` (parallel to `AbilityGrantCounts`)
	 *  whose pool ID resolves to an ability matching the predicate. Returns
	 *  INDEX_NONE if not found. Linear walk — ability lists are short. */
	template <typename PredT>
	static int32 FindInstanceIndex(const USeinWorldSubsystem& World,
		const FSeinAbilityComponent& AC, PredT Pred)
	{
		for (int32 i = 0; i < AC.AbilityInstanceIDs.Num(); ++i)
		{
			const USeinAbility* Ab = World.GetAbilityInstance(AC.AbilityInstanceIDs[i]);
			if (Ab && Pred(Ab)) return i;
		}
		return INDEX_NONE;
	}

	static FSeinAbilityGrantOwnership& EnsureOwnershipRow(
		FSeinAbilityComponent& AC, int32 ParallelIndex)
	{
		while (AC.AbilityGrantOwnership.Num() <= ParallelIndex)
		{
			FSeinAbilityGrantOwnership Ownership;
			const int32 LegacyIndex = AC.AbilityGrantOwnership.Num();
			Ownership.AnonymousGrantCount = AC.AbilityGrantCounts.IsValidIndex(LegacyIndex)
				? FMath::Max(1, AC.AbilityGrantCounts[LegacyIndex])
				: 1;
			AC.AbilityGrantOwnership.Add(MoveTemp(Ownership));
		}
		return AC.AbilityGrantOwnership[ParallelIndex];
	}

	static int64 GetOwnershipTotal(const FSeinAbilityGrantOwnership& Ownership)
	{
		return static_cast<int64>(Ownership.AnonymousGrantCount)
			+ static_cast<int64>(Ownership.EffectInstanceIDs.Num());
	}

	/** Tear down an ability instance entry: cancel-if-active, drop from
	 *  parallel arrays, clear active/passive slots, unregister from pool,
	 *  remove class from GrantedAbilities. Caller is responsible for the
	 *  broker-dirty call. Component ownership rows are detached before
	 *  CancelAbility so reentrant grant/revoke cannot invalidate the index. The
	 *  old pool slot stays occupied until Cancel returns, preventing ID reuse. */
	static void DestroyInstanceAt(USeinWorldSubsystem& World,
		FSeinAbilityComponent& AC, int32 ParallelIndex)
	{
		if (!AC.AbilityInstanceIDs.IsValidIndex(ParallelIndex)) return;

		const int32 ID = AC.AbilityInstanceIDs[ParallelIndex];
		USeinAbility* Instance = World.GetAbilityInstance(ID);
		UClass* ClassToForget = nullptr;
		const bool bCancel = Instance && Instance->bIsActive;
		if (Instance)
		{
			ClassToForget = Instance->GetClass();
		}
		if (AC.ActiveAbilityID == ID)
		{
			AC.ActiveAbilityID = INDEX_NONE;
		}
		AC.ActivePassiveIDs.Remove(ID);

		AC.AbilityInstanceIDs.RemoveAt(ParallelIndex);
		if (AC.AbilityGrantCounts.IsValidIndex(ParallelIndex))
		{
			AC.AbilityGrantCounts.RemoveAt(ParallelIndex);
		}
		if (AC.AbilityGrantOwnership.IsValidIndex(ParallelIndex))
		{
			AC.AbilityGrantOwnership.RemoveAt(ParallelIndex);
		}

		if (ClassToForget)
		{
			AC.GrantedAbilities.Remove(ClassToForget);
		}
		if (bCancel)
		{
			Instance->CancelAbility();
		}
		World.UnregisterAbilityInstance(ID);
	}
}

namespace SeinAbilityGrantLocal
{
	static int32 GrantAbilityInternal(const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle, TSubclassOf<USeinAbility> AbilityClass,
		int64 EffectInstanceID)
	{
		UWorld* ContextWorld = WorldContextObject
			? GEngine->GetWorldFromContextObject(
				WorldContextObject, EGetWorldErrorMode::ReturnNull)
			: nullptr;
		USeinWorldSubsystem* Subsystem = ContextWorld
			? ContextWorld->GetSubsystem<USeinWorldSubsystem>()
			: nullptr;
		if (!Subsystem)
		{
			UE_LOG(LogSeinBPFL, Warning, TEXT("GrantAbility: no SeinWorldSubsystem"));
			return INDEX_NONE;
		}
		if (!Subsystem->RequireStateMutationAuthorization(TEXT("GrantAbility")))
		{
			return INDEX_NONE;
		}
		const FSeinEntity* Entity = Subsystem->GetEntityPool().Get(EntityHandle);
		if (!AbilityClass || AbilityClass->HasAnyClassFlags(CLASS_Abstract)
			|| !Entity || !Entity->IsAlive() || EffectInstanceID < 0)
		{
			UE_LOG(LogSeinBPFL, Warning,
				TEXT("GrantAbility: invalid entity, class, or source for entity %s"),
				*EntityHandle.ToString());
			return INDEX_NONE;
		}
		FSeinAbilityComponent* AbilityComp =
			Subsystem->GetComponentMutable<FSeinAbilityComponent>(
				EntityHandle);
		if (!AbilityComp)
		{
			UE_LOG(LogSeinBPFL, Warning,
				TEXT("GrantAbility: entity %s has no FSeinAbilityComponent"),
				*EntityHandle.ToString());
			return INDEX_NONE;
		}

		const UClass* TargetClass = AbilityClass.Get();
		const int32 ExistingIdx = FindInstanceIndex(*Subsystem, *AbilityComp,
			[TargetClass](const USeinAbility* Ab) { return Ab->GetClass() == TargetClass; });
		if (ExistingIdx != INDEX_NONE)
		{
			FSeinAbilityGrantOwnership& Ownership =
				EnsureOwnershipRow(*AbilityComp, ExistingIdx);
			const int64 PreviousTotal = GetOwnershipTotal(Ownership);
			if (PreviousTotal >= MAX_int32)
			{
				UE_LOG(LogSeinBPFL, Error,
					TEXT("GrantAbility: refcount saturated for %s on entity %s"),
					*AbilityClass->GetName(), *EntityHandle.ToString());
				return INDEX_NONE;
			}
			if (EffectInstanceID > 0)
			{
				Ownership.EffectInstanceIDs.Add(EffectInstanceID);
			}
			else
			{
				++Ownership.AnonymousGrantCount;
			}
			while (AbilityComp->AbilityGrantCounts.Num() <= ExistingIdx)
			{
				AbilityComp->AbilityGrantCounts.Add(1);
			}
			AbilityComp->AbilityGrantCounts[ExistingIdx] =
				static_cast<int32>(PreviousTotal + 1);
			return AbilityComp->AbilityInstanceIDs[ExistingIdx];
		}

		USeinAbility* Instance = NewObject<USeinAbility>(Subsystem, AbilityClass);
		Instance->InitializeAbility(EntityHandle, Subsystem);
		const int32 AbilityID = Subsystem->RegisterAbilityInstance(Instance);

		FSeinAbilityGrantOwnership Ownership;
		if (EffectInstanceID > 0)
		{
			Ownership.EffectInstanceIDs.Add(EffectInstanceID);
		}
		else
		{
			Ownership.AnonymousGrantCount = 1;
		}
		AbilityComp->GrantedAbilities.AddUnique(AbilityClass);
		AbilityComp->AbilityInstanceIDs.Add(AbilityID);
		AbilityComp->AbilityGrantCounts.Add(1);
		AbilityComp->AbilityGrantOwnership.Add(MoveTemp(Ownership));

		if (Instance->bIsPassive)
		{
			if (!Instance->ActivateAbility(EntityHandle, FFixedVector::ZeroVector))
			{
				AbilityComp =
					Subsystem->GetComponentMutable<FSeinAbilityComponent>(
						EntityHandle);
				const int32 FailedIndex = AbilityComp
					? AbilityComp->AbilityInstanceIDs.IndexOfByKey(AbilityID)
					: INDEX_NONE;
				if (AbilityComp && FailedIndex != INDEX_NONE)
				{
					DestroyInstanceAt(*Subsystem, *AbilityComp, FailedIndex);
				}
				return INDEX_NONE;
			}
			AbilityComp =
				Subsystem->GetComponentMutable<FSeinAbilityComponent>(
					EntityHandle);
			const int32 CurrentIndex = AbilityComp
				? AbilityComp->AbilityInstanceIDs.IndexOfByKey(AbilityID)
				: INDEX_NONE;
			const bool bSourceStillOwned = CurrentIndex != INDEX_NONE
				&& AbilityComp->AbilityGrantOwnership.IsValidIndex(CurrentIndex)
				&& (EffectInstanceID > 0
					? AbilityComp->AbilityGrantOwnership[CurrentIndex]
						.EffectInstanceIDs.Contains(EffectInstanceID)
					: AbilityComp->AbilityGrantOwnership[CurrentIndex]
						.AnonymousGrantCount > 0);
			if (!bSourceStillOwned || Subsystem->GetAbilityInstance(AbilityID) != Instance)
			{
				return INDEX_NONE;
			}
		}

		DirtyBrokerCapability(*Subsystem, EntityHandle);
		return AbilityID;
	}
}

int32 USeinAbilityBPFL::SeinGrantAbility(const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle, TSubclassOf<USeinAbility> AbilityClass)
{
	return SeinAbilityGrantLocal::GrantAbilityInternal(
		WorldContextObject, EntityHandle, AbilityClass, /*EffectInstanceID=*/0);
}

int32 USeinAbilityBPFL::SeinGrantAbilityFromEffect(const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle, TSubclassOf<USeinAbility> AbilityClass,
	int64 EffectInstanceID)
{
	if (EffectInstanceID <= 0) return INDEX_NONE;
	return SeinAbilityGrantLocal::GrantAbilityInternal(
		WorldContextObject, EntityHandle, AbilityClass, EffectInstanceID);
}

// ─────────────────────────────────────────────────────────────────────
// Revoke (refcount-aware)
// ─────────────────────────────────────────────────────────────────────
//
// Default revoke is reference-decrement: drops the grant count by 1 per
// matching instance, only destroying the instance when the count reaches
// zero. That keeps two grant-sources from clobbering each other (native +
// effect, effect + effect, scripted + effect). Returns the number of
// instances actually DESTROYED — callers wanting "did I decrement
// anything" should compare against the pre-call HasAbility / GrantCount.
//
// `SeinForceRevokeAbility*` below zeroes the refcount and destroys
// regardless of remaining holders — the explicit escape hatch when a
// designer wants the ability gone immediately.

namespace SeinAbilityGrantLocal
{
	/** Consume one source-owned reference. Effect-specific calls only consume a
	 *  matching effect ID. Aggregate calls prefer anonymous ownership, otherwise
	 *  consume the oldest effect source and prune its live ledger before any
	 *  callback-capable destruction. Returns 1 only when the instance dies. */
	static int32 ConsumeGrantAndMaybeDestroy(USeinWorldSubsystem& World,
		FSeinEntityHandle Entity, FSeinAbilityComponent& AC, int32 ParallelIndex,
		int64 RequiredEffectInstanceID = 0)
	{
		if (!AC.AbilityInstanceIDs.IsValidIndex(ParallelIndex)) return 0;
		USeinAbility* Ability = World.GetAbilityInstance(AC.AbilityInstanceIDs[ParallelIndex]);
		if (!Ability) return 0;
		const TSubclassOf<USeinAbility> AbilityClass = Ability->GetClass();
		FSeinAbilityGrantOwnership& Ownership = EnsureOwnershipRow(AC, ParallelIndex);

		int64 PrunedEffectID = 0;
		if (RequiredEffectInstanceID > 0)
		{
			const int32 SourceIndex = Ownership.EffectInstanceIDs.IndexOfByKey(
				RequiredEffectInstanceID);
			if (SourceIndex == INDEX_NONE) return 0;
			Ownership.EffectInstanceIDs.RemoveAt(SourceIndex, 1, EAllowShrinking::No);
		}
		else if (Ownership.AnonymousGrantCount > 0)
		{
			--Ownership.AnonymousGrantCount;
		}
		else if (!Ownership.EffectInstanceIDs.IsEmpty())
		{
			PrunedEffectID = Ownership.EffectInstanceIDs[0];
			Ownership.EffectInstanceIDs.RemoveAt(0, 1, EAllowShrinking::No);
		}
		else
		{
			return 0;
		}

		if (PrunedEffectID > 0)
		{
			World.PruneEffectAbilityGrantClaim(
				PrunedEffectID, Entity, AbilityClass);
		}
		const int64 Remaining = GetOwnershipTotal(Ownership);
		while (AC.AbilityGrantCounts.Num() <= ParallelIndex)
		{
			AC.AbilityGrantCounts.Add(1);
		}
		AC.AbilityGrantCounts[ParallelIndex] = static_cast<int32>(Remaining);
		if (Remaining > 0) return 0;
		DestroyInstanceAt(World, AC, ParallelIndex);
		return 1;
	}
}

int32 USeinAbilityBPFL::SeinRevokeAbilityByTag(const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle,
	FGameplayTag AbilityTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !AbilityTag.IsValid()
		|| !Subsystem->RequireStateMutationAuthorization(
			TEXT("RevokeAbilityByTag")))
	{
		return 0;
	}
	FSeinAbilityComponent* AbilityComp =
		Subsystem->GetComponentMutable<FSeinAbilityComponent>(
			EntityHandle);
	if (!AbilityComp) return 0;

	// Collect parallel-indices first — mutating during iteration invalidates
	// cursors. Walk forward; defer the decrement-and-maybe-destroy pass so
	// we can iterate matching IDs by snapshot. Multiple instances may share
	// a tag (rare but legal: two different ability classes with the same
	// AbilityTag for grouping). Each gets one decrement here.
	TArray<int32> MatchingIDs;
	for (int32 ID : AbilityComp->AbilityInstanceIDs)
	{
		const USeinAbility* Ab = Subsystem->GetAbilityInstance(ID);
		if (Ab && Ab->AbilityTag == AbilityTag)
		{
			MatchingIDs.Add(ID);
		}
	}
	if (MatchingIDs.Num() == 0)
	{
		UE_LOG(LogSeinBPFL, Verbose,
			TEXT("RevokeAbilityByTag: entity %s has no instance of tag %s"),
			*EntityHandle.ToString(), *AbilityTag.ToString());
		return 0;
	}

	int32 NumDestroyed = 0;
	for (int32 ID : MatchingIDs)
	{
		AbilityComp =
			Subsystem->GetComponentMutable<FSeinAbilityComponent>(
				EntityHandle);
		if (!AbilityComp) break;
		const int32 Idx = AbilityComp->AbilityInstanceIDs.IndexOfByKey(ID);
		if (Idx == INDEX_NONE) continue;  // already removed by a prior pass
		NumDestroyed += SeinAbilityGrantLocal::ConsumeGrantAndMaybeDestroy(
			*Subsystem, EntityHandle, *AbilityComp, Idx);
	}

	if (NumDestroyed > 0)
	{
		SeinAbilityGrantLocal::DirtyBrokerCapability(*Subsystem, EntityHandle);
	}

	UE_LOG(LogSeinBPFL, Verbose,
		TEXT("RevokeAbilityByTag: entity %s — tag %s — %d instance(s) destroyed, %d decremented"),
		*EntityHandle.ToString(), *AbilityTag.ToString(),
		NumDestroyed, MatchingIDs.Num() - NumDestroyed);

	return NumDestroyed;
}

int32 USeinAbilityBPFL::SeinRevokeAbilityByClass(const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle,
	TSubclassOf<USeinAbility> AbilityClass)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !AbilityClass
		|| !Subsystem->RequireStateMutationAuthorization(
			TEXT("RevokeAbilityByClass")))
	{
		return 0;
	}
	FSeinAbilityComponent* AbilityComp =
		Subsystem->GetComponentMutable<FSeinAbilityComponent>(
			EntityHandle);
	if (!AbilityComp) return 0;

	// One class = one instance per entity (grant is idempotent on class).
	const UClass* TargetClass = AbilityClass.Get();
	const int32 Idx = SeinAbilityGrantLocal::FindInstanceIndex(*Subsystem, *AbilityComp,
		[TargetClass](const USeinAbility* Ab) { return Ab->GetClass() == TargetClass; });
	if (Idx == INDEX_NONE)
	{
		UE_LOG(LogSeinBPFL, Verbose,
			TEXT("RevokeAbilityByClass: entity %s has no instance of class %s"),
			*EntityHandle.ToString(), *AbilityClass->GetName());
		return 0;
	}

	const int32 NumDestroyed = SeinAbilityGrantLocal::ConsumeGrantAndMaybeDestroy(
		*Subsystem, EntityHandle, *AbilityComp, Idx);

	if (NumDestroyed > 0)
	{
		SeinAbilityGrantLocal::DirtyBrokerCapability(*Subsystem, EntityHandle);
	}

	UE_LOG(LogSeinBPFL, Verbose,
		TEXT("RevokeAbilityByClass: entity %s — class %s — %s"),
		*EntityHandle.ToString(), *AbilityClass->GetName(),
		NumDestroyed > 0 ? TEXT("instance destroyed") : TEXT("refcount decremented (other holders remain)"));

	return NumDestroyed;
}

int32 USeinAbilityBPFL::SeinRevokeAbilityFromEffect(const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle, TSubclassOf<USeinAbility> AbilityClass,
	int64 EffectInstanceID)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !AbilityClass || EffectInstanceID <= 0
		|| !Subsystem->RequireStateMutationAuthorization(
			TEXT("RevokeAbilityFromEffect")))
	{
		return 0;
	}
	FSeinAbilityComponent* AbilityComp =
		Subsystem->GetComponentMutable<FSeinAbilityComponent>(
			EntityHandle);
	if (!AbilityComp) return 0;
	const UClass* TargetClass = AbilityClass.Get();
	const int32 Idx = SeinAbilityGrantLocal::FindInstanceIndex(
		*Subsystem, *AbilityComp,
		[TargetClass](const USeinAbility* Ability)
		{
			return Ability->GetClass() == TargetClass;
		});
	if (Idx == INDEX_NONE) return 0;
	const int32 NumDestroyed = SeinAbilityGrantLocal::ConsumeGrantAndMaybeDestroy(
		*Subsystem, EntityHandle, *AbilityComp, Idx, EffectInstanceID);
	if (NumDestroyed > 0)
	{
		SeinAbilityGrantLocal::DirtyBrokerCapability(*Subsystem, EntityHandle);
	}
	return NumDestroyed;
}

int32 USeinAbilityBPFL::SeinForceRevokeAbilityByTag(const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle,
	FGameplayTag AbilityTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !AbilityTag.IsValid()
		|| !Subsystem->RequireStateMutationAuthorization(
			TEXT("ForceRevokeAbilityByTag")))
	{
		return 0;
	}
	FSeinAbilityComponent* AbilityComp =
		Subsystem->GetComponentMutable<FSeinAbilityComponent>(
			EntityHandle);
	if (!AbilityComp) return 0;

	// Snapshot matching IDs, then destroy each regardless of refcount.
	TArray<int32> MatchingIDs;
	for (int32 ID : AbilityComp->AbilityInstanceIDs)
	{
		const USeinAbility* Ab = Subsystem->GetAbilityInstance(ID);
		if (Ab && Ab->AbilityTag == AbilityTag)
		{
			MatchingIDs.Add(ID);
		}
	}
	if (MatchingIDs.Num() == 0) return 0;

	int32 NumDestroyed = 0;
	for (int32 ID : MatchingIDs)
	{
		AbilityComp =
			Subsystem->GetComponentMutable<FSeinAbilityComponent>(
				EntityHandle);
		if (!AbilityComp) break;
		const int32 Idx = AbilityComp->AbilityInstanceIDs.IndexOfByKey(ID);
		if (Idx == INDEX_NONE) continue;
		if (const USeinAbility* Ability = Subsystem->GetAbilityInstance(ID))
		{
			Subsystem->PruneAllEffectAbilityGrantClaims(
				EntityHandle, Ability->GetClass());
		}
		SeinAbilityGrantLocal::DestroyInstanceAt(*Subsystem, *AbilityComp, Idx);
		++NumDestroyed;
	}

	if (NumDestroyed > 0)
	{
		SeinAbilityGrantLocal::DirtyBrokerCapability(*Subsystem, EntityHandle);
	}

	UE_LOG(LogSeinBPFL, Log,
		TEXT("ForceRevokeAbilityByTag: entity %s force-destroyed %d instance(s) of tag %s"),
		*EntityHandle.ToString(), NumDestroyed, *AbilityTag.ToString());

	return NumDestroyed;
}

int32 USeinAbilityBPFL::SeinForceRevokeAbilityByClass(const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle,
	TSubclassOf<USeinAbility> AbilityClass)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !AbilityClass
		|| !Subsystem->RequireStateMutationAuthorization(
			TEXT("ForceRevokeAbilityByClass")))
	{
		return 0;
	}
	FSeinAbilityComponent* AbilityComp =
		Subsystem->GetComponentMutable<FSeinAbilityComponent>(
			EntityHandle);
	if (!AbilityComp) return 0;

	const UClass* TargetClass = AbilityClass.Get();
	const int32 Idx = SeinAbilityGrantLocal::FindInstanceIndex(*Subsystem, *AbilityComp,
		[TargetClass](const USeinAbility* Ab) { return Ab->GetClass() == TargetClass; });
	if (Idx == INDEX_NONE) return 0;

	Subsystem->PruneAllEffectAbilityGrantClaims(EntityHandle, AbilityClass);
	SeinAbilityGrantLocal::DestroyInstanceAt(*Subsystem, *AbilityComp, Idx);
	SeinAbilityGrantLocal::DirtyBrokerCapability(*Subsystem, EntityHandle);

	UE_LOG(LogSeinBPFL, Log,
		TEXT("ForceRevokeAbilityByClass: entity %s force-destroyed instance of %s"),
		*EntityHandle.ToString(), *AbilityClass->GetName());

	return 1;
}

bool USeinAbilityBPFL::SeinHasAbilityOfClass(const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle,
	TSubclassOf<USeinAbility> AbilityClass)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !AbilityClass) return false;
	const FSeinAbilityComponent* AbilityComp = Subsystem->GetComponent<FSeinAbilityComponent>(EntityHandle);
	if (!AbilityComp) return false;
	return AbilityComp->HasAbilityOfClass(*Subsystem, AbilityClass.Get());
}

int32 USeinAbilityBPFL::SeinGetAbilityGrantCount(const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle,
	TSubclassOf<USeinAbility> AbilityClass)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !AbilityClass) return 0;
	const FSeinAbilityComponent* AbilityComp = Subsystem->GetComponent<FSeinAbilityComponent>(EntityHandle);
	if (!AbilityComp) return 0;
	return AbilityComp->GetAbilityGrantCount(*Subsystem, AbilityClass.Get());
}

bool USeinAbilityBPFL::SeinIsAbilityOnCooldown(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const FSeinAbilityComponent* AbilityComp = Subsystem->GetComponent<FSeinAbilityComponent>(EntityHandle);
	if (!AbilityComp) return false;

	if (const USeinAbility* Ability = AbilityComp->FindAbilityByTag(*Subsystem, AbilityTag))
	{
		return Ability->IsOnCooldown();
	}
	return false;
}

FFixedPoint USeinAbilityBPFL::SeinGetCooldownRemaining(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return FFixedPoint::Zero;

	const FSeinAbilityComponent* AbilityComp = Subsystem->GetComponent<FSeinAbilityComponent>(EntityHandle);
	if (!AbilityComp) return FFixedPoint::Zero;

	if (const USeinAbility* Ability = AbilityComp->FindAbilityByTag(*Subsystem, AbilityTag))
	{
		return Ability->CooldownRemaining;
	}
	return FFixedPoint::Zero;
}

bool USeinAbilityBPFL::SeinHasAbility(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag AbilityTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	const FSeinAbilityComponent* AbilityComp = Subsystem->GetComponent<FSeinAbilityComponent>(EntityHandle);
	if (!AbilityComp) return false;

	return AbilityComp->HasAbilityWithTag(*Subsystem, AbilityTag);
}

FSeinAbilityAvailability USeinAbilityBPFL::SeinGetAbilityAvailability(
	const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle,
	FGameplayTag AbilityTag,
	FSeinEntityHandle OptionalTargetEntity,
	FFixedVector OptionalTargetLocation)
{
	FSeinAbilityAvailability Out;
	Out.AbilityTag = AbilityTag;

	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) { Out.Reason = ESeinAbilityUnavailableReason::UnknownAbility; return Out; }

	const FSeinAbilityComponent* AbilityComp = Subsystem->GetComponent<FSeinAbilityComponent>(EntityHandle);
	if (!AbilityComp) { Out.Reason = ESeinAbilityUnavailableReason::UnknownAbility; return Out; }

	USeinAbility* Ability = AbilityComp->FindAbilityByTag(*Subsystem, AbilityTag);
	if (!Ability) { Out.Reason = ESeinAbilityUnavailableReason::UnknownAbility; return Out; }

	Out.CooldownRemaining = Ability->CooldownRemaining;
	Out.bIsActive = Ability->bIsActive;

	const FSeinPlayerID Owner = Subsystem->GetEntityOwner(EntityHandle);
	FSeinResourceCost ActivationCost;
	FSeinResourceCost CompletionCostUnused;
	Ability->ResolveActivationCosts(
		WorldContextObject, ActivationCost, CompletionCostUnused);
	Out.bCanAfford = USeinResourceBPFL::SeinCanAfford(
		WorldContextObject, Owner, ActivationCost);

	// Walk the same gate order as ProcessCommands::ActivateAbility and report
	// the first failing gate.
	if (Ability->IsOnCooldown())
	{
		Out.Reason = ESeinAbilityUnavailableReason::OnCooldown;
		return Out;
	}

	if (!Ability->BlockedTags.IsEmpty())
	{
		if (Subsystem->HasAnyTag(EntityHandle, Ability->BlockedTags))
		{
			Out.Reason = ESeinAbilityUnavailableReason::BlockedByTag;
			return Out;
		}
	}

	// Mirror the activation gate's RequiredEntityTags / RequiredPlayerTags checks
	// so UI greys-out for the same reasons the gate would reject. (DESIGN §7)
	if (!Ability->RequiredEntityTags.IsEmpty())
	{
		if (!Subsystem->HasAllTags(EntityHandle, Ability->RequiredEntityTags))
		{
			Out.Reason = ESeinAbilityUnavailableReason::BlockedByTag;
			return Out;
		}
	}
	if (!Ability->RequiredPlayerTags.IsEmpty())
	{
		const FSeinPlayerState* PS = Subsystem->GetPlayerState(Owner);
		if (!PS || !PS->HasAllPlayerTags(Ability->RequiredPlayerTags))
		{
			Out.Reason = ESeinAbilityUnavailableReason::BlockedByTag;
			return Out;
		}
	}

	// Target-validation gates (range / LOS / ValidTargetTags) only run when the
	// caller actually supplies target context. UI callers use this BPFL to ask
	// "is this ability startable in principle?" — they have no per-button target,
	// so they pass an invalid handle + zero vector and expect those gates to be
	// skipped. Without this guard, abilities with MaxRange > 0 would compare
	// owner→origin distance and falsely fire OutOfRange every refresh.
	//
	// Contract: pass an invalid Target AND zero Location to skip target-validation
	// gates. Pass a valid Target OR a non-zero Location to run them (per-click
	// preview, e.g. "would this work if the player clicked HERE").
	const bool bHasTargetContext = OptionalTargetEntity.IsValid() || !OptionalTargetLocation.IsZero();
	if (bHasTargetContext)
	{
		const ESeinAbilityTargetValidationResult Validation = FSeinAbilityValidation::ValidateTarget(
			*Ability, EntityHandle, OptionalTargetEntity, OptionalTargetLocation, *Subsystem);
		switch (Validation)
		{
			case ESeinAbilityTargetValidationResult::OutOfRange:
			{
				const USeinAbility* MoveAbility =
					AbilityComp->FindMoveAbility(*Subsystem);
				if (Ability->OutOfRangeBehavior
						!= ESeinOutOfRangeBehavior::AutoMoveThen
					|| !MoveAbility || !MoveAbility->AbilityTag.IsValid())
				{
					Out.Reason = ESeinAbilityUnavailableReason::OutOfRange;
					return Out;
				}
				// AutoMoveThen's click-time command preflight checks affordability,
				// then defers the remaining gates to the eventual follow-up.
				Out.bAvailable = Out.bCanAfford;
				Out.Reason = Out.bCanAfford
					? ESeinAbilityUnavailableReason::None
					: ESeinAbilityUnavailableReason::Unaffordable;
				return Out;
			}
			case ESeinAbilityTargetValidationResult::InvalidTarget:
				Out.Reason = ESeinAbilityUnavailableReason::InvalidTarget; return Out;
			case ESeinAbilityTargetValidationResult::NoLineOfSight:
				Out.Reason = ESeinAbilityUnavailableReason::NoLineOfSight; return Out;
			default: break;
		}
	}

	if (!Ability->CanActivate())
	{
		Out.Reason = ESeinAbilityUnavailableReason::CanActivateFailed;
		return Out;
	}

	if (!Out.bCanAfford)
	{
		Out.Reason = ESeinAbilityUnavailableReason::Unaffordable;
		return Out;
	}

	Out.bAvailable = true;
	Out.Reason = ESeinAbilityUnavailableReason::None;
	return Out;
}

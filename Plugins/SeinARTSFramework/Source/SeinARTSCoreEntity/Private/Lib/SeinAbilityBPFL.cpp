/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAbilityBPFL.cpp
 * @brief   Implementation of ability system Blueprint nodes.
 */

#include "Lib/SeinAbilityBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinCommandBrokerData.h"
#include "Core/SeinPlayerState.h"
#include "Abilities/SeinAbility.h"
#include "Abilities/SeinAbilityValidation.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Input/SeinCommand.h"
#include "Lib/SeinResourceBPFL.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinBPFL, Log, All);

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
	if (!Subsystem) return;

	FSeinAbilityComponent* AbilityComp = Subsystem->GetComponent<FSeinAbilityComponent>(EntityHandle);
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
	Subsystem->EnqueueCommand(Cmd);
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

	FSeinCommandBrokerData* Broker = Subsystem->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
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
	if (!Subsystem) return;

	FSeinAbilityComponent* AbilityComp = Subsystem->GetComponent<FSeinAbilityComponent>(EntityHandle);
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
			if (FSeinCommandBrokerData* Broker = World.GetComponent<FSeinCommandBrokerData>(Memb->CurrentBrokerHandle))
			{
				Broker->bCapabilityMapDirty = true;
			}
		}
		// Also mark the entity's own broker dirty if THIS entity is a broker
		// carrier (squad entity). Squad members route through the line above;
		// the squad entity itself is the broker carrier and isn't a member of
		// any other broker.
		if (FSeinCommandBrokerData* OwnBroker = World.GetComponent<FSeinCommandBrokerData>(Entity))
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

	/** Tear down an ability instance entry: cancel-if-active, drop from
	 *  parallel arrays, clear active/passive slots, unregister from pool,
	 *  remove class from GrantedAbilities. Caller is responsible for the
	 *  broker-dirty call. `ParallelIndex` is the index into both
	 *  AbilityInstanceIDs and AbilityGrantCounts. */
	static void DestroyInstanceAt(USeinWorldSubsystem& World,
		FSeinAbilityComponent& AC, int32 ParallelIndex)
	{
		if (!AC.AbilityInstanceIDs.IsValidIndex(ParallelIndex)) return;

		const int32 ID = AC.AbilityInstanceIDs[ParallelIndex];
		UClass* ClassToForget = nullptr;
		if (USeinAbility* Ab = World.GetAbilityInstance(ID))
		{
			if (Ab->bIsActive)
			{
				Ab->CancelAbility();
			}
			ClassToForget = Ab->GetClass();
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
		World.UnregisterAbilityInstance(ID);

		if (ClassToForget)
		{
			AC.GrantedAbilities.Remove(ClassToForget);
		}
	}
}

int32 USeinAbilityBPFL::SeinGrantAbility(const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle,
	TSubclassOf<USeinAbility> AbilityClass)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("GrantAbility: no SeinWorldSubsystem"));
		return INDEX_NONE;
	}
	if (!AbilityClass || AbilityClass->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("GrantAbility: null or abstract AbilityClass for entity %s"),
			*EntityHandle.ToString());
		return INDEX_NONE;
	}
	FSeinAbilityComponent* AbilityComp = Subsystem->GetComponent<FSeinAbilityComponent>(EntityHandle);
	if (!AbilityComp)
	{
		UE_LOG(LogSeinBPFL, Warning,
			TEXT("GrantAbility: entity %s has no FSeinAbilityComponent — author one on its entity bridge's ComponentData before granting"),
			*EntityHandle.ToString());
		return INDEX_NONE;
	}

	// Refcounted idempotency. If the entity already holds an instance of
	// this class, bump its refcount and return the existing pool ID. A
	// matching `SeinRevokeAbility*` call elsewhere only fully destroys the
	// instance when the count drops back to zero — so two grant-sources
	// (native authoring + an effect, two effects, etc.) coexist safely.
	const UClass* TargetClass = AbilityClass.Get();
	const int32 ExistingIdx = SeinAbilityGrantLocal::FindInstanceIndex(*Subsystem, *AbilityComp,
		[TargetClass](const USeinAbility* Ab) { return Ab->GetClass() == TargetClass; });
	if (ExistingIdx != INDEX_NONE)
	{
		// Bump the parallel-array refcount entry. Auto-pad the count array if
		// it's somehow lagging behind (defensive — legacy snapshots loaded
		// before this field existed will have empty AbilityGrantCounts).
		while (AbilityComp->AbilityGrantCounts.Num() <= ExistingIdx)
		{
			AbilityComp->AbilityGrantCounts.Add(1);
		}
		AbilityComp->AbilityGrantCounts[ExistingIdx] += 1;

		const int32 ExistingID = AbilityComp->AbilityInstanceIDs[ExistingIdx];
		UE_LOG(LogSeinBPFL, Verbose,
			TEXT("GrantAbility: entity %s already holds %s (id=%d) — refcount now %d"),
			*EntityHandle.ToString(), *AbilityClass->GetName(),
			ExistingID, AbilityComp->AbilityGrantCounts[ExistingIdx]);
		return ExistingID;
	}

	// New grant — instantiate via the same pipeline as the spawn-time native
	// path (which itself now routes through this BPFL for refcount
	// consistency).
	USeinAbility* Instance = NewObject<USeinAbility>(Subsystem, AbilityClass);
	Instance->InitializeAbility(EntityHandle, Subsystem);
	const int32 AbilityID = Subsystem->RegisterAbilityInstance(Instance);

	// Track in component. Adding the class to GrantedAbilities means a
	// save/reload that re-runs InitializeEntityAbilities picks up the
	// runtime-granted class too. Parallel `AbilityGrantCounts` entry seeds
	// at 1 — the count balances against the eventual matching revoke.
	AbilityComp->GrantedAbilities.AddUnique(AbilityClass);
	AbilityComp->AbilityInstanceIDs.Add(AbilityID);
	AbilityComp->AbilityGrantCounts.Add(1);

	if (Instance->bIsPassive)
	{
		Instance->ActivateAbility(EntityHandle, FFixedVector::ZeroVector);
		AbilityComp->ActivePassiveIDs.Add(AbilityID);
	}

	SeinAbilityGrantLocal::DirtyBrokerCapability(*Subsystem, EntityHandle);

	UE_LOG(LogSeinBPFL, Log,
		TEXT("GrantAbility: entity %s granted %s [tag=%s passive=%d id=%d refcount=1]"),
		*EntityHandle.ToString(), *AbilityClass->GetName(),
		*Instance->AbilityTag.ToString(), Instance->bIsPassive ? 1 : 0, AbilityID);

	return AbilityID;
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
	/** Decrement the refcount at `ParallelIndex`. If it hits zero, tear down
	 *  the instance via DestroyInstanceAt. Returns 1 if the instance was
	 *  destroyed, 0 otherwise. Pads the count array if it lags (legacy
	 *  snapshots) so the first revoke on a pre-refcount instance still
	 *  reaches zero in one call. */
	static int32 DecrementAndMaybeDestroy(USeinWorldSubsystem& World,
		FSeinAbilityComponent& AC, int32 ParallelIndex)
	{
		if (!AC.AbilityInstanceIDs.IsValidIndex(ParallelIndex)) return 0;

		while (AC.AbilityGrantCounts.Num() <= ParallelIndex)
		{
			AC.AbilityGrantCounts.Add(1);
		}
		int32& Count = AC.AbilityGrantCounts[ParallelIndex];
		if (Count > 1)
		{
			Count -= 1;
			return 0;
		}
		// Count is 1 (or 0 — defensively destroy in either case).
		DestroyInstanceAt(World, AC, ParallelIndex);
		return 1;
	}
}

int32 USeinAbilityBPFL::SeinRevokeAbilityByTag(const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle,
	FGameplayTag AbilityTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !AbilityTag.IsValid()) return 0;
	FSeinAbilityComponent* AbilityComp = Subsystem->GetComponent<FSeinAbilityComponent>(EntityHandle);
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
		const int32 Idx = AbilityComp->AbilityInstanceIDs.IndexOfByKey(ID);
		if (Idx == INDEX_NONE) continue;  // already removed by a prior pass
		NumDestroyed += SeinAbilityGrantLocal::DecrementAndMaybeDestroy(*Subsystem, *AbilityComp, Idx);
	}

	if (NumDestroyed > 0)
	{
		SeinAbilityGrantLocal::DirtyBrokerCapability(*Subsystem, EntityHandle);
	}

	UE_LOG(LogSeinBPFL, Log,
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
	if (!Subsystem || !AbilityClass) return 0;
	FSeinAbilityComponent* AbilityComp = Subsystem->GetComponent<FSeinAbilityComponent>(EntityHandle);
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

	const int32 NumDestroyed = SeinAbilityGrantLocal::DecrementAndMaybeDestroy(*Subsystem, *AbilityComp, Idx);

	if (NumDestroyed > 0)
	{
		SeinAbilityGrantLocal::DirtyBrokerCapability(*Subsystem, EntityHandle);
	}

	UE_LOG(LogSeinBPFL, Log,
		TEXT("RevokeAbilityByClass: entity %s — class %s — %s"),
		*EntityHandle.ToString(), *AbilityClass->GetName(),
		NumDestroyed > 0 ? TEXT("instance destroyed") : TEXT("refcount decremented (other holders remain)"));

	return NumDestroyed;
}

int32 USeinAbilityBPFL::SeinForceRevokeAbilityByTag(const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle,
	FGameplayTag AbilityTag)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !AbilityTag.IsValid()) return 0;
	FSeinAbilityComponent* AbilityComp = Subsystem->GetComponent<FSeinAbilityComponent>(EntityHandle);
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
		const int32 Idx = AbilityComp->AbilityInstanceIDs.IndexOfByKey(ID);
		if (Idx == INDEX_NONE) continue;
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
	if (!Subsystem || !AbilityClass) return 0;
	FSeinAbilityComponent* AbilityComp = Subsystem->GetComponent<FSeinAbilityComponent>(EntityHandle);
	if (!AbilityComp) return 0;

	const UClass* TargetClass = AbilityClass.Get();
	const int32 Idx = SeinAbilityGrantLocal::FindInstanceIndex(*Subsystem, *AbilityComp,
		[TargetClass](const USeinAbility* Ab) { return Ab->GetClass() == TargetClass; });
	if (Idx == INDEX_NONE) return 0;

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
	Out.bCanAfford = USeinResourceBPFL::SeinCanAfford(WorldContextObject, Owner, Ability->ResourceCost);

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
				Out.Reason = ESeinAbilityUnavailableReason::OutOfRange; return Out;
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

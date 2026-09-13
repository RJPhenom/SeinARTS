/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinWorldProductionPolicy.cpp
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Enforces production allowances and runtime overrides on the simulation spine.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */
#include "Simulation/SeinWorldSubsystem.h"
#include "Actor/SeinActor.h"
#include "Actor/SeinEntityBridgeComponent.h"
#include "Components/SeinProduciblePayload.h"
#include "Components/SeinProductionPayload.h"

namespace
{
	const FSeinProduciblePayload* FindProducible(TSubclassOf<ASeinActor> Class)
	{
		if (!Class || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) return nullptr;
		TArray<const USeinEntityBridgeComponent*> Bridges;
		AActor::GetActorClassDefaultComponents<USeinEntityBridgeComponent>(Class, Bridges);
		for (const auto* Bridge : Bridges)
		{
			if (Bridge)
			{
				if (const auto* Data = Bridge->FindAuthoredData<FSeinProduciblePayload>()) return Data;
			}
		}
		return nullptr;
	}

	bool Increment(FSeinProductionPolicyState& State, const FString& Key)
	{
		int64& Count = State.Completed.FindOrAdd(Key);
		if (Count == MAX_int64) return false;
		++Count;
		return true;
	}

	void Decrement(FSeinProductionPolicyState& State, const FString& Key)
	{
		if (int64* Count = State.Completed.Find(Key))
		{
			if (*Count > 1) --*Count;
			else State.Completed.Remove(Key);
		}
	}
}

ESeinProductionQueueResult USeinWorldSubsystem::CheckProductionQueue(
	FSeinEntityHandle Producer, TSubclassOf<ASeinActor> ProducibleClass,
	FSeinProductionQueueSettings& OutSettings, int64& OutUsed) const
{
	OutSettings = FSeinProductionQueueSettings();
	OutUsed = 0;
	const auto* Entity = GetEntity(Producer);
	if (!Entity || !Entity->IsAlive()) return ESeinProductionQueueResult::InvalidProducer;
	const auto* Production = GetComponent<FSeinProductionPayload>(Producer);
	if (!Production) return ESeinProductionQueueResult::MissingProduction;
	const auto* Authored = FindProducible(ProducibleClass);
	if (!Authored) return ESeinProductionQueueResult::InvalidProducible;
	if (Authored->bIsResearch)
	{
		UClass* EffectClass = Authored->GrantedTechEffect.Get();
		if (!EffectClass || EffectClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
			|| !GetDefault<USeinEffect>(EffectClass)) return ESeinProductionQueueResult::InvalidProducible;
	}
	OutSettings.QueuePolicy = Authored->QueuePolicy;
	OutSettings.QueueAmount = Authored->QueueAmount;
	const FString Key = ProducibleClass->GetPathName();
	const FSeinPlayerID Player = GetEntityOwner(Producer);
	const auto* PlayerState = GetPlayerState(Player);
	const auto* History = GetComponent<FSeinProductionHistoryPayload>(Producer);
	if (PlayerState)
	{
		if (const auto* Override = PlayerState->ProductionPolicyState.Overrides.Find(Key)) OutSettings = *Override;
	}
	if (History)
	{
		if (const auto* Override = History->State.Overrides.Find(Key)) OutSettings = *Override;
	}
	if (!OutSettings.IsValid()) return ESeinProductionQueueResult::InvalidPolicy;
	const bool bPlayer = OutSettings.IsPlayerScoped();
	if (bPlayer && !PlayerState) return ESeinProductionQueueResult::InvalidProducer;
	OutUsed = bPlayer ? PlayerState->ProductionPolicyState.Completed.FindRef(Key)
		: (History ? History->State.Completed.FindRef(Key) : 0);
	const auto CountQueue = [&](const FSeinProductionPayload& Queue)
	{
		for (const auto& Entry : Queue.Queue)
		{
			if (Entry.ActorClass == ProducibleClass && OutUsed < MAX_int64) ++OutUsed;
		}
	};
	// Pending reservations are the queues themselves. This avoids a second mutable
	// count drifting after cancellation, component removal, or producer destruction.
	if (bPlayer)
	{
		if (const auto* Storage = GetComponentStorageRaw(FSeinProductionPayload::StaticStruct()))
		{
			Storage->ForEachLiveComponent([&](FSeinEntityHandle Handle, const void* Raw)
			{
				const auto* Candidate = GetEntity(Handle);
				if (Candidate && Candidate->IsAlive() && GetEntityOwner(Handle) == Player)
					CountQueue(*static_cast<const FSeinProductionPayload*>(Raw));
			});
		}
	}
	else CountQueue(*Production);
	if (!Production->CanQueueMore()) return ESeinProductionQueueResult::QueueFull;
	const int32 Limit = OutSettings.GetLimit();
	return Limit > 0 && OutUsed >= Limit
		? ESeinProductionQueueResult::LimitReached : ESeinProductionQueueResult::Available;
}

bool USeinWorldSubsystem::SetProducerQueueSettings(FSeinEntityHandle Producer,
	TSubclassOf<ASeinActor> ProducibleClass, const FSeinProductionQueueSettings* Settings)
{
	if (!RequireStateMutationAuthorization(TEXT("SetProducerQueueSettings"))) return false;
	const auto* Entity = GetEntity(Producer);
	if (!Entity || !Entity->IsAlive() || !FindProducible(ProducibleClass)
		|| (Settings && !Settings->IsValid())) return false;
	auto* History = GetComponentMutable<FSeinProductionHistoryPayload>(Producer);
	if (!History && Settings)
	{
		AddComponent(Producer, FSeinProductionHistoryPayload());
		History = GetComponentMutable<FSeinProductionHistoryPayload>(Producer);
		if (!History) return false;
	}
	if (History)
	{
		const FString Key = ProducibleClass->GetPathName();
		if (Settings) History->State.Overrides.Add(Key, *Settings);
		else History->State.Overrides.Remove(Key);
	}
	return true;
}

bool USeinWorldSubsystem::SetPlayerQueueSettings(FSeinPlayerID Player,
	TSubclassOf<ASeinActor> ProducibleClass, const FSeinProductionQueueSettings* Settings)
{
	if (!RequireStateMutationAuthorization(TEXT("SetPlayerQueueSettings"))) return false;
	if (!FindProducible(ProducibleClass) || (Settings && !Settings->IsValid())) return false;
	auto* State = GetPlayerStateMutable(Player);
	if (!State) return false;
	const FString Key = ProducibleClass->GetPathName();
	if (Settings) State->ProductionPolicyState.Overrides.Add(Key, *Settings);
	else State->ProductionPolicyState.Overrides.Remove(Key);
	return true;
}

FSeinProductionCompletionClaim USeinWorldSubsystem::BeginProductionCompletion(
	FSeinEntityHandle Producer, FSeinPlayerID Player, TSubclassOf<ASeinActor> ProducibleClass)
{
	FSeinProductionCompletionClaim Claim;
	if (!RequireStateMutationAuthorization(TEXT("BeginProductionCompletion")) || !ProducibleClass) return Claim;
	Claim.Producer = Producer;
	Claim.Player = Player;
	Claim.ClassPath = ProducibleClass->GetPathName();
	if (!GetComponent<FSeinProductionHistoryPayload>(Producer)) AddComponent(Producer, FSeinProductionHistoryPayload());
	if (auto* History = GetComponentMutable<FSeinProductionHistoryPayload>(Producer))
		Claim.bProducerIncremented = Increment(History->State, Claim.ClassPath);
	if (auto* State = GetPlayerStateMutable(Player))
		Claim.bPlayerIncremented = Increment(State->ProductionPolicyState, Claim.ClassPath);
	return Claim;
}

void USeinWorldSubsystem::RollBackProductionCompletion(const FSeinProductionCompletionClaim& Claim)
{
	if (!RequireStateMutationAuthorization(TEXT("RollBackProductionCompletion"))) return;
	if (Claim.bProducerIncremented)
	{
		if (auto* History = GetComponentMutable<FSeinProductionHistoryPayload>(Claim.Producer))
			Decrement(History->State, Claim.ClassPath);
	}
	if (Claim.bPlayerIncremented)
	{
		if (auto* State = GetPlayerStateMutable(Claim.Player))
			Decrement(State->ProductionPolicyState, Claim.ClassPath);
	}
}

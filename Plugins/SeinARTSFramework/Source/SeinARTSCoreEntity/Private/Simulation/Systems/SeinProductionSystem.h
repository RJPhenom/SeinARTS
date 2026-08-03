/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinProductionSystem.h
 * @brief   Advances production queues. Handles the DESIGN §9 stall-at-completion
 *          semantics (front entry reaches 100% but waits for its AtCompletion
 *          cost to fit) and routes research completion through the unified
 *          effect apply pipeline per DESIGN §10.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Core/SeinSystemPriority.h"
#include "Core/SeinPlayerState.h"
#include "SeinARTSCoreEntityLog.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/ComponentStorage.h"
#include "Components/SeinProductionComponent.h"
#include "Components/SeinIdentityComponent.h"
#include "Components/SeinAbilityComponent.h"
#include "Actor/SeinEntityComponent.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Effects/SeinEffect.h"
#include "Events/SeinVisualEvent.h"
#include "Lib/SeinResourceBPFL.h"
#include "Tags/SeinARTSGameplayTags.h"

namespace SeinProductionLocal
{
	struct FReadyCompletion
	{
		FSeinProductionQueueEntry Entry;
		FSeinPlayerID ProducerOwner;
		FFixedTransform SpawnTransform;
		bool bRallyToEntity = false;
		FFixedTransform RallyTransform;
		FSeinEntityHandle RallyEntity;
		FFixedPoint OriginalProgress;
		bool bOriginalStalled = false;
	};

	/** Read FSeinIdentityComponent::IdentityTag off the producible class's
	 *  CDO entity bridge ComponentData. Returns an invalid tag when no
	 *  identity component is authored on the producible. */
	static FGameplayTag GetIdentityTagFromClass(TSubclassOf<class ASeinActor> ActorClass)
	{
		if (!ActorClass) return FGameplayTag();
		TArray<const USeinEntityComponent*> Bridges;
		AActor::GetActorClassDefaultComponents<USeinEntityComponent>(ActorClass, Bridges);
		for (const USeinEntityComponent* Bridge : Bridges)
		{
			if (!Bridge) continue;
			if (const FSeinIdentityComponent* Identity = Bridge->FindAuthoredData<FSeinIdentityComponent>())
			{
				return Identity->IdentityTag;
			}
		}
		return FGameplayTag();
	}

	static bool CostsEqual(const FSeinResourceCost& A, const FSeinResourceCost& B)
	{
		if (A.Amounts.Num() != B.Amounts.Num()) return false;
		for (const auto& Pair : A.Amounts)
		{
			const FFixedPoint* Other = B.Amounts.Find(Pair.Key);
			if (!Other || *Other != Pair.Value) return false;
		}
		return true;
	}

	/** Queue entries have no stable ID yet, so callbacks are insulated by a full
	 *  value snapshot. Identical entries are intentionally interchangeable. */
	static bool EntriesEqual(
		const FSeinProductionQueueEntry& A,
		const FSeinProductionQueueEntry& B)
	{
		return A.ActorClass == B.ActorClass
			&& A.TotalBuildTime == B.TotalBuildTime
			&& CostsEqual(A.AtEnqueueCost, B.AtEnqueueCost)
			&& CostsEqual(A.AtCompletionCost, B.AtCompletionCost)
			&& A.ResourcePayer == B.ResourcePayer
			&& A.bIsResearch == B.bIsResearch
			&& A.ResearchEffectClass == B.ResearchEffectClass
			&& A.RefundPolicy.bUseCustomRefund == B.RefundPolicy.bUseCustomRefund
			&& A.RefundPolicy.CustomRefundPercentage
				== B.RefundPolicy.CustomRefundPercentage;
	}

	static bool SnapshotReadyCompletion(
		USeinWorldSubsystem& World,
		FSeinEntityHandle Producer,
		FReadyCompletion& Out)
	{
		const FSeinEntity* Entity = World.GetEntity(Producer);
		const FSeinProductionComponent* Production =
			World.GetComponent<FSeinProductionComponent>(Producer);
		if (!Entity || !Entity->IsAlive()
			|| !Production || Production->Queue.IsEmpty()
			|| Production->CurrentBuildProgress < Production->Queue[0].TotalBuildTime)
		{
			return false;
		}

		Out.Entry = Production->Queue[0];
		Out.ProducerOwner = World.GetEntityOwner(Producer);
		Out.SpawnTransform = Entity->Transform * Production->SpawnPointOffset;
		Out.bRallyToEntity = Production->bRallyToEntity;
		Out.RallyTransform = Production->RallyTransform;
		Out.RallyEntity = Production->RallyEntity;
		Out.OriginalProgress = Production->CurrentBuildProgress;
		Out.bOriginalStalled = Production->bStalledAtCompletion;
		return true;
	}

	/** Detach the exact front entry before any spawn/effect callback can mutate
	 *  its storage. The atomic deduction is the sequential affordability gate. */
	static bool DeductAndDetach(
		USeinWorldSubsystem& World,
		FSeinEntityHandle Producer,
		const FReadyCompletion& Completion)
	{
		if (!USeinResourceBPFL::SeinDeduct(
			&World, Completion.Entry.ResourcePayer,
			Completion.Entry.AtCompletionCost))
		{
			return false;
		}

		FSeinProductionComponent* Production =
			World.GetComponentMutable<FSeinProductionComponent>(Producer);
		if (!Production || Production->Queue.IsEmpty()
			|| !EntriesEqual(Production->Queue[0], Completion.Entry))
		{
			USeinResourceBPFL::SeinRefund(
				&World, Completion.Entry.ResourcePayer,
				Completion.Entry.AtCompletionCost);
			return false;
		}

		Production->Queue.RemoveAt(0);
		Production->CurrentBuildProgress = FFixedPoint::Zero;
		Production->bStalledAtCompletion = false;
		return true;
	}

	static void RestoreFailedCompletion(
		USeinWorldSubsystem& World,
		FSeinEntityHandle Producer,
		const FReadyCompletion& Completion)
	{
		USeinResourceBPFL::SeinRefund(
			&World, Completion.Entry.ResourcePayer,
			Completion.Entry.AtCompletionCost);
		if (FSeinProductionComponent* Production =
			World.GetComponentMutable<FSeinProductionComponent>(Producer))
		{
			Production->Queue.Insert(Completion.Entry, 0);
			Production->CurrentBuildProgress = Completion.OriginalProgress;
			Production->bStalledAtCompletion = Completion.bOriginalStalled;
		}
	}

	/** Returns true only on the transition into stalled, so diagnostics and the
	 *  visual event are emitted exactly once for a malformed/unaffordable front. */
	static bool MarkStalled(
		USeinWorldSubsystem& World,
		FSeinEntityHandle Producer,
		const FSeinProductionQueueEntry& ExpectedFront)
	{
		bool bNewlyStalled = false;
		if (FSeinProductionComponent* Production =
			World.GetComponentMutable<FSeinProductionComponent>(Producer))
		{
			if (!Production->Queue.IsEmpty()
				&& EntriesEqual(Production->Queue[0], ExpectedFront)
				&& !Production->bStalledAtCompletion)
			{
				Production->bStalledAtCompletion = true;
				bNewlyStalled = true;
			}
		}

		if (bNewlyStalled)
		{
			const FGameplayTag IdentityTag =
				GetIdentityTagFromClass(ExpectedFront.ActorClass);
			World.EnqueueVisualEvent(
				FSeinVisualEvent::MakeProductionStalledEvent(Producer, IdentityTag));
		}
		return bNewlyStalled;
	}

	static void DispatchRally(
		USeinWorldSubsystem& World,
		FSeinEntityHandle Produced,
		const FReadyCompletion& Completion)
	{
		FFixedVector RallyLocation = Completion.RallyTransform.GetLocation();
		if (Completion.bRallyToEntity && Completion.RallyEntity.IsValid())
		{
			RallyLocation = FFixedVector::ZeroVector;
			if (const FSeinEntity* RallyEntity =
				World.GetEntity(Completion.RallyEntity))
			{
				RallyLocation = RallyEntity->Transform.GetLocation();
			}
		}

		if (RallyLocation == FFixedVector::ZeroVector
			|| RallyLocation == Completion.SpawnTransform.GetLocation())
		{
			return;
		}

		FGameplayTag MoveAbilityTag;
		if (const FSeinAbilityComponent* AbilityComponent =
			World.GetComponent<FSeinAbilityComponent>(Produced))
		{
			if (const USeinAbility* MoveAbility =
				AbilityComponent->FindMoveAbility(World))
			{
				MoveAbilityTag = MoveAbility->AbilityTag;
			}
		}
		if (!MoveAbilityTag.IsValid()) return;

		FSeinBrokerQueuedOrder Order;
		Order.Context.AddTag(MoveAbilityTag);
		Order.TargetLocation = RallyLocation;
		Order.bIsInternalPrefix = true;
		const TArray<FSeinEntityHandle> Member = {Produced};
		World.CreateBrokerForMembers(
			Member, Completion.ProducerOwner, Order);
	}

	static void EmitNextStartedEvent(
		USeinWorldSubsystem& World, FSeinEntityHandle Producer)
	{
		TSubclassOf<ASeinActor> NextClass;
		const FSeinEntity* ProducerEntity = World.GetEntity(Producer);
		if (ProducerEntity && ProducerEntity->IsAlive())
		{
			if (const FSeinProductionComponent* Production =
				World.GetComponent<FSeinProductionComponent>(Producer);
				Production && !Production->Queue.IsEmpty())
			{
				NextClass = Production->Queue[0].ActorClass;
			}
		}
		if (NextClass)
		{
			const FGameplayTag NextIdentityTag =
				GetIdentityTagFromClass(NextClass);
			World.EnqueueVisualEvent(FSeinVisualEvent::MakeProductionEvent(
				Producer, NextIdentityTag, /*bCompleted=*/false));
		}
	}

	static void EmitCompletionEvents(
		USeinWorldSubsystem& World,
		FSeinEntityHandle Producer,
		FGameplayTag ProducedIdentityTag)
	{
		World.EnqueueVisualEvent(FSeinVisualEvent::MakeProductionEvent(
			Producer, ProducedIdentityTag, /*bCompleted=*/true));
		EmitNextStartedEvent(World, Producer);
	}
}

/**
 * System: Production
 * Phase: AbilityExecution | Priority: 50
 *
 * Per tick for every entity with FSeinProductionComponent + non-empty Queue:
 *   1. Advance CurrentBuildProgress (unless already stalled — stall halts the timer).
 *   2. When progress >= TotalBuildTime, atomically deduct AtCompletionCost and
 *      detach the front entry before spawning/applying it. Failed unit spawns
 *      restore the exact entry and refund the completion deduction. A research
 *      replacement invalidated by an OnRemoved callback is irreversible: its
 *      detached entry/cost stay consumed and no success event is emitted.
 *   3. Unaffordable or malformed entries stall and fire ProductionStalled once.
 *   4. On successful completion: fire ProductionCompleted + (if research) TechResearched,
 *      dequeue, reset progress; fire ProductionStarted for the next entry if any.
 */
class FSeinProductionSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override
	{
		// Phase 1 is deliberately mutation-light: spawning can grow both the entity
		// pool and component storages, invalidating every reference held by this walk.
		TArray<FSeinEntityHandle> ReadyProducers;
		TArray<FSeinEntityHandle> ActiveProducers;
		const ISeinComponentStorage* ProductionStorage =
			World.GetComponentStorageRaw(FSeinProductionComponent::StaticStruct());
		if (ProductionStorage)
		{
			ReadyProducers.Reserve(ProductionStorage->GetComponentCount());
			ActiveProducers.Reserve(ProductionStorage->GetComponentCount());
			ProductionStorage->ForEachLiveComponent([&](
				FSeinEntityHandle Handle, const void* RawComponent)
			{
				if (!World.GetEntityPool().IsValid(Handle)) return;
				const FSeinProductionComponent* ProdComp =
					static_cast<const FSeinProductionComponent*>(RawComponent);
				if (ProdComp && ProdComp->Queue.Num() != 0)
				{
					ActiveProducers.Add(Handle);
				}
			});
			for (const FSeinEntityHandle Handle : ActiveProducers)
			{
				FSeinProductionComponent* ProdComp =
					World.GetComponentMutable<FSeinProductionComponent>(Handle);
				if (!ProdComp || ProdComp->Queue.Num() == 0) continue;

				// Advance progress unless we're already parked at 100% waiting on cap.
				if (!ProdComp->bStalledAtCompletion)
				{
					ProdComp->CurrentBuildProgress = ProdComp->CurrentBuildProgress + DeltaTime;
				}

				if (ProdComp->CurrentBuildProgress >= ProdComp->Queue[0].TotalBuildTime)
				{
					ReadyProducers.Add(Handle);
				}
			}
		}

		// Phase 2 processes the stable handle snapshot in ascending slot order.
		// Every producer/component/queue value is reacquired between operations.
		for (const FSeinEntityHandle Producer : ReadyProducers)
		{
			SeinProductionLocal::FReadyCompletion Completion;
			if (!SeinProductionLocal::SnapshotReadyCompletion(
				World, Producer, Completion))
			{
				continue;
			}

			FGameplayTag ProducedIdentityTag;
			if (Completion.Entry.bIsResearch)
			{
				UClass* EffectClass = Completion.Entry.ResearchEffectClass.Get();
				const USeinEffect* EffectDef = EffectClass
					? GetDefault<USeinEffect>(EffectClass)
					: nullptr;
				if (!EffectDef || EffectClass->HasAnyClassFlags(
					CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
				{
					if (SeinProductionLocal::MarkStalled(
						World, Producer, Completion.Entry))
					{
						UE_LOG(LogSeinSim, Error,
							TEXT("Production stalled on %s: research entry has no usable effect class."),
							*Producer.ToString());
					}
					continue;
				}
				ProducedIdentityTag = EffectDef->EffectTag;
			}
			else if (!Completion.Entry.ActorClass)
			{
				if (SeinProductionLocal::MarkStalled(
					World, Producer, Completion.Entry))
				{
					UE_LOG(LogSeinSim, Error,
						TEXT("Production stalled on %s: unit entry has no actor class."),
						*Producer.ToString());
				}
				continue;
			}

			if (!SeinProductionLocal::DeductAndDetach(
				World, Producer, Completion))
			{
				SeinProductionLocal::MarkStalled(
					World, Producer, Completion.Entry);
				continue;
			}

			if (Completion.Entry.bIsResearch)
			{
				const USeinWorldSubsystem::FEffectApplyResult ApplyResult =
					World.ApplyEffectTransactional(
					Producer, Completion.Entry.ResearchEffectClass, Producer);
				if (ApplyResult.Status
					== USeinWorldSubsystem::EEffectApplyStatus::RejectedNoMutation)
				{
					SeinProductionLocal::RestoreFailedCompletion(
						World, Producer, Completion);
					SeinProductionLocal::MarkStalled(
						World, Producer, Completion.Entry);
					continue;
				}
				if (ApplyResult.Status == USeinWorldSubsystem::EEffectApplyStatus::
					InvalidatedAfterReplacementRemoval)
				{
					// Replacement teardown already ran arbitrary callbacks. Retrying or
					// refunding would duplicate irreversible authored side effects, so
					// consume this detached completion without claiming research success.
					UE_LOG(LogSeinSim, Error,
						TEXT("Production research on %s was invalidated by a RemoveEffectsWithTag callback; cost and queue entry remain consumed."),
						*Producer.ToString());
					SeinProductionLocal::EmitNextStartedEvent(World, Producer);
					continue;
				}
				if (ProducedIdentityTag.IsValid())
				{
					World.EnqueueVisualEvent(
						FSeinVisualEvent::MakeTechResearchedEvent(
							Completion.ProducerOwner, ProducedIdentityTag));
				}
			}
			else
			{
				const FSeinEntityHandle Produced = World.SpawnEntity(
					Completion.Entry.ActorClass,
					Completion.SpawnTransform,
					Completion.ProducerOwner);
				if (!Produced.IsValid())
				{
					SeinProductionLocal::RestoreFailedCompletion(
						World, Producer, Completion);
					continue;
				}

				ProducedIdentityTag = SeinProductionLocal::GetIdentityTagFromClass(
					Completion.Entry.ActorClass);
				SeinProductionLocal::DispatchRally(
					World, Produced, Completion);
			}

			SeinProductionLocal::EmitCompletionEvents(
				World, Producer, ProducedIdentityTag);
		}
	}

	virtual FSeinSystemDescriptor DescribeSystem() const override
	{
		return FSeinSystemDescriptor::Stateless(
			FName(TEXT("seinarts.core.production")),
			1u,
			ESeinTickPhase::AbilityExecution,
			SeinSystemPriority::Production);
	}
};

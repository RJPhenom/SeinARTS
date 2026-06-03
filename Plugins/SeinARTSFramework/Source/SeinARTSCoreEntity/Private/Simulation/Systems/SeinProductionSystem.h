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
#include "Core/SeinPlayerState.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinProductionComponent.h"
#include "Components/SeinIdentityComponent.h"
#include "Actor/SeinEntityComponent.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Effects/SeinEffect.h"
#include "Events/SeinVisualEvent.h"
#include "Lib/SeinResourceBPFL.h"

namespace SeinProductionLocal
{
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
}
#include "Tags/SeinARTSGameplayTags.h"

/**
 * System: Production
 * Phase: AbilityExecution | Priority: 50
 *
 * Per tick for every entity with FSeinProductionComponent + non-empty Queue:
 *   1. Advance CurrentBuildProgress (unless already stalled — stall halts the timer).
 *   2. When progress >= TotalBuildTime, `AttemptSpawn`:
 *      a. If AtCompletionCost fits (catalog-aware CanAfford), deduct + spawn/apply.
 *      b. Else raise `bStalledAtCompletion`, fire ProductionStalled once, retry next tick.
 *   3. On successful completion: fire ProductionCompleted + (if research) TechResearched,
 *      dequeue, reset progress; fire ProductionStarted for the next entry if any.
 */
class FSeinProductionSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override
	{
		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			FSeinProductionComponent* ProdComp = World.GetComponent<FSeinProductionComponent>(Handle);
			if (!ProdComp || ProdComp->Queue.Num() == 0) return;

			const FSeinPlayerID OwnerID = World.GetEntityOwner(Handle);

			// Advance progress unless we're already parked at 100% waiting on cap.
			if (!ProdComp->bStalledAtCompletion)
			{
				ProdComp->CurrentBuildProgress = ProdComp->CurrentBuildProgress + DeltaTime;
			}

			const FSeinProductionQueueEntry& Front = ProdComp->Queue[0];
			if (ProdComp->CurrentBuildProgress < Front.TotalBuildTime)
			{
				return;
			}

			// At or past 100%. Gate on AtCompletion affordability.
			if (!Front.AtCompletionCost.IsEmpty() &&
				!USeinResourceBPFL::SeinCanAfford(&World, OwnerID, Front.AtCompletionCost))
			{
				if (!ProdComp->bStalledAtCompletion)
				{
					ProdComp->bStalledAtCompletion = true;
					const FGameplayTag IdentityTag = SeinProductionLocal::GetIdentityTagFromClass(Front.ActorClass);
					World.EnqueueVisualEvent(FSeinVisualEvent::MakeProductionStalledEvent(Handle, IdentityTag));
				}
				return;
			}

			// AtCompletion deduct — catalog-aware (pop/supply resources add toward cap).
			if (!Front.AtCompletionCost.IsEmpty())
			{
				USeinResourceBPFL::SeinDeduct(&World, OwnerID, Front.AtCompletionCost);
			}

			FGameplayTag ProducedIdentityTag;

			if (Front.bIsResearch)
			{
				// Unified tech path: apply the research effect class to the owner's
				// representative entity. The effect's scope routes modifiers + tags
				// to the right sim location (Instance / Archetype / Player).
				if (Front.ResearchEffectClass)
				{
					World.ApplyEffect(Handle, Front.ResearchEffectClass, Handle);
					if (const USeinEffect* EffectDef = GetDefault<USeinEffect>(Front.ResearchEffectClass))
					{
						ProducedIdentityTag = EffectDef->EffectTag;
						if (ProducedIdentityTag.IsValid())
						{
							World.EnqueueVisualEvent(
								FSeinVisualEvent::MakeTechResearchedEvent(OwnerID, ProducedIdentityTag));
						}
					}
				}
			}
			else if (Front.ActorClass)
			{
				// Spawn at the producer's authored SpawnPointOffset, composed
				// with the producer's world transform so the offset rotates
				// with the producer (barracks placed at any yaw spawns out of
				// the same door regardless of facing). The composed transform's
				// rotation also carries to the produced unit, so designers can
				// point the SpawnPointOffset forward and units arrive aimed at
				// the rally walkway before the rally Move dispatches. Default
				// identity = pivot (mesh-interior); designer is expected to
				// override.
				const FFixedTransform SpawnWorldTransform = Entity.Transform * ProdComp->SpawnPointOffset;
				const FFixedVector SpawnLocation = SpawnWorldTransform.GetLocation();

				const FSeinEntityHandle ProducedHandle =
					World.SpawnEntity(Front.ActorClass, SpawnWorldTransform, OwnerID);

				ProducedIdentityTag = SeinProductionLocal::GetIdentityTagFromClass(Front.ActorClass);

				// Stat attribution is designer-authored — wrap this entry point
				// (or hook the ProductionCompleted visual event from BP) if your
				// project tracks production counters.

				// Rally auto-move (DESIGN §9 Q9 + §5 broker integration): if the
				// production component carries a non-zero rally target, drop a
				// single-member broker order on the produced unit to navigate
				// there. Entity-targeted rallies resolve to the entity's current
				// transform at dispatch time.
				if (ProducedHandle.IsValid())
				{
					FFixedVector RallyLoc;
					if (ProdComp->bRallyToEntity && ProdComp->RallyEntity.IsValid())
					{
						if (const FSeinEntity* RallyEntityPtr = World.GetEntity(ProdComp->RallyEntity))
						{
							RallyLoc = RallyEntityPtr->Transform.GetLocation();
						}
					}
					else
					{
						RallyLoc = ProdComp->RallyTransform.GetLocation();
					}

					if (RallyLoc != FFixedVector::ZeroVector && RallyLoc != SpawnLocation)
					{
						// Resolve the produced unit's move-ability tag via the
						// bIsMoveAbility flag (designer-set on whichever
						// ability represents "move" — typically SA_Move).
						// Skip the auto-rally move if the produced unit has
						// no move ability flagged.
						const FSeinAbilityComponent* ProducedAbilityComp =
							World.GetComponent<FSeinAbilityComponent>(ProducedHandle);
						const USeinAbility* MoveAbility = ProducedAbilityComp
							? ProducedAbilityComp->FindMoveAbility(World)
							: nullptr;
						if (MoveAbility && MoveAbility->AbilityTag.IsValid())
						{
							FSeinBrokerQueuedOrder Order;
							Order.Context.AddTag(MoveAbility->AbilityTag);
							Order.TargetLocation = RallyLoc;
							Order.bIsInternalPrefix = true;
							TArray<FSeinEntityHandle> Member = { ProducedHandle };
							World.CreateBrokerForMembers(Member, OwnerID, Order);
						}
					}
				}
			}

			// Fire ProductionCompleted for UI. For research uses the effect tag; for
			// units uses the identity tag. Empty tag is allowed if neither resolved.
			World.EnqueueVisualEvent(
				FSeinVisualEvent::MakeProductionEvent(Handle, ProducedIdentityTag, /*bCompleted=*/true));

			// Dequeue + reset progress/stall.
			ProdComp->Queue.RemoveAt(0);
			ProdComp->CurrentBuildProgress = FFixedPoint::Zero;
			ProdComp->bStalledAtCompletion = false;

			// Fire ProductionStarted for the next entry, if any.
			if (ProdComp->Queue.Num() > 0)
			{
				const FSeinProductionQueueEntry& Next = ProdComp->Queue[0];
				const FGameplayTag NextIdentityTag = SeinProductionLocal::GetIdentityTagFromClass(Next.ActorClass);
				World.EnqueueVisualEvent(
					FSeinVisualEvent::MakeProductionEvent(Handle, NextIdentityTag, /*bCompleted=*/false));
			}
		});
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::AbilityExecution; }
	virtual int32 GetPriority() const override { return 50; }
	virtual FName GetSystemName() const override { return TEXT("Production"); }
};

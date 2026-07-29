/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinProducibleComponent.h
 * @brief:   Producibility metadata for an entity — "when something produces
 *           ME, this is how long I take, what prerequisites are required,
 *           and what happens on cancel/completion." Decomposed out of
 *           the pre-refactor archetype-definition class as part of the Phase-1 entity-component
 *           refactor.
 *
 *           Carried by producible entity classes (units, buildings, tech upgrades).
 *           Non-producible entities (scenario actors, terrain props, abstract
 *           sim entities) don't author this component.
 *
 *           The PRODUCTION COST is NOT here — it lives on the triggering
 *           ability's `ResourceCost` (designers wire e.g.
 *           `SA_TrainRifleman.ResourceCost = { Manpower: 50 }`). The
 *           ability's `EnqueueProduction(<this class>)` reads BuildTime
 *           + Refund + research metadata from this struct to build the queue
 *           entry.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SeinComponent.h"
#include "Components/SeinProductionComponent.h"
#include "Effects/SeinEffect.h"  // full type required for TSubclassOf<USeinEffect>.Get() in GetTypeHash
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "SeinProducibleComponent.generated.h"

USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinProducibleComponent : public FSeinComponent
{
	GENERATED_BODY()

	/** Time in sim-seconds to produce this entity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Producible")
	FFixedPoint BuildTime = FFixedPoint::FromInt(10);

	/** Tech tags the owning player must have unlocked to produce/research this.
	 *  Used by UI for greying production buttons; the actual gate at activation
	 *  is on the triggering ability's `RequiredPlayerTags` (designers typically
	 *  mirror the same set in both places). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Producible")
	FGameplayTagContainer PrerequisiteTags;

	/** Refund policy applied when this entry is cancelled mid-build. Default
	 *  progress-proportional refund of its deducted activation principal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Producible")
	FSeinProductionRefundPolicy RefundPolicy;

	/** If true, completing production applies `GrantedTechEffect` to the owning
	 *  player instead of spawning a unit. The ability's
	 *  `EnqueueProduction(<this>)` detects the flag and creates a research
	 *  queue entry. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Producible")
	bool bIsResearch = false;

	/** USeinEffect class applied on research completion. The effect's scope
	 *  (Instance / Class / Player) determines where modifiers land; its
	 *  `EffectTag` + `GrantedTags` become player tags (refcounted) per the
	 *  unified tech-is-an-effect rule. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Producible",
		meta = (EditCondition = "bIsResearch"))
	TSubclassOf<USeinEffect> GrantedTechEffect;
};

FORCEINLINE uint32 GetTypeHash(const FSeinProducibleComponent& Component)
{
	uint32 Hash = GetTypeHash(Component.BuildTime);
	// FGameplayTagContainer has no GetTypeHash overload — iterate tags
	// individually (mirrors FSeinEntityTagState's approach).
	for (const FGameplayTag& Tag : Component.PrerequisiteTags)
	{
		Hash = HashCombine(Hash, GetTypeHash(Tag));
	}
	Hash = HashCombine(Hash, GetTypeHash(Component.bIsResearch));
	Hash = HashCombine(Hash, GetTypeHash(Component.GrantedTechEffect.Get()));
	return Hash;
}

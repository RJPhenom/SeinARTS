#pragma once

#include "Abilities/SeinAbility.h"
#include "Actor/SeinActor.h"
#include "Effects/SeinEffect.h"
#include "SeinProductionCostTestTypes.generated.h"

/** Native producible whose entity-bridge payload is scoped by production-cost tests. */
UCLASS()
class ASeinProductionCostTestActor : public ASeinActor
{
	GENERATED_BODY()
};

/** Paid ability used to exercise activation funding and production ownership. */
UCLASS()
class USeinProductionCostTestAbility : public USeinAbility
{
	GENERATED_BODY()
};

/** Distinct move ability used by the AutoMoveThen funding test. */
UCLASS()
class USeinProductionCostTestMoveAbility : public USeinAbility
{
	GENERATED_BODY()
};

/** Persistent player-scope effect used by research completion tests. */
UCLASS()
class USeinProductionCostTestResearchEffect : public USeinEffect
{
	GENERATED_BODY()
};

#pragma once

#include "Abilities/SeinAbility.h"
#include "Actor/SeinActor.h"
#include "SeinPlacementYawTestTypes.generated.h"

/** Native test classes used to exercise the full broker placement gate. */
UCLASS()
class ASeinPlacementYawTestBuilding : public ASeinActor
{
	GENERATED_BODY()
};

UCLASS()
class USeinPlacementYawTestAbility : public USeinAbility
{
	GENERATED_BODY()
};

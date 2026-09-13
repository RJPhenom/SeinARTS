#pragma once

#include "Abilities/SeinAbility.h"
#include "SeinProductionPolicyTestTypes.generated.h"

/** Actual command-driven enqueue ability; deliberately has no optional UI preflight. */
UCLASS()
class USeinProductionPolicyTestAbility : public USeinAbility
{
	GENERATED_BODY()
public:
	USeinProductionPolicyTestAbility();
	virtual void OnActivate_Implementation() override;
};

#pragma once

#include "Abilities/SeinAbility.h"
#include "SeinCooldownSharingTestTypes.generated.h"

/** Immutable authored defaults keep snapshot continuation representative. */
UCLASS()
class USeinCooldownSharingTestAbility : public USeinAbility
{
	GENERATED_BODY()
public:
	USeinCooldownSharingTestAbility();
};

#pragma once

#include "Abilities/SeinAbility.h"
#include "SeinContainmentTestTypes.generated.h"

/** Test-only designer-style ability that enters its target container. */
UCLASS()
class USeinContainmentEnterTestAbility : public USeinAbility
{
	GENERATED_BODY()

public:
	USeinContainmentEnterTestAbility();

	virtual void OnActivate_Implementation() override;
};

/** Test-only designer-style ability that exits its current container. */
UCLASS()
class USeinContainmentExitTestAbility : public USeinAbility
{
	GENERATED_BODY()

public:
	USeinContainmentExitTestAbility();

	virtual void OnActivate_Implementation() override;
};

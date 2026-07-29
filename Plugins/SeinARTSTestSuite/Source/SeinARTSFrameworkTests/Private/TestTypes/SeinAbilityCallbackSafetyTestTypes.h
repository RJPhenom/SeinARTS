#pragma once

#include "Abilities/SeinAbility.h"
#include "SeinAbilityCallbackSafetyTestTypes.generated.h"

UCLASS()
class USeinCallbackImmediateEndAbility : public USeinAbility
{
	GENERATED_BODY()

public:
	virtual void OnActivate_Implementation() override;
};

UCLASS()
class USeinCallbackReplaceComponentAbility : public USeinAbility
{
	GENERATED_BODY()

public:
	virtual void OnActivate_Implementation() override;
};

UCLASS()
class USeinCallbackGrowComponentStorageAbility : public USeinAbility
{
	GENERATED_BODY()

public:
	virtual void OnActivate_Implementation() override;
};

UCLASS()
class USeinCallbackCancelReplacementAbility : public USeinAbility
{
	GENERATED_BODY()
};

UCLASS()
class USeinCallbackRevokeOnCancelAbility : public USeinAbility
{
	GENERATED_BODY()

public:
	virtual void OnEnd_Implementation(bool bWasCancelled) override;
};

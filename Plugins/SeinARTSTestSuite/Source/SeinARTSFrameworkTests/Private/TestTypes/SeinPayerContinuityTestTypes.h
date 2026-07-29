#pragma once

#include "Abilities/SeinAbility.h"
#include "Input/SeinCommandAuthorityPolicy.h"
#include "SeinPayerContinuityTestTypes.generated.h"

/** Test policy whose current command principal pays for entity actions. */
UCLASS(Const)
class USeinPayerContinuityAuthorityPolicy : public USeinCommandAuthorityPolicy
{
	GENERATED_BODY()

public:
	virtual bool AuthorizeCommand_Implementation(
		const USeinCommandAuthorityView* View,
		const FSeinCommand& Command,
		ESeinCommandAuthorityScope Scope,
		FGameplayTag& OutRejectionReason) const override;

	virtual bool CanControlEntity_Implementation(
		const USeinCommandAuthorityView* View,
		const FSeinCommand& Command,
		FSeinEntityHandle Entity) const override;

	virtual FSeinPlayerID ResolveResourcePayer_Implementation(
		const USeinCommandAuthorityView* View,
		const FSeinCommand& Command,
		FSeinEntityHandle Entity) const override;
};

UCLASS()
class USeinPayerContinuityAbility : public USeinAbility
{
	GENERATED_BODY()
};

UCLASS()
class USeinPayerContinuityMoveAbility : public USeinAbility
{
	GENERATED_BODY()
};

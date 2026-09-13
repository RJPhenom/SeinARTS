#pragma once
#include "Abilities/SeinAbility.h"
#include "SeinActivationInputTestTypes.generated.h"

UCLASS()
class USeinActivationInputTestAbility : public USeinAbility
{
	GENERATED_BODY()
public:
	USeinActivationInputTestAbility();
	UPROPERTY(BlueprintReadWrite, Category = "Test")
	int32 QueueIndex = 7;
	UPROPERTY(BlueprintReadWrite, Category = "Test")
	FFixedVector Offset;
	UPROPERTY(BlueprintReadWrite, Category = "Test")
	TArray<int32> Choices = {3, 5};
	UPROPERTY()
	int32 Total = 0;
	UPROPERTY()
	int32 Activations = 0;
	virtual bool CanActivateWithInputs_Implementation(const FSeinAbilityActivationInputs& Inputs) const override;
	virtual void OnActivate_Implementation() override;
};

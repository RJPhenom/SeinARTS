#pragma once

#include "Actor/SeinActor.h"
#include "SeinAbilityVisualEventTestTypes.generated.h"

UCLASS()
class ASeinAbilityVisualEventTestActor : public ASeinActor
{
	GENERATED_BODY()

public:
	TArray<FName> ReceivedEvents;
	TArray<FGameplayTag> ReceivedTags;
	virtual void ProcessEvent(UFunction* Function, void* Parameters) override;
};

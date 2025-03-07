

#pragma once

// Engine includes
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

// Generated includes
#include "SAFUnitMember.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SEINARTS_FRAMEWORK_API USAFUnitMember : public USceneComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	USAFUnitMember();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

		
};

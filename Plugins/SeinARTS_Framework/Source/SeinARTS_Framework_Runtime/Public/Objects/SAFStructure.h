

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SAFObject.h"
#include "SAFStructure.generated.h"

UCLASS()
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFStructure : public ASAFObject
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ASAFStructure();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	
};

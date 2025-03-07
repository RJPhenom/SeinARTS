

#pragma once

// Engine includes
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

// Framework includes

// Generated includes
#include "SAFObject.generated.h"

UCLASS()
class SEINARTS_FRAMEWORK_API ASAFObject : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ASAFObject();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;


};

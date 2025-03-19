

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SAFObject.generated.h"

UCLASS()
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFObject : public AActor
{
	GENERATED_BODY()
	
// =========================================================================================
//                                      PROPERTIES
// =========================================================================================
public:	


protected:



private:


// =========================================================================================
//                                      METHODS
// =========================================================================================
public:	

	ASAFObject();

	virtual void Tick(float DeltaTime) override;


protected:

	virtual void BeginPlay() override;


private:


};



#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SAFObject.h"
#include "SAFUnit.generated.h"

UCLASS()
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFUnit : public ASAFObject
{
	GENERATED_BODY()
	
public:	
	// PROPERTIES
	//------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS Properties")
	bool Selectable = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS Properties")
	bool Orderable = true;

	// Sets default values for this actor's properties
	ASAFUnit();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;


};

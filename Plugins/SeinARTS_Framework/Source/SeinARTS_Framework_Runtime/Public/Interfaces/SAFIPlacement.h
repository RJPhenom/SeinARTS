

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SAFIPlacement.generated.h"

// This class does not need to be modified.
UINTERFACE(Blueprintable)
class USAFIPlacement : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class SEINARTS_FRAMEWORK_RUNTIME_API ISAFIPlacement
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	bool GetPlaceable();
	virtual bool GetPlaceable_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void OnStartPlacing();
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void OnPlace();
};

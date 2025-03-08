

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SAFISelection.generated.h"

// This class does not need to be modified.
UINTERFACE(Blueprintable)
class USAFISelection : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class SEINARTS_FRAMEWORK_API ISAFISelection
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	bool GetSelectable();
	virtual bool GetSelectable_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void OnSelect(AController* Selector);
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void OnDeselect(AController* Selector);
};

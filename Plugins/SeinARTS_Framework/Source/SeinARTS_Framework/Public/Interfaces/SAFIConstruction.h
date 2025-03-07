

#pragma once

// Engine includes
#include "CoreMinimal.h"
#include "UObject/Interface.h"

// Generated includes
#include "SAFIConstruction.generated.h"

// This class does not need to be modified.
UINTERFACE(Blueprintable)
class USAFIConstruction : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class SEINARTS_FRAMEWORK_API ISAFIConstruction
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	bool GetConstructionEnabled();
	virtual bool GetConstructionEnabled_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void OnConstructionIssued();
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void OnConstructionStarted();
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void OnConstructionStopped();
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void OnConstructionResumed();
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void OnConstructionFinished();
};

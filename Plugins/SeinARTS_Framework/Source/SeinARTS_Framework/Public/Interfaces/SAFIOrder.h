

#pragma once

// Engine includes
#include "CoreMinimal.h"
#include "UObject/Interface.h"

// Generated includes
#include "SAFIOrder.generated.h"

// This class does not need to be modified.
UINTERFACE(Blueprintable)
class USAFIOrder : public UInterface
{
	GENERATED_BODY()
};

/**
 *
 */
class SEINARTS_FRAMEWORK_API ISAFIOrder
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	bool GetOrderable();
	virtual bool GetOrderable_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void ReceiveOrder(AController* Issuer, FVector PayloadPos, ASAFObject* PayloadTarg, bool Additive = false);
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void ConfirmOrderReceived(AController* Issuer);
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void ConfirmOrderCancelled(AController* Issuer);
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void ConfirmOrderCompleted(AController* Issuer);
};
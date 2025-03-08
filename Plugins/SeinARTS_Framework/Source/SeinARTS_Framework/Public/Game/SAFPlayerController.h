

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInput/Public/EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "SAFPlayerController.generated.h"

/**
 * 
 */
UCLASS()
class SEINARTS_FRAMEWORK_API ASAFPlayerController : public APlayerController
{
	GENERATED_BODY()

private:

	// ===============================
	//      ENHANCED INPUT SYSTEM
	// ===============================

	FEnhancedInputActionValueBinding* ShiftCommandBinding;
	FEnhancedInputActionValueBinding* AlternateBinding;
	FEnhancedInputActionValueBinding* ControlBinding;


	// ===============================
	//      SELECTION PROPERTIES
	// ===============================

	TArray<TWeakObjectPtr<ASAFObject>> Selection;
	TWeakObjectPtr<ASAFObject> Selected;


protected:

	virtual void BeginPlay() override;

public:

	ASAFPlayerController();
	virtual void SetupInputComponent() override;


	// ===============================
	//      ENHANCED INPUT SYSTEM
	// ===============================

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Input")
	UInputMappingContext* DefaultMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Input")
	UInputAction* SelectionAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Input")
	UInputAction* OrderAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Input")
	UInputAction* ShiftCommandAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Input")
	UInputAction* AlternateAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Input")
	UInputAction* ControlAction;


	// ===============================
	//      SELECTION FUNCTIONS
	// ===============================

	// Returns the selection as a blueprint accessible array.
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection")
	TArray<ASAFObject*> GetSelection();

	// Returns the currently selected object. Gets the active selected unit if multiple
	// are selected.
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection")
	ASAFObject* GetSelected();

	// Sets the selection to the provides blueprint-providable array. Returns true if the
	// operation was successful. Returns false if aborted (invalid object in input array).
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection")
	bool SetSelection(TArray<ASAFObject*> InSelection);

	// Select add the objects in the input array to selection, given:
	//		i)		The input array has one item, OR
	//		ii)		The input array items AND the current selection array items 
	//				share a common owner, and that owner is the player associated 
	//				with this controller.
	// Additionally takes in a bool to determine if selection is additive. If true, appends
	// the input array to the selection. Otherwise, selection is cleared, then input array
	// is appended.
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection")
	void Select(TArray<ASAFObject*> Targets, bool Additive = false);

	// Deselect removes the units in the input array from selection, if present.
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection")
	void Deselect(TArray<ASAFObject*> Targets);

	void StartSelector(const FInputActionValue& value);
	void EndSelector(const FInputActionValue& value);

	// ===============================
	//      ORDER FUNCTIONS
	// ===============================
 
	// Tells the server to issue an order to SAFObjects. Payloads are 
	// generic, units handle order response depending on payload contents.
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_IssueOrder(const TArray<ASAFObject*>& Recipients, FVector PayloadPos, ASAFObject* PayloadTarget, bool Additive = false);
	void Server_IssueOrder_Implementation(const TArray<ASAFObject*>& Targets, FVector Location, ASAFObject* OrderIssuer, bool Additive = false);
	bool Server_IssueOrder_Validate(const TArray<ASAFObject*>& Targets, FVector Location, ASAFObject* OrderIssuer, bool Additive = false);
};

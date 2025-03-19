

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
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFPlayerController : public APlayerController
{
	GENERATED_BODY()

// =========================================================================================
//                                      PROPERTIES
// =========================================================================================
public:

	// ===============================
	//      SELECTION PROPERTIES
	// ===============================

	// The array of selection units.
	UPROPERTY(BlueprintReadOnly, EditInstanceOnly, Category = "SeinARTS")
	TArray<AActor*> Selection;

	// The index of the highlighted selected unit in the array.
	UPROPERTY(BlueprintReadOnly, EditInstanceOnly, Category = "SeinARTS")
	int Selected;

	// If enabled, selector will subtract highlighted actors from the selection while
	// the Control action is being triggered when the Selection action is released.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SeinARTS")
	bool EnableCtrlSubtractiveSelect = false;

	// Enables certain on-screen debug messages related to class functions.
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "SeinARTS")
	bool EnableSelectorDebugMessages = true;


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

private:

	// ===============================
	//      ENHANCED INPUT SYSTEM
	// ===============================

	FEnhancedInputActionValueBinding* ShiftCommandBinding;
	FEnhancedInputActionValueBinding* AlternateBinding;
	FEnhancedInputActionValueBinding* ControlBinding;


// =========================================================================================
//                                      METHODS
// =========================================================================================
public:

	ASAFPlayerController();
	virtual void SetupInputComponent() override;


	// ===============================
	//      SELECTION FUNCTIONS
	// ===============================

	// Returns the currently selected object. Gets the active selected unit if multiple
	// are selected.
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection")
	AActor* GetSelected() { return (Selection.IsValidIndex(Selected)) ? Selection[Selected] : nullptr; };

	// Select attempts to add the actors in the input array to selection. It will only add
	// Actors who do not belong to the player if they are the sole actor being added, and
	// the addition mode is not additive.
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection")
	void Select(TArray<AActor*> Targets, bool Additive = false, bool Subtractive = false);

	// Deselect removes the units in the input array from selection, if present.
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection")
	void Deselect(TArray<AActor*> Targets);

	// Deselectall units in the selection, if any.
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Selection")
	void EmptySelection();

	void StartSelector(const FInputActionValue& value);
	void EndSelector(const FInputActionValue& value);

	// ===============================
	//      ORDER FUNCTIONS
	// ===============================
 
	// Tells the server to issue an order to SAFObjects. Payloads are 
	// generic, units handle order response depending on payload contents.
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_IssueOrder(const TArray<AActor*>& Recipients, FVector PayloadPos, AActor* PayloadTarget, bool Additive = false);
	void Server_IssueOrder_Implementation(const TArray<AActor*>& Targets, FVector Location, AActor* OrderIssuer, bool Additive = false);
	bool Server_IssueOrder_Validate(const TArray<AActor*>& Targets, FVector Location, AActor* OrderIssuer, bool Additive = false);


protected:

	virtual void BeginPlay() override;


private:


};

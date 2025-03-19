

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
class SEINARTS_FRAMEWORK_RUNTIME_API ISAFISelection
{
	GENERATED_BODY()

public:

	// Returns whether or not the actor with this interface is currently selectable. By default 
	// this is always false. Intended to be overwridden by designer allowing for dynamic states
	// of selectability (i.e. you could create a boolean 'Selectable' property in a Blueprint
	// and return the value of that property). 
	// 
	// This is useful for scripted events in campaign mode
	// or for handling states where a unit becomes unselectable (has entered a death-animation state
	// before destruction.
	// 
	// **NOTE:** For selection on your class(es) to work you **MUST** override this function to, 
	// at minmimum, return true.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	bool GetSelectable();

	// Triggers when the actor is highlighted, but not yet selected (i.e. when the mouse is hovering
	// over it, or when it is overlapped by the selector box while drag click selecting).
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void Highlight();

	// Triggered when the actor is selected by a SeinARTS player controller.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void OnSelect(AController* Selector);

	// Triggered when the actor is deselected by a SeinARTS player controller.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void OnDeselect(AController* Selector);
};

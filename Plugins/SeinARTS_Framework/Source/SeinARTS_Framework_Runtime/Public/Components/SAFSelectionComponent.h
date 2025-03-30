

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SAFSelectionComponent.generated.h"

class UWidgetComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBlueprintHighlight);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBlueprintOnSelect, AController*, Controller);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBlueprintOnDeselect, AController*, Controller);

UENUM(BlueprintType)
enum SAFEnumerator_SelectedGUIMode {
	Circle,
	Outline,
	Shadowed,
	Texture,
	Widget,
	Custom
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SEINARTS_FRAMEWORK_RUNTIME_API USAFSelectionComponent : public UActorComponent
{
	GENERATED_BODY()

// =========================================================================================
//                                      PROPERTIES
// =========================================================================================	
public:	

	UPROPERTY(BlueprintAssignable)
	FBlueprintHighlight OnHighlight;

	UPROPERTY(BlueprintAssignable)
	FBlueprintOnSelect OnSelect;

	UPROPERTY(BlueprintAssignable)
	FBlueprintOnDeselect OnDeselect;

	// Toggle the selectability of the parent actor through this property.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SeinARTS")
	bool Selectable = true;

	// Determines how the parent actor will display its selected status. Modes include:
	//		i)		Circle: draws a circle around the actor mesh(es).
	//		ii)		Outline: draws an outline around the actor mesh(es).
	//		iii)	Shadowed: draws a shadowed outline around the actor mesh(es).
	//		iv)		Texture: draws a texture at the base of the actor mesh(es).
	//		v)		Widget: displays a widget via an associated WidgetComponent's
	//		vi)		Custom: user defined behaviour (requires overrides).
	//
	// WARNING: The following display modes require SAFPostProcessVolume setup:
	//		- Outline
	//		- Shadowed
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SeinARTS")
	TEnumAsByte<SAFEnumerator_SelectedGUIMode> DisplayMode = Circle;

	// The associated WidgetComponent to toggle visibility when using 'Widget' display mode.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SeinARTS")
	UWidgetComponent* Widget;

	// The texture to display when using 'Texture' display mode.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SeinARTS")
	UTexture2D* Texture;

	// The colour or tint applied to the highlight display.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SeinARTS|Selector")
	FLinearColor HighlightColour = FLinearColor::Yellow;

	// The colour or tint applied to the selector display.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SeinARTS|Selector")
	FLinearColor SelectedColour = FLinearColor::Green;


private:


// =========================================================================================
//                                        METHODS
// =========================================================================================	
public:	

	USAFSelectionComponent();

	// Triggered when the actor is highlight by the Selector box or cursor.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SeinARTS|Selection")
	void Highlight();
	void Highlight_Implementation();

	// Triggers when the actor is selected. This gets called by the selecting controller.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SeinARTS|Selection")
	void Select(AController* Controller);
	void Select_Implementation(AController* Controller);

	// Triggers when the actor is deselected. This gets called by the selecting controller.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SeinARTS|Selection")
	void Deselect(AController* Controller);
	void Deselect_Implementation(AController* Controller);
		

protected:

	virtual void BeginPlay() override;


};

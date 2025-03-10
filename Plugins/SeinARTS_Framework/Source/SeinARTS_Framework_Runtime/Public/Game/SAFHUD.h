

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SAFHUD.generated.h"

class ASAFObject;

/**
 * 
 */
UCLASS()
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFHUD : public AHUD
{
	GENERATED_BODY()

private:
	
	const float TraceLength = 100000.0f;

	bool DrawSelector = false;
	bool DrawSelectorRect = false;
	float SelectorX, SelectorY, SelectorW, SelectorH;
	
public:

	virtual void DrawHUD() override;

	// Should units (or unit members) be fully enclosed by the selection rect to be included
	// in the selection result.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SAFHUD|Selector Properties")
	bool UnitsMustBeFullyenclosed = false;

	// Should banners enclosed by the selection rect have their units included (screen space
	// shape is treated as world space bounds, as if a unit or unit member, responds to fully
	// enclosed setting).
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SAFHUD|Selector Properties")
	bool IncludeBannersInDragSelect = false;

	// Colour of the rect fill (opacity should be set 0.5f or lower for best effect).
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SAFHUD|Selector Properties")
	FLinearColor SelectorFillColour = FLinearColor(.1f,.1f,.1f,.1f);

	// Colour of the rect border (opacity = 0.0f for no border).
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SAFHUD|Selector Properties")
	FLinearColor SelectorBorderColour = FLinearColor(.1f, .1f, .1f, 1.0f);

	void ReceiveSelectorStarted();
	TArray<ASAFObject*> ReceiveSelectorEnded();
	TArray<ASAFObject*> GetSAFUnitsInSelectorRect();
	
};



#pragma once

// Engine includes
#include "CoreMinimal.h"

// Framework includes
#include "GameFramework/HUD.h"
#include "SAFObject.h"
#include "SAFUnitMember.h"

// Generated includes
#include "SAFHUD.generated.h"

/**
 * 
 */
UCLASS()
class SEINARTS_FRAMEWORK_API ASAFHUD : public AHUD
{
	GENERATED_BODY()

private:
	
	const float TraceLength = 100000.0f;

	bool DrawSelector = false;
	bool DrawSelectorRect = false;
	float SelectorX, SelectorY, SelectorW, SelectorH;
	
public:

	virtual void DrawHUD() override;

	// Selector Properties
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SAFHUD|Selector Properties")
	bool UnitsMustBeFullyenclosed = false;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SAFHUD|Selector Properties")
	bool IncludeBannersInDragSelect = false;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SAFHUD|Selector Properties")
	FLinearColor SelectorFillColour = FLinearColor(.1f,.1f,.1f,.1f);
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SAFHUD|Selector Properties")
	FLinearColor SelectorBorderColour = FLinearColor(.1f, .1f, .1f, 1.0f);

	void ReceiveSelectorStarted();
	TArray<UObject*> ReceiveSelectorEnded();

	TArray<UObject*> GetSAFUnitsInSelectorRect();
	
};

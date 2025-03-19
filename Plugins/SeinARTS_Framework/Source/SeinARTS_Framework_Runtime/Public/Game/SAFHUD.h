

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SAFHUD.generated.h"

class AActor;

/**
 * 
 */
UCLASS()
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFHUD : public AHUD
{
	GENERATED_BODY()

// =========================================================================================
//                                      PROPERTIES
// =========================================================================================	
public:

	virtual void DrawHUD() override;

	// Enables certain on-screen debug messages related to class functions.
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "SeinARTS")
	bool EnableSelectorDebugMessages = true;

	// Colour of the rect fill (opacity should be set 0.5f or lower for best effect).
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SeinARTS|Selector")
	FLinearColor FillColour = FLinearColor(.1f, .1f, .1f, .1f);

	// Colour of the rect border (opacity = 0.0f for no border).
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SeinARTS|Selector")
	FLinearColor BorderColour = FLinearColor(.1f, .1f, .1f, 1.0f);

	// Wether to hide the cursor while drag-selecting. On by default.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SeinARTS|Selector")
	bool HideCursorWhileSelecting = true;

	// Filters the actors in the selector by the chosen class.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SeinARTS|Selector")
	TSubclassOf<AActor> FilterByActor = AActor::StaticClass();

	// Filters the actors in the selector by those which have components of the chosen class,
	// which themselves are within the selector.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SeinARTS|Selector")
	TSubclassOf<UActorComponent> FilterByComponent = UActorComponent::StaticClass();

	// Should banners enclosed by the selection rect have their units included.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SeinARTS|Selector")
	bool IncludeBanners = false;

	// Should non-colliding components be included when determining bounds overlap for 
	// selection.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SeinARTS|Selector")
	bool IncludingNonCollidingComponents = false;

	// Should units (or unit members) be fully enclosed by the selection rect to be included
	// in the selection result.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "SeinARTS|Selector")
	bool UnitsMustBeFullyEnclosed = false;


protected:


private:

	const float TraceLength = 1000000.0f;

	bool DrawSelector = false;
	bool DrawSelectorRect = false;
	float SelectorX, SelectorY, SelectorW, SelectorH;
	FVector4 SelectorRect = FVector4(0, 0, 0, 0);
	TArray<AActor*> SelectorResults;


// =========================================================================================
//                                        METHODS
// =========================================================================================
public:

	void ReceiveSelectorStarted();
	TArray<AActor*> ReceiveSelectorEnded();


protected:


private:


	AActor* GetSelectableUnderCursor() const;
	TArray<AActor*> GetSelectablesInSelectorRect() const;
	bool SelectorIntersectsBannerBounds(const UActorComponent* Component) const;
	bool SelectorIntersectsActorBounds(const UActorComponent* Component) const;
	bool RectsIntersect(const FVector4& Rect1, const FVector4& Rect2) const;


};

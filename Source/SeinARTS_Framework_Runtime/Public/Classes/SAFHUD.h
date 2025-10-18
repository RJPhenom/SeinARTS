#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SAFHUD.generated.h"

// Event Delegates
// ==========================================================================================================
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMaruqeeStarted, FVector2D, MarqueeStart);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMaruqeeEnded, FVector2D, MarqueeEnd, bool, bMarqueeWasDrawn);

/**
 * SAFHUD
 * 
 * Base HUD class for the SeinARTS Framework. Includes default drag-select 
 * marquee + single select helpers.
 * 
 * The way SeinARTS handles selection is that the HUD handles the draw of the marquee
 * and computes the results, dispatching them out via the above update delegates. The
 * controller is responsible for telling this class when it should start, update, and
 * end marquee drawing. You can see the default bindings in the .cpp file under the 
 * BeginPlay() defintion.
 * 
 * In order to trigger these, the player controller will need to be setup with input
 * triggers. The provided base SAFBPPlayerController in the plugin content does this 
 * for you. 
 * 
 * At minimum, you will need to have a player controller that:
 *    1) Binds input actions to call 
 *         -BeginMarquee (Start), 
 *         -UpdateMarquee (Triggered),
 *         -EndMarquee (Cancelled, Completed).
 *    2) Binds receivers (provided in the SAFPlayerInterface) to this class' 
 *       OnSingleSelectUpdate and OnMarqueeSelectUpdate dispatchers to handle
 *       the results.
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS HUD"))
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFHUD : public AHUD {

	GENERATED_BODY()

public:

	ASAFHUD();

	// Event Bindings
	// =================================================================================
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|HUD")
	FOnMaruqeeStarted OnMarqueeStarted;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|HUD")
	FOnMaruqeeEnded OnMarqueeEnded;

	// Public Marquee Properties
	// ========================================================================================
	/** Color used when drawing the selection rectangle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS")
	FLinearColor MarqueeColor = FLinearColor(0.f, 1.f, 1.f, 0.2f);

	/** Color used when drawing the selection rectangle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS")
	FLinearColor MarqueeBorderColor = FLinearColor(0.f, 1.f, 1.f, 1.f);

	/** Color used when drawing the selection rectangle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS")
	float MarqueeBorderThickness = 1.f;

	/** If the drag distance (|End-Start|) is below this, treat it as a single-select.
	 *
	 * Note: this is managed separately from the DragThreshold property on the
	 * SAFPlayerController, as that manages actions whereas this manages drawing only.
	 * However, it is a good idea to keep these values in sync as it is the most intuitive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS", meta=(ClampMin="1", ClampMax="100"))
	float MarqueeThreshold = 10.f;

	/** Class filter used by selection-rectangle queries (optional). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS")
	TSubclassOf<AActor> MarqueeClassFilter = nullptr;

	/** Whether to include non-colliding components when doing marquee selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS")
	bool bMarqueeIncludesNonCollidingComponents = false;

	/** Whether to require actors be fully enclosed when doing marquee selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS")
	bool bMarqueeRequiresActorsBeFullyInclosed = false;

	/** Begin a drag-marquee at screen position. */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Marquee")
	void BeginMarquee(const FVector2D InStart);
	virtual void BeginMarquee_Implementation(const FVector2D InStart);

	/** End the marquee; perform selection (marquee vs single). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|HUD")
	void EndMarquee(const FVector2D InEnd);
	virtual void EndMarquee_Implementation(const FVector2D InEnd);

	/** Draws points in the world where a unit will go if issued a move order, particularly 
	 * to show how SAFSquads will move into cover. Defaults to DrawDebugPoint but is intended 
	 * to be replaced with decals or other UI/UX by the designers. Note: this does the drawing,
	 * but when and what to draw is governed by the Player Controller. The PlayerController 
	 * calls this by default on tick while there is a valid selection. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|HUD")
	void DrawDestination(const FVector& Destination, bool bUsesCover);
	virtual void DrawDestination_Implementation(const FVector& Destination, bool bUsesCover);

	/** Runs DrawDestination for each element in Destinations. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|HUD")
	void DrawDestinations(const TArray<FVector>& Destinations, bool bUsesCover);
	virtual void DrawDestinations_Implementation(const TArray<FVector>& Destinations, bool bUsesCover);

protected:

	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

	// Marquee Management
	// ========================================================================================
	/** Tracks if we are updating the select action this frame. */
	UPROPERTY(BlueprintReadWrite, Category="SeinARTS")
	bool bSelecting;

	/** Whether to draw the marquee this frame. */
	UPROPERTY(BlueprintReadWrite, Category="SeinARTS")
	bool bDrawMarquee = false;

	/** Drag start (screen space). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="SeinARTS")
	FVector2D MarqueeStart = FVector2D::ZeroVector;

	/** Drag end / current cursor (screen space). */
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS")
	FVector2D MarqueeEnd = FVector2D::ZeroVector;

	/** Cached rect params (computed each update). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="SeinARTS")
	float MarqueeWidth = 0.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="SeinARTS")
	float MarqueeHeight = 0.f;

	/** Container for marquee results (to be passed and handled by a SAFPlayerController). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="SeinARTS")
	TArray<AActor*> MarqueeSelectionResults;
	
};

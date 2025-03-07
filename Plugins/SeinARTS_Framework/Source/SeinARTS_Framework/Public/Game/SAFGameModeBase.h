

#pragma once

// Engine includes
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

// Framework includes
#include "Framework/SAFCore.h"
#include "Framework/SAFAssetManager.h"

// Generated includes
#include "SAFGameModeBase.generated.h"

/**
 * 
 */
UCLASS()
class SEINARTS_FRAMEWORK_API ASAFGameModeBase : public AGameModeBase
{
	GENERATED_BODY()
	
public:

	ASAFGameModeBase();
	

	// ===============================
	//      MAP & LEVEL PROPERTIES
	// ===============================

	// Sets the type of map boundaries.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game Mode|Map & Level Properties")
	TEnumAsByte<SAFEnumerator_MapBoundsType> MapBoundsType;

	// If using rect bounds, set X and Y dimensions. Map center = 0, therefore if you input 10,000 
	// as your map width, your boundary points will be -5000 (left) and 5000 (right).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game Mode|Map & Level Properties")
	FVector2D RectBounds;

	// If using radial bounds, input = radius of your map, centered on point 0,0,0.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game Mode|Map & Level Properties")
	float RadialBounds;

	// Altitude bounds. Measured both above (+) and below (-) zero plane (negative bounds will be irrelevant
	// if a ground plane exists blocking coordinates beneath it).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game Mode|Map & Level Properties")
	float ZBounds;

	// SeinARTS Properties:
	// --------------------
	UPROPERTY(BlueprintReadOnly)
	ASAFAssetManager* SAFAssetManager;

	// ===============================
	//      MAP & LEVEL FUNCTIONS
	// ===============================

	// Checks a vector if its within map bounds.
	UFUNCTION(BlueprintCallable, Category = "Game Mode|Map & Level Functions")
	bool CheckVectorWithinMapBounds(FVector Vector);

	// Takes in a raw vector and returns a safe vector within the map bounds.
	UFUNCTION(BlueprintCallable, Category = "Game Mode|Map & Level Functions")
	FVector GetSafeVectorWithinMapBounds(FVector Vector);
};

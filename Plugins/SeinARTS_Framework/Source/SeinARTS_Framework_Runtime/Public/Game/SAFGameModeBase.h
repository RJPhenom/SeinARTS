

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "SAFGameModeBase.generated.h"

UENUM(BlueprintType)
enum SAFEnumerator_MapBoundsType {
	Rect,
	Radial
};

/**
 * 
 */
UCLASS()
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFGameModeBase : public AGameModeBase
{
	GENERATED_BODY()


// =========================================================================================
//                                      PROPERTIES
// =========================================================================================
public:

	ASAFGameModeBase();


	// ===============================
	//         MAP PROPERTIES
	// ===============================

	// Sets the type of map boundaries.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Map Properties")
	TEnumAsByte<SAFEnumerator_MapBoundsType> MapBoundsType = Rect;

	// If using rect bounds, set X and Y dimensions. Map center = 0, therefore if you input 10,000 
	// as your map width, your boundary points will be -5000 (left) and 5000 (right).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Map Properties")
	FVector2D RectBounds = FVector2D(500.0f, 500.0f);

	// If using radial bounds, input = radius of your map, centered on point 0,0,0.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Map Properties")
	float RadialBounds = 500.0f;

	// Altitude bounds. Measured both above (+) and below (-) zero plane (negative bounds will be irrelevant
	// if a ground plane exists blocking coordinates beneath it).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Map Properties")
	float ZBounds = 500.0f;


	// ===============================
	//			 NAVIGATION
	// ===============================

	// The Navigation Volume (the actor that contains the navmesh, but not the navmesh itself).
	UPROPERTY(BlueprintReadOnly)
	ANavMeshBoundsVolume* NavMeshVolume;


private:

	// ===============================
	//			 NAVIGATION
	// ===============================

	UNavigationSystemV1* NavSys;
	ARecastNavMesh* NavMesh;


// =========================================================================================
//                                      METHODS
// =========================================================================================
protected:

	virtual void BeginPlay() override;


public:

	// ===============================
	//         MAP FUNCTIONS
	// ===============================

	// Checks a vector if its within map bounds.
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Map Functions")
	bool CheckVectorWithinMapBounds(FVector Vector);

	// Takes in a raw vector and returns a safe vector within the map bounds.
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Map Functions")
	FVector GetSafeVectorWithinMapBounds(FVector Vector);


	// ===============================
	//			 NAVIGATION
	// ===============================

	// The Navigation Volume (the actor that contains the navmesh, but not the navmesh itself).
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Navigation")
	void ResizeNavVolume(FVector inSize);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Navigation")
	void SizeNavVolumeToMapBounds();
};

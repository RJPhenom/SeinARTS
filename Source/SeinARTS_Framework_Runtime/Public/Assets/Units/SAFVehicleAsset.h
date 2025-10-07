#pragma once

#include "CoreMinimal.h"
#include "Assets/Units/SAFUnitAsset.h"
#include "Engine/EngineTypes.h"
#include "SAFVehicleAsset.generated.h"

class USkeletalMesh;
class UAnimInstance;
class UMaterialInterface;

/**
 * SAFVehicleAsset
 * 
 * Data Asset definition: the data asset structure for Vehicle units in the SeinARTS Framework.
 * Inherits from USAFUnitAsset to include common unit properties.
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Vehicle Unit Data Asset"))
class SEINARTS_FRAMEWORK_RUNTIME_API USAFVehicleAsset : public USAFUnitAsset {

	GENERATED_BODY()

public:

	/** Skeletal mesh to use for the vehicle pawn. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Visual")
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

	/** AnimBP class (AnimInstance subclass). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Visual")
	TSoftClassPtr<UAnimInstance> AnimClass;
	
};

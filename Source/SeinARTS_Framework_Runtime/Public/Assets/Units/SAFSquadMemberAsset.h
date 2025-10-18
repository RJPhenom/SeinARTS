#pragma once
#include "CoreMinimal.h"
#include "Assets/SAFUnitAsset.h"
#include "Engine/EngineTypes.h"
#include "SAFSquadMemberAsset.generated.h"

class USkeletalMesh;
class UAnimInstance;
class UMaterialInterface;

/**
 * SAFSquadMemberAsset
 * 
 * Data Asset definition: the data asset structure for SquadMembers, individual Character Actors
 * which make up Squad type units in the SeinARTS Framework.
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS SquadMember Data Asset"))
class SEINARTS_FRAMEWORK_RUNTIME_API USAFSquadMemberAsset : public USAFUnitAsset {

	GENERATED_BODY()

public:

	/** Skeletal mesh to use on the member's Character Mesh. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Visual")
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

	/** Optional AnimBP class (AnimInstance subclass). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Visual")
	TSubclassOf<UAnimInstance> AnimClass;

	/** Designer-friendly per-character mesh offset (relative to the Character's capsule).
	 * Defaults match UE Mannequin/Manny: loc Z = -90, yaw = -90. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Visual")
	FTransform CharacterMeshOffset = FTransform(
		FRotator(0.f, -90.f, 0.f),
		FVector(0.f, 0.f, -90.f),
		FVector(1.f, 1.f, 1.f)
	);
	
};

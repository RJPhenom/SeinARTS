// Source/SeinARTS_Framework_Runtime/Public/Data/SAF_SquadData.h
#pragma once
#include "CoreMinimal.h"
#include "Data/SAFUnitData.h"
#include "Engine/DataAsset.h"
#include "SAFSquadData.generated.h"

class USAFSquadMemberData;

/**
 * SAFUnitData
 * 
 * Data Asset definition: the data asset structure for Squad units in the SeinARTS Framework.
 * Inherits from USAFUnitData to include common unit properties.
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Squad Unit Data Asset"))
class SEINARTS_FRAMEWORK_RUNTIME_API USAFSquadData : public USAFUnitData {

	GENERATED_BODY()

public:

	/** Ordered list of SquadMembers. Index 0 = leader. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Squad")
	TArray<TSoftObjectPtr<USAFSquadMemberData>> Members;

	/** Ordered list of SquadMember positions in the squad formation.
	 * (Note: if you want your squad to be able to change formations, you can
	 * need to write to the live instance Positions, not the read-only data). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Squad")
	TArray<FVector> Positions;

	/** Modifies how far back the next row will be when stacking multiple rows
	 * in cover behind a cover object. Calculate based on SquadMember bounds
	 * extent * modifiers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Squad")
	float CoverRowOffsetModifier = 1.f;

	/** Modifies how far back the next row will be when stacking multiple rows
	 * in cover behind a cover object. Calculate based on SquadMember bounds
	 * extent * modifiers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Squad")
	float LateralStaggerModifier = 1.f;

	/** Modifies how far apart the next SquadMember will be when in cover behind
	 * a cover object. Default is 3. Calculated using:
	 *
	 * SquadCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius() * modifier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Squad")
	float CoverSpacingModifier = 2.f;
	
};

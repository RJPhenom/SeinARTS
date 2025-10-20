#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Structs/SAFVectorSet.h"
#include "SAFOrder.generated.h"

class AActor;
class USAFAsset;

/**
 * SAFOrder
 * 
 * A basic data structure that defines what an 'Order' contains
 * in the SeinARTS Framework. Start and End positions are used 
 * to support directional drag-orders (like moving formations).
 */
USTRUCT(BlueprintType)
struct SEINARTS_FRAMEWORK_RUNTIME_API FSAFOrder {
	GENERATED_BODY()

	/** Unique identifier for this order instance. */
	UPROPERTY(BlueprintReadOnly) FGuid Id;

	/** The target actor of this order, if any. */
	UPROPERTY(BlueprintReadWrite) TObjectPtr<AActor> Target = nullptr;

	/** Additional target data asset associated with this order, if any. */
	UPROPERTY(BlueprintReadWrite) TSoftObjectPtr<USAFAsset> TargetData = nullptr;

	/** A vector set representing start and end locations of the order. */
	UPROPERTY(BlueprintReadWrite) FSAFVectorSet Vectors = FSAFVectorSet();

	/** An optional gameplay tag associated with this order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FGameplayTag Tag;

	FSAFOrder() {}
	FSAFOrder(AActor* InTarget, TSoftObjectPtr<USAFAsset> InTargetData, const FSAFVectorSet& InVectors, FGameplayTag Tag = FGameplayTag())
		: Id(FGuid::NewGuid()), Target(InTarget), TargetData(InTargetData), Vectors(InVectors), Tag(Tag) {}
};

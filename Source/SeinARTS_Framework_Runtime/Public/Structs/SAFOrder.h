#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
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
	UPROPERTY(BlueprintReadOnly) FGuid Id;
	UPROPERTY(BlueprintReadWrite) TObjectPtr<AActor> Target = nullptr;
	UPROPERTY(BlueprintReadWrite) TSoftObjectPtr<USAFAsset> TargetData = nullptr;
	UPROPERTY(BlueprintReadWrite) FVector Start = FVector::ZeroVector;
	UPROPERTY(BlueprintReadWrite) FVector End = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FGameplayTag Tag;

	FSAFOrder() {}
	FSAFOrder(AActor* InTarget, TSoftObjectPtr<USAFAsset> InTargetData, const FVector& InStart, const FVector& InEnd, FGameplayTag Tag = FGameplayTag())
		: Id(FGuid::NewGuid()), Target(InTarget), TargetData(InTargetData), Start(InStart), End(InEnd), Tag(Tag) {}
};

#pragma once

#include "CoreMinimal.h"
#include "SAFVectorSet.generated.h"

/**
 * SAFVectorSet
 * 
 * A simple struct to contain start and end vectors in both screen and world space
 * for the three main input modes that are contiguous with HUD draw call overrides.
 */
USTRUCT(BlueprintType)

struct FSAFVectorSet {
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Vectors", meta=(DisplayName="World Start")) FVector Start = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Vectors", meta=(DisplayName="World End")) FVector End = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Vectors", meta=(DisplayName="Screen Space Start")) FVector2D Start2D = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Vectors", meta=(DisplayName="Screen Space End")) FVector2D End2D = FVector2D::ZeroVector;
};
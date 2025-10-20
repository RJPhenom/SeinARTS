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

	/** World space start vector */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Vectors", meta=(DisplayName="World Start")) FVector Start = FVector::ZeroVector;

	/** World space end vector */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Vectors", meta=(DisplayName="World End")) FVector End = FVector::ZeroVector;

	/** Screen space start vector */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Vectors", meta=(DisplayName="Screen Space Start")) FVector2D Start2D = FVector2D::ZeroVector;

	/** Screen space end vector */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Vectors", meta=(DisplayName="Screen Space End")) FVector2D End2D = FVector2D::ZeroVector;

	/** Returns a string representation of the vector set. */
	FORCEINLINE FString ToString() const {
		return FString::Printf(
			TEXT("Start: %s (2D: %s), End: %s (2D: %s)"),
			*Start.ToString(),
			*Start2D.ToString(),
			*End.ToString(),
			*End2D.ToString()
		);
	}
};
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Enums/SAFCoverTypes.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "SAFMathLibrary.generated.h"

class UNavigationSystemV1;
class UPrimitiveComponent;
class UWorld;
class AActor;
class APlayerState;

/**
 * SAFMathLibrary
 *
 * Math/geometry/nav helpers useful for interpolation and
 * NavMesh edging.
 */
namespace SAFMathLibrary {

// Interpolation
// ==========================================================================================================================================
float SmoothClamp(float A, float B, float Min, float Max);
float ReverseSmoothClamp(float A, float B, float Min, float Max);
float SmoothStepForwardVelocity(float DeltaTime, float CurrSignedSpeed, float TargetSignedSpeed, float InAcceleration, float InDeceleration);
    
// Geometry / Nav
// =====================================================================================================================
FVector VectorIntersect(const FVector& A1, const FVector& A2, const FVector& B1, const FVector& B2);
FVector EdgeOutwardNormal2D(const FVector& EdgeStart, const FVector& EdgeEnd, const FVector& ShapeCenter);
float ComputeActorStandoff(const AActor* Actor, float Min = 20.f, float Multiplier = 0.6f, float FallbackRadius = 50.f);
bool CheckPointOverlapsPositions(const FVector& Point, float Radius, const TArray<FVector>& Positions);

}


/**
 * USAFMathLibrary (BPFL)
 *
 * Blueprint exposure for SAFMathLibrary helpers.
 */
UCLASS()
class USAFMathLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()

public:

    // Interpolation
    // ==========================================================================================================================================
    /** Lerp(A,B,0.5) then Clamp(Min,Max). Mirrors the BP graph you shared. */
    UFUNCTION(BlueprintPure, Category="SeinARTS|Math", meta=(CompactNodeTitle="SmoothClamp"))
    static float SmoothClamp(float A, float B, float Min, float Max) 
	{ return SAFMathLibrary::SmoothClamp(A, B, Min, Max); }

    /** Placeholder for reverse behavior (define how you want it to differ). */
    UFUNCTION(BlueprintPure, Category="SeinARTS|Math", meta=(CompactNodeTitle="Reverse SmoothClamp"))
    static float ReverseSmoothClamp(float A, float B, float Min, float Max) 
	{ return SAFMathLibrary::ReverseSmoothClamp(A, B, Min, Max); }

    /** Smooths forward velocity changes; dampens instant direction flips when reversing. */
    UFUNCTION(BlueprintPure, Category="SeinARTS|Math")
    static float SmoothStepForwardVelocity(float DeltaTime, float CurrSignedSpeed, float TargetSignedSpeed, float InAcceleration, float InDeceleration) 
	{ return SAFMathLibrary::SmoothStepForwardVelocity(DeltaTime, CurrSignedSpeed, TargetSignedSpeed, InAcceleration, InDeceleration); }

    // Geometry / Nav
    // ===================================================================================================================
    /** Returns the 2D intersection of segments A1→A2 and B1→B2 (Z=0). */
    UFUNCTION(BlueprintPure, Category="SeinARTS|Math", meta=(CompactNodeTitle="Vector Intersect"))
    static FVector VectorIntersect(const FVector& A1, const FVector& A2, const FVector& B1, const FVector& B2) 
	{ return SAFMathLibrary::VectorIntersect(A1, A2, B1, B2); }

    /** Returns the outward planar normal (X,Y,0) for an edge, guaranteed to point away from the shape center. */
    UFUNCTION(BlueprintPure, Category="SeinARTS|Math")
    static FVector EdgeOutwardNormal2D(const FVector& EdgeStart, const FVector& EdgeEnd, const FVector& ShapeCenter) 
	{ return SAFMathLibrary::EdgeOutwardNormal2D(EdgeStart, EdgeEnd, ShapeCenter); }

    /** Returns the actor size-aware standoff distance for positioning against an edge; uses capsule radius when possible. */
    UFUNCTION(BlueprintPure, Category="SeinARTS|Math")
    static float ComputeUnitStandoff(const AActor* Actor, float Min = 20.f, float Multiplier = 0.6f, float FallbackRadius = 50.f) 
	{ return SAFMathLibrary::ComputeActorStandoff(Actor, Min, Multiplier, FallbackRadius); }

    /** Does a simple check if the point vector overlaps any vector in the array by a distance of Radius. */
    UFUNCTION(BlueprintPure, Category="SeinARTS|Math")
    static float CheckPointOverlapsPositions(const FVector& Point, float Radius, const TArray<FVector>& Positions) 
	{ return SAFMathLibrary::CheckPointOverlapsPositions(Point, Radius, Positions); }
  
};
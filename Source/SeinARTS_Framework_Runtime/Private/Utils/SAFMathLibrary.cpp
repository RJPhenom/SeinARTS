#include "Utils/SAFMathLibrary.h"
#include "Components/CapsuleComponent.h" 

namespace SAFMathLibrary {

	// Interpolation
	// ================================================================================================================================================
  /** Lerps floats with clamping */
	float SmoothClamp(float A, float B, float Min, float Max) {
		const float Mid = FMath::Lerp(A, B, 0.5f);
		return FMath::Clamp(Mid, Min, Max);
	}

  /** Reverse the min and max inputs on a SmoothClamp */
	float ReverseSmoothClamp(float A, float B, float Min, float Max) {
		return SmoothClamp(A, B, Max, Min);
	}

	/** Smooths forward velocity changes, preventing instant direction changes when issuing a reverse move during an active forward move. */
	float SmoothStepForwardVelocity(float DeltaTime, float CurrSignedSpeed, float TargetSignedSpeed, float InAcceleration, float InDeceleration) {
		const float MaxA = InAcceleration * DeltaTime;
		const float MaxD = InDeceleration * DeltaTime;

		// If signs are opposite, decel to zero first
		if (FMath::Sign(CurrSignedSpeed) != FMath::Sign(TargetSignedSpeed) && FMath::Abs(CurrSignedSpeed) > KINDA_SMALL_NUMBER) {
			const float step = FMath::Clamp(-CurrSignedSpeed, -MaxD, MaxD);
			return CurrSignedSpeed + step; // still not switching sign this frame
		}

		// Else accel normally
		const float diff  = TargetSignedSpeed - CurrSignedSpeed;
		const float limit = (FMath::Abs(TargetSignedSpeed) > FMath::Abs(CurrSignedSpeed)) ? MaxA : MaxD;
		return CurrSignedSpeed + FMath::Clamp(diff, -limit, limit);
	}

	// Geometry / Nav
	// ================================================================================================================================================
  /** Returns an FVector that lies at the intersection point of A1->A2 and B1->B2 in 2D. 
   * Z coordinates are not considered and return vector will have Z value of 0. */
  FVector VectorIntersect(const FVector& A1, const FVector& A2, const FVector& B1, const FVector& B2) {
    const FVector2D A12D(A1.X, A1.Y);
    const FVector2D A22D(A2.X, A2.Y);
    const FVector2D B12D(B1.X, B1.Y);
    const FVector2D B22D(B2.X, B2.Y);
    const FVector2D A = A22D - A12D;
    const FVector2D B = B22D - B12D;
  
    const float Denominator = FVector2D::CrossProduct(A, B);
    if (FMath::IsNearlyZero(Denominator)) { return FVector::ZeroVector; } // If parallel or coincident fallback to zero vector
    const float CrossQuotient = FVector2D::CrossProduct(B12D - A12D, B) / Denominator;
    const FVector2D Intersect = A12D + CrossQuotient * A;
    return FVector(Intersect.X, Intersect.Y, 0);
  }

  /** Returns the outward planar normal (X, Y, 0) for an edge, guaranteed to point away from the given shape center. */
  FVector EdgeOutwardNormal2D(const FVector& EdgeStart, const FVector& EdgeEnd, const FVector& ShapeCenter) {
    // Get 2D edge and perpendicular
    const FVector EdgeDirection = (EdgeEnd - EdgeStart).GetSafeNormal2D(); 
    if (EdgeDirection.IsNearlyZero()) return FVector::ZeroVector;
    FVector Perpendicular(-EdgeDirection.Y, EdgeDirection.X, 0.f);

    // Ensure we're pushing outwards
    const FVector Mid  = (EdgeStart + EdgeEnd) * 0.5f;
    const float Sign = FVector::DotProduct(Perpendicular, (Mid - ShapeCenter));
    if (Sign < 0.f) Perpendicular *= -1.f;

    return Perpendicular.GetSafeNormal();
  }

  /** Returns the actor extents/size-aware standoff distance for when positioning against an edge (usually cover); 
   * uses capsule radius when possible, fallsback to actor bounds sphere radius and finally 50.f default radius. */
  float ComputeActorStandoff(const AActor* Actor, float Min, float Multiplier, float FallbackRadius) {
    float Radius = FallbackRadius;

    // First, try to find any capsule component on this actor,
    // fallback to root component bounds if no capsule found
    if (Actor) {
      if (const UCapsuleComponent* Capsule = Actor->FindComponentByClass<UCapsuleComponent>()) Radius = Capsule->GetScaledCapsuleRadius();
      else if (Actor->GetRootComponent()) Radius = Actor->GetRootComponent()->Bounds.SphereRadius * 0.5f;
    }

    return FMath::Max(Min, Radius * Multiplier);
  }

  bool CheckPointOverlapsPositions(const FVector& Point, float Radius, const TArray<FVector>& Positions) {
    if (Positions.Num() == 0) return false;
    
    const float R2 = Radius * Radius;
    for (const FVector& P : Positions) 
      if (FVector::DistSquared2D(Point, P) <= R2) return true;

    return false;
  }

}
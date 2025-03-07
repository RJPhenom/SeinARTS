


#include "Game/SAFGameModeBase.h"

ASAFGameModeBase::ASAFGameModeBase() {
	
}

bool ASAFGameModeBase::CheckVectorWithinMapBounds(FVector Vector) {
    switch (MapBoundsType) {

    case SAFEnumerator_MapBoundsType::Rect:
        return Vector.X <= RectBounds.X && Vector.Y <= RectBounds.Y && Vector.Z <= ZBounds;

    case SAFEnumerator_MapBoundsType::Radial:
        return Vector.X <= RadialBounds && Vector.Y <= RadialBounds && Vector.Z <= ZBounds;

    default:
        UE_LOG(LogTemp, Warning, TEXT("Unknown map boundaries type. Invalidating Vector."));
        return false;
    }
}

FVector ASAFGameModeBase::GetSafeVectorWithinMapBounds(FVector Vector) {
    if (CheckVectorWithinMapBounds(Vector)) return Vector;

    FVector SafeVector = FVector::ZeroVector;
    switch (MapBoundsType) {

    case SAFEnumerator_MapBoundsType::Rect:
        SafeVector.X = (FMath::Abs(Vector.X) > RectBounds.X) ? RectBounds.X * (Vector.X < 0 ? -1 : 1) : Vector.X;
        SafeVector.Y = (FMath::Abs(Vector.Y) > RectBounds.Y) ? RectBounds.Y * (Vector.Y < 0 ? -1 : 1) : Vector.Y;
        SafeVector.Z = (FMath::Abs(Vector.Z) > ZBounds) ? ZBounds * (Vector.Z < 0 ? -1 : 1) : Vector.Z;
        break;

    case SAFEnumerator_MapBoundsType::Radial:
        SafeVector.X = (FMath::Abs(Vector.X) > RadialBounds) ? RadialBounds * (Vector.X < 0 ? -1 : 1) : Vector.X;
        SafeVector.Y = (FMath::Abs(Vector.Y) > RadialBounds) ? RadialBounds * (Vector.Y < 0 ? -1 : 1) : Vector.Y;
        SafeVector.Z = (FMath::Abs(Vector.Z) > ZBounds) ? ZBounds * (Vector.Z < 0 ? -1 : 1) : Vector.Z;
        break;

    default:
        UE_LOG(LogTemp, Warning, TEXT("Unknown map boundaries type. Zeroing Vector."));
        return FVector::ZeroVector;
    }

    return SafeVector;
}
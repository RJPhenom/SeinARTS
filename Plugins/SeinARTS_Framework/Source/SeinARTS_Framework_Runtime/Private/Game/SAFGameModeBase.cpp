


#include "Game/SAFGameModeBase.h"
#include "SAFPlayerController.h"
#include "SAFHUD.h"
#include "SAFCameraPawn.h"
#include "SAFPlayerState.h"

ASAFGameModeBase::ASAFGameModeBase() {
    DefaultPawnClass = ASAFCameraPawn::StaticClass();
    HUDClass = ASAFHUD::StaticClass();
    PlayerControllerClass = ASAFPlayerController::StaticClass();
}

void ASAFGameModeBase::BeginPlay() {
    Super::BeginPlay();
}


// ===============================
//      MAP FUNCTIONS
// ===============================

bool ASAFGameModeBase::CheckVectorWithinMapBounds(FVector Vector) {
    switch (MapBoundsType) {

    case SAFEnumerator_MapBoundsType::Rect:
        return FMath::Abs(Vector.X) <= RectBounds.X && FMath::Abs(Vector.Y) <= RectBounds.Y && FMath::Abs(Vector.Z) <= ZBounds;

    case SAFEnumerator_MapBoundsType::Radial:
        return FMath::Abs(FVector2D::Distance(FVector2D::ZeroVector, FVector2D(Vector.X, Vector.Y))) <= RadialBounds && FMath::Abs(Vector.Z) <= ZBounds;

    default:
        UE_LOG(LogTemp, Error, TEXT("Unknown map boundaries type. Invalidating Vector."));
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
        UE_LOG(LogTemp, Error, TEXT("Unknown map boundaries type. Zeroing Vector."));
        return FVector::ZeroVector;
    }

    return SafeVector;
}
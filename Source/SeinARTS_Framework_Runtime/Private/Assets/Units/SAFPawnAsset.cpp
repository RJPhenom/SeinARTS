#include "Assets/Units/SAFPawnAsset.h"
#include "Components/SAFMovementComponent.h"

USAFPawnAsset::USAFPawnAsset(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
	MovementComponentClass = USAFMovementComponent::StaticClass();
}

void USAFPawnAsset::PostLoad() {
	Super::PostLoad();
	if (!MovementComponentClass) MovementComponentClass = USAFMovementComponent::StaticClass(); 
}
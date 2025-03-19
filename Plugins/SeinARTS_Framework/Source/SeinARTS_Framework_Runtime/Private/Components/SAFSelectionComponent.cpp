


#include "SAFSelectionComponent.h"


USAFSelectionComponent::USAFSelectionComponent() {
	PrimaryComponentTick.bCanEverTick = false;
}


void USAFSelectionComponent::BeginPlay() {
	Super::BeginPlay();	
}

void USAFSelectionComponent::OnHighlight_Implementation() {

}

void USAFSelectionComponent::OnSelect_Implementation(AController* Controller) {

}

void USAFSelectionComponent::OnDeselect_Implementation(AController* Controller) {

}


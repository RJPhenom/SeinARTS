


#include "SAFSelectionComponent.h"


USAFSelectionComponent::USAFSelectionComponent() {
	PrimaryComponentTick.bCanEverTick = false;
}


void USAFSelectionComponent::BeginPlay() {
	Super::BeginPlay();	
}

void USAFSelectionComponent::Highlight_Implementation() {
	OnHighlight.Broadcast();
}

void USAFSelectionComponent::Select_Implementation(AController* Controller) {
	OnSelect.Broadcast(Controller);
}

void USAFSelectionComponent::Deselect_Implementation(AController* Controller) {
	OnDeselect.Broadcast(Controller);
}


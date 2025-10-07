#include "Assets/Units/SAFUnitAsset.h"
#include "Gameplay/Attributes/SAFUnitAttributes.h"
#include "Gameplay/Attributes/SAFProductionAttributes.h"

USAFUnitAsset::USAFUnitAsset(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
	AttributeSets.AddUnique(USAFUnitAttributes::StaticClass());
	AttributeSets.AddUnique(USAFProductionAttributes::StaticClass());
}

void USAFUnitAsset::PostLoad() {
	Super::PostLoad();
	AttributeSets.AddUnique(USAFUnitAttributes::StaticClass());
	AttributeSets.AddUnique(USAFProductionAttributes::StaticClass());
}

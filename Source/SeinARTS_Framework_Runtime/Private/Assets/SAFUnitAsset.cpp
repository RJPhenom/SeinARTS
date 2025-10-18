#include "Assets/SAFUnitAsset.h"
#include "Classes/SAFUnit.h"
#include "Classes/SAFPawn.h"
#include "Gameplay/Attributes/SAFUnitAttributes.h"
#include "Gameplay/Attributes/SAFProductionAttributes.h"

USAFUnitAsset::USAFUnitAsset(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
	AttributeSets.AddUnique(USAFUnitAttributes::StaticClass());
	AttributeSets.AddUnique(USAFProductionAttributes::StaticClass());
	InstanceClass = ASAFUnit::StaticClass();
	PawnInstanceClass = ASAFPawn::StaticClass();
}

void USAFUnitAsset::PostLoad() {
	Super::PostLoad();
	AttributeSets.AddUnique(USAFUnitAttributes::StaticClass());
	AttributeSets.AddUnique(USAFProductionAttributes::StaticClass());
	if (!InstanceClass) InstanceClass = ASAFUnit::StaticClass();
	if (!PawnInstanceClass) PawnInstanceClass = ASAFPawn::StaticClass();
}

#pragma once

#include "Components/ActorComponent.h"

#include "SeinLevelVolumeDebugComponentTestTypes.generated.h"

UCLASS()
class USeinLevelVolumeDebugTestComponent : public UActorComponent
{
	GENERATED_BODY()
};

UCLASS()
class USeinLevelVolumeDerivedDebugTestComponent
	: public USeinLevelVolumeDebugTestComponent
{
	GENERATED_BODY()
};

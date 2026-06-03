#include "Abilities/Actions/SeinWaitAction.h"

void USeinWaitAction::Initialize(FFixedPoint InDuration)
{
	Duration = InDuration;
	Elapsed = FFixedPoint::Zero;
}

bool USeinWaitAction::TickAction(FFixedPoint DeltaTime, USeinWorldSubsystem& World)
{
	Elapsed = Elapsed + DeltaTime;
	return Elapsed >= Duration;
}

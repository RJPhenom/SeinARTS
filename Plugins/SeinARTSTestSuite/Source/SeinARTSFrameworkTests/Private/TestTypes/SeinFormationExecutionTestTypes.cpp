#include "TestTypes/SeinFormationExecutionTestTypes.h"

FSeinFormationLayout USeinMutatingFormationTest::
	BuildFormation_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target)
{
	++InvocationCount;
	return Super::BuildFormation_Implementation(
		World, Members, Target);
}

#include "TestTypes/SeinAbilityContinuationValidationTestTypes.h"

USeinAbilityContinuationValidationAsyncProxy*
USeinAbilityContinuationValidationAsyncProxy::StartValidationAsync()
{
	return NewObject<
		USeinAbilityContinuationValidationAsyncProxy>();
}

USeinAbilityContinuationValidationUnregisteredProxy*
USeinAbilityContinuationValidationUnregisteredProxy::
	StartUnregisteredValidationAsync()
{
	return NewObject<
		USeinAbilityContinuationValidationUnregisteredProxy>();
}

USeinAbilityContinuationValidationHeterogeneousAsyncProxy*
USeinAbilityContinuationValidationHeterogeneousAsyncProxy::
	StartHeterogeneousValidationAsync()
{
	return NewObject<
		USeinAbilityContinuationValidationHeterogeneousAsyncProxy>();
}

int32 USeinAbilityContinuationValidationTestAbility::
	ReadUnsafeTransientValue() const
{
	return UnsafeTransientValue;
}

int32 USeinAbilityContinuationValidationTestAbility::
	ExtractWaypointIndex(FSeinMoveToResult Result) const
{
	return Result.WaypointIndex;
}

void USeinAbilityContinuationValidationTestAbility::
	RecordUnsafeTransientValue()
{
	NativeObservedValue = UnsafeTransientValue;
}

void USeinAbilityContinuationValidationTestAbility::
	ConsumeResultInSafeMarkedLatent(
		FSeinMoveToResult Result,
		FLatentActionInfo LatentInfo)
{
	(void)Result;
	(void)LatentInfo;
}

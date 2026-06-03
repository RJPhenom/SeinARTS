/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinProductionComponent.cpp
 * @brief   FSeinProductionComponent sim-payload implementation — queue state
 *          queries and build-progress accessors.
 */

#include "Components/SeinProductionComponent.h"

bool FSeinProductionComponent::IsProducing() const
{
	return Queue.Num() > 0;
}

FFixedPoint FSeinProductionComponent::GetProgressPercent() const
{
	if (!IsProducing())
	{
		return FFixedPoint::Zero;
	}

	const FSeinProductionQueueEntry& Current = Queue[0];
	if (Current.TotalBuildTime <= FFixedPoint::Zero)
	{
		return FFixedPoint::One;
	}

	return CurrentBuildProgress / Current.TotalBuildTime;
}

bool FSeinProductionComponent::CanQueueMore() const
{
	return Queue.Num() < MaxQueueSize;
}

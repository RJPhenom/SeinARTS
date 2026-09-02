/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinProductionComponent.cpp
 * @brief   FSeinProductionPayload sim-payload implementation — queue state
 *          queries and build-progress accessors.
 */

#include "Components/SeinProductionPayload.h"

bool FSeinProductionPayload::IsProducing() const
{
	return Queue.Num() > 0;
}

FFixedPoint FSeinProductionPayload::GetProgressPercent() const
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

bool FSeinProductionPayload::CanQueueMore() const
{
	return Queue.Num() < MaxQueueSize;
}

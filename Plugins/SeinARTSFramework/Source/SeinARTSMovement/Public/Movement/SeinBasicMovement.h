/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBasicMovement.h
 * @brief   Baseline movement algo -- seek current waypoint at MoveSpeed,
 *          arrive within acceptance radius, increment to next waypoint.
 *
 *          No rotation, no momentum. Direct translate along the path polyline.
 *          Shipped as the framework default so "works out of the box" stays
 *          preserved for any unit that doesn't pick a specific algo.
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinMovement.h"
#include "SeinBasicMovement.generated.h"

UCLASS(meta = (DisplayName = "Basic (Seek + Arrive)"))
class SEINARTSMOVEMENT_API USeinBasicMovement : public USeinMovement
{
	GENERATED_BODY()

public:
	virtual bool Tick(const FSeinMovementContext& Ctx) override;
};

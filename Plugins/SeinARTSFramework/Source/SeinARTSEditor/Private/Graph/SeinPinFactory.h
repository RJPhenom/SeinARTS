/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPinFactory.h
 * @brief   Graph pin factory that colors fixed vector, rotator, and transform pins.
 *
 * Fixed vector, rotator, and transform pins use their UE counterparts' colors.
 * FFixedPoint scalar pins retain Unreal's default struct color.
 */

#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"
#include "SGraphPin.h"

struct FSeinPinFactory : public FGraphPanelPinFactory
{
	virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* Pin) const override;
};

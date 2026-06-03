/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPinFactory.h
 * @brief   Graph pin factory that assigns custom colors to fixed-point USTRUCT pins.
 *
 * Unreal uses a single "struct" color for all USTRUCT pins by default, so FFixedPoint /
 * FFixedVector / FFixedRotator / FFixedTransform all render as dark blue. This factory
 * inspects the pin's SubCategoryObject and returns a custom SGraphPin subclass that
 * overrides GetPinColor() so our fixed-point types visually match their UE counterparts.
 */

#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"
#include "SGraphPin.h"

struct FSeinPinFactory : public FGraphPanelPinFactory
{
	virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* Pin) const override;
};

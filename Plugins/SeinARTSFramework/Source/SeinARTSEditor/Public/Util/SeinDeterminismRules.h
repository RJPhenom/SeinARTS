/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDeterminismRules.h
 * @brief   Single source of truth for "is this BP pin type allowed in deterministic
 *          sim data". Shared by the UDS field validator (which strips bad fields on
 *          edit) and the movement-tuning export (which mirrors BP vars into a UDS).
 *          Header-only so both can use it without a link dependency.
 */

#pragma once

#include "CoreMinimal.h"
#include "EdGraphSchema_K2.h"
#include "Factories/SeinSimComponentFactory.h"

namespace SeinDeterminism
{
	/** True if a BP pin type is safe for deterministic sim data: bool, the integer
	 *  types, FName, enums, and SeinDeterministic-marked structs (native USTRUCTs with
	 *  the meta, and UDSes tagged by the factory — e.g. FFixedPoint/FFixedVector/...).
	 *  Rejects float/double/real, object/class/soft/interface refs, delegates,
	 *  string/text, wildcard, fieldpath. Container-of (array/set/map) inherits its
	 *  element category's verdict — matching the validator's category-only check. */
	inline bool IsPinTypeDeterministic(const FEdGraphPinType& PinType)
	{
		const FName Cat = PinType.PinCategory;

		if (Cat == UEdGraphSchema_K2::PC_Boolean
		 || Cat == UEdGraphSchema_K2::PC_Byte
		 || Cat == UEdGraphSchema_K2::PC_Int
		 || Cat == UEdGraphSchema_K2::PC_Int64
		 || Cat == UEdGraphSchema_K2::PC_Name
		 || Cat == UEdGraphSchema_K2::PC_Enum)
		{
			return true;
		}

		if (Cat == UEdGraphSchema_K2::PC_Struct)
		{
			const UStruct* SubStruct = Cast<UStruct>(PinType.PinSubCategoryObject.Get());
			return USeinSimComponentFactory::IsSeinDeterministicStruct(SubStruct);
		}

		return false;
	}
}

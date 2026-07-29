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
	inline bool IsTerminalTypeDeterministic(
		FName Category,
		const UObject* TypeObject)
	{
		if (Category == UEdGraphSchema_K2::PC_Boolean
		 || Category == UEdGraphSchema_K2::PC_Byte
		 || Category == UEdGraphSchema_K2::PC_Int
		 || Category == UEdGraphSchema_K2::PC_Int64
		 || Category == UEdGraphSchema_K2::PC_Name
		 || Category == UEdGraphSchema_K2::PC_Enum)
		{
			return true;
		}

		if (Category == UEdGraphSchema_K2::PC_Struct)
		{
			return USeinSimComponentFactory::IsSeinDeterministicStruct(
				Cast<UStruct>(TypeObject));
		}
		return false;
	}

	/** True if a BP pin type is safe for deterministic sim data: bool, the integer
	 *  types, FName, enums, and SeinDeterministic-marked structs (native USTRUCTs with
	 *  the meta, and UDSes tagged by the factory — e.g. FFixedPoint/FFixedVector/...).
	 *  Rejects float/double/real, object/class/soft/interface refs, delegates,
	 *  string/text, wildcard, fieldpath. Arrays and sets use the primary terminal
	 *  type; maps require both their key and value terminals to be deterministic. */
	inline bool IsPinTypeDeterministic(const FEdGraphPinType& PinType)
	{
		if (!IsTerminalTypeDeterministic(
				PinType.PinCategory,
				PinType.PinSubCategoryObject.Get()))
		{
			return false;
		}
		return !PinType.IsMap()
			|| IsTerminalTypeDeterministic(
				PinType.PinValueType.TerminalCategory,
				PinType.PinValueType.TerminalSubCategoryObject.Get());
	}
}

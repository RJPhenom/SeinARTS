/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAttributeResolver.h
 * @brief   Utility for reading/writing FFixedPoint component fields via
 *          UE reflection and resolving modifier stacks deterministically.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Attributes/SeinModifier.h"

/**
 * Static utility class for attribute resolution.
 *
 * Reads base values from component data using FProperty reflection, applies
 * modifier stacks in a deterministic order (Override > Multiply > Add), and
 * writes results back. Caches property lookups for performance.
 */
class SEINARTSCOREENTITY_API FSeinAttributeResolver
{
public:
	/**
	 * Read a named FFixedPoint field from a component data block.
	 * @return The field value, or FFixedPoint::Zero if not found or wrong type.
	 */
	static FFixedPoint ReadFixedPointField(const void* StructData, UScriptStruct* StructType, FName FieldName);

	/**
	 * Write a FFixedPoint value to a named field in a component data block.
	 * @return true if the write succeeded.
	 */
	static bool WriteFixedPointField(void* StructData, UScriptStruct* StructType, FName FieldName, FFixedPoint Value);

	/**
	 * Resolve final attribute value by applying modifiers to a base value.
	 *
	 * Resolution order:
	 *  1. If any Override modifier exists, the last one becomes the base.
	 *  2. All Add values are summed and added to the base.
	 *  3. All Multiply values are multiplied together and applied to the result.
	 *
	 * Final = (Base + SumOfAdds) * ProductOfMultiplies
	 */
	static FFixedPoint ResolveModifiers(FFixedPoint BaseValue, const TArray<FSeinModifier>& Modifiers);

	/**
	 * Find the FProperty for a named field on a struct type.
	 * Results are cached for subsequent lookups.
	 * @return The property, or nullptr if not found.
	 */
	static FProperty* FindFieldProperty(UScriptStruct* StructType, FName FieldName);

	/** Check whether a property is an FStructProperty wrapping FFixedPoint. */
	static bool IsFixedPointField(FProperty* Property);

	/** Clear the internal property cache. Call on hot-reload if needed. */
	static void ClearPropertyCache();

private:
	FSeinAttributeResolver() = delete;
};

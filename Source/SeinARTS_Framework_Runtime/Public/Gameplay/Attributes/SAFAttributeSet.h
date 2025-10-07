#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "Engine/DataTable.h"
#include "SAFAttributeSet.generated.h"

/**
 * USAFAttributeSet
 * 
 * Generic seeding from a DataTable row (by name-matching).
 * - Sets any FGameplayAttributeData property on this set when a same-named numeric
 *   field exists on the row struct.
 * - Handles Max* before their paired attributes and optionally preserves the
 *   current/max ratio when only Max* is provided.
 */
UCLASS(Abstract)
class SEINARTS_FRAMEWORK_RUNTIME_API USAFAttributeSet : public UAttributeSet {
	GENERATED_BODY()

public:

	/** Seed from a DataTable row handle (works for ANY row struct). */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Attributes")
	void SeedFromRowHandle(const FDataTableRowHandle& RowHandle, bool bMaintianMaxRatios = true);

protected:

	/** Seed from a UStruct instance (works for ANY struct). */
	void SeedFromUStruct(const UScriptStruct* RowStruct, const uint8* RowData, bool bPreserveCurrentIfOnlyMaxProvided);

	/** Helper to get a numeric field from a UStruct instance. */
	static bool TryGetNumericField(const UStruct* RowStruct, const uint8* RowData, const FName& FieldName, double& OutValue);

};

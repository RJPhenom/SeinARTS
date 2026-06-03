/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinAttributeBPFL.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Blueprint Function Library for FProperty-based attribute resolution
 * 				and modification.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "SeinAttributeBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Attribute Library"))
class SEINARTSCOREENTITY_API USeinAttributeBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Resolve an attribute value (base + all active modifiers) for a given entity, component type, and field name */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Attributes", meta = (WorldContext = "WorldContextObject", DisplayName = "Resolve Attribute"))
	static FFixedPoint SeinResolveAttribute(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, UScriptStruct* ComponentType, FName FieldName);

	/** Get the base value of an attribute (before modifiers) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Attributes", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Base Attribute"))
	static FFixedPoint SeinGetBaseAttribute(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, UScriptStruct* ComponentType, FName FieldName);

	/** Set the base value of an attribute directly */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Attributes", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Base Attribute"))
	static void SeinSetBaseAttribute(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, UScriptStruct* ComponentType, FName FieldName, FFixedPoint Value);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};

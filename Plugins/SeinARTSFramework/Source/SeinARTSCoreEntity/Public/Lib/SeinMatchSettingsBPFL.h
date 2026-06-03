/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMatchSettingsBPFL.h
 * @brief   Read-side helpers for `FSeinMatchSettings::Extensions` lookup.
 *
 * Designers compose match behavior by including USTRUCTs in the
 * `Extensions` array (see `FSeinMatchSettings`). To read them at runtime:
 *
 * C++:
 *     auto* Rules = FindMatchExtension<FMyGameRules>(WorldSubsystem.GetMatchSettings());
 *     if (Rules) { ... }
 *
 * Blueprint:
 *     SeinGetMatchExtensions(World) → TArray<FInstancedStruct>
 *     For each, use UE's built-in `Get Struct From Instanced Struct` to
 *     extract the typed payload. Or use convenience finder for known structs.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Data/SeinMatchSettings.h"
#include "StructUtils/InstancedStruct.h"
#include "SeinMatchSettingsBPFL.generated.h"

/**
 * Find an extension struct of type T in the match settings' Extensions
 * array. Returns the typed pointer or nullptr if not present. C++ callers
 * use this directly.
 */
template <typename T>
const T* FindMatchExtension(const FSeinMatchSettings& Settings)
{
	const UScriptStruct* TargetStruct = T::StaticStruct();
	for (const FInstancedStruct& Extension : Settings.Extensions)
	{
		if (Extension.GetScriptStruct() == TargetStruct)
		{
			return Extension.GetPtr<T>();
		}
	}
	return nullptr;
}

/**
 * Mutable variant. Used at lobby/preset authoring time when a designer's
 * BP wants to edit an extension struct's fields. Runtime sim code reads
 * the const variant.
 */
template <typename T>
T* FindMatchExtensionMutable(FSeinMatchSettings& Settings)
{
	const UScriptStruct* TargetStruct = T::StaticStruct();
	for (FInstancedStruct& Extension : Settings.Extensions)
	{
		if (Extension.GetScriptStruct() == TargetStruct)
		{
			return Extension.GetMutablePtr<T>();
		}
	}
	return nullptr;
}

UCLASS(meta = (DisplayName = "SeinARTS Match Settings Library"))
class SEINARTSCOREENTITY_API USeinMatchSettingsBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Snapshot accessor — current match settings on the world subsystem.
	 *  Empty Slots means no match active / WorldSubsystem unavailable. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Match",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Match Settings"))
	static FSeinMatchSettings SeinGetMatchSettings(const UObject* WorldContextObject);

	/** Direct accessor for just the Extensions list — most BP loops want
	 *  this. Iterate + use UE's built-in `Get Struct From Instanced Struct`
	 *  to extract typed payloads. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Match",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Match Extensions"))
	static TArray<FInstancedStruct> SeinGetMatchExtensions(const UObject* WorldContextObject);
};

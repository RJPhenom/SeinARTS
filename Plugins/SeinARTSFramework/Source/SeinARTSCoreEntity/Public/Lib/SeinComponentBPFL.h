/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinComponentBPFL.h
 * @brief   Generic component access — escape hatch for introspection.
 *          Per-component typed BPFLs (SeinMovementBPFL, ...)
 *          are the preferred path for known component types; the typed K2
 *          nodes `Get Component` / `Set Component` are the preferred path
 *          for designer-authored UDS sim components.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "StructUtils/InstancedStruct.h"
#include "SeinComponentBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Component Library"))
class SEINARTSCOREENTITY_API USeinComponentBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Read a component's data as FInstancedStruct, keyed by struct type. Returns false
	 *  and logs a warning on invalid handle / missing component. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Component", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Component Data"))
	static bool SeinGetComponentData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, UScriptStruct* StructType, FInstancedStruct& OutData);

	/** List every component on the entity as FInstancedStruct. Iterates every registered
	 *  storage. Useful for debug tooling and generic designer introspection. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Component", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Components"))
	static TArray<FInstancedStruct> SeinGetComponents(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	// ====================================================================
	// Typed component access (CustomThunk, BP-internal — fronted by K2 nodes)
	// ====================================================================
	//
	// These two thunks back `UK2Node_SeinGetComponent` / `UK2Node_SeinSetComponent`
	// — the BP graph editor nodes that present a struct picker and a typed
	// output/input pin. The K2 node generates a call to one of these thunks
	// with `OutStruct` / `InStruct` wired to a wildcard struct argument that
	// the engine's `CustomStructureParam` mechanism types at compile time.
	//
	// Designers never call these directly (BlueprintInternalUseOnly hides them
	// from the action menu). The K2 node is the user-facing surface; this is
	// the runtime entry point.
	//
	// Wildcard typing: the thunk reads the struct type off the connected
	// parameter at execution time, fetches storage by that type, and
	// memcopies in/out of the correctly-sized buffer. Determinism is
	// preserved — same storage path as templated AddComponent<T> / GetComponent<T>.

	/** Internal — backs `UK2Node_SeinGetComponent`. Reads a component out of
	 *  storage into a typed BP struct pin. Returns false on missing world
	 *  subsystem / null struct type / entity-doesn't-have-component. */
	UFUNCTION(BlueprintCallable, CustomThunk,
		Category = "SeinARTS|Component",
		meta = (WorldContext = "WorldContextObject",
				BlueprintInternalUseOnly = "true",
				CustomStructureParam = "OutStruct",
				DisplayName = "Get Component (Typed Internal)"))
	static bool SeinGetComponentTyped(const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle, int32& OutStruct);
	DECLARE_FUNCTION(execSeinGetComponentTyped);

	/** Internal — backs `UK2Node_SeinSetComponent`. Writes a typed BP struct
	 *  pin into component storage, replacing the existing component (or
	 *  adding it if absent). Returns false on missing world subsystem or
	 *  null struct type. */
	UFUNCTION(BlueprintCallable, CustomThunk,
		Category = "SeinARTS|Component",
		meta = (WorldContext = "WorldContextObject",
				BlueprintInternalUseOnly = "true",
				CustomStructureParam = "InStruct",
				DisplayName = "Set Component (Typed Internal)"))
	static bool SeinSetComponentTyped(const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle, const int32& InStruct);
	DECLARE_FUNCTION(execSeinSetComponentTyped);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};

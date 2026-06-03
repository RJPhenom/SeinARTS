/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinARTSEditorModule.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Editor module for SeinARTS content creation tools.
 */

#pragma once

#include "Modules/ModuleManager.h"
#include "AssetTypeCategories.h"
#include "Delegates/Delegate.h"

class IAssetTypeActions;
class FSeinDeterministicStructValidator;
class FPrimitiveDrawInterface;
struct FGraphPanelPinFactory;
struct FInstancedStruct;

/**
 * Per-component-type draw callback for the entity bridge visualizer.
 *
 * Optional system editor modules (SeinARTSCoverEditor, SeinARTSFogOfWar's
 * `#if WITH_EDITOR` block, etc.) register a delegate here at StartupModule
 * and the entity visualizer invokes each registered delegate from its
 * `DrawVisualization` pass. Decoupling rule: SeinARTSEditor only owns the
 * built-in extents draw layer + the registration plumbing — every other
 * per-component viz layer lives in its owning system's editor module, which
 * means SeinARTSEditor never `#include`s their headers and Build.cs doesn't
 * depend on them.
 *
 * Parameters:
 *   - ComponentData: the bridge's authored FInstancedStruct array. Callback
 *     walks this itself, filtering by `Entry.GetScriptStruct() ==
 *     FMyComponent::StaticStruct()`.
 *   - ActorQuat / ActorPos: world-space pose of the owning actor (or
 *     identity if the bridge has no Owner — e.g. SCS-template iteration).
 *   - PDI: the active draw interface. Lifetime is one DrawVisualization call.
 *
 * Lifecycle: registrants own un-registration in their ShutdownModule. The
 * editor module clears the map on its own Shutdown as a safety net (covers
 * the case where an optional module's editor block is disabled at runtime
 * but didn't unregister cleanly).
 */
DECLARE_DELEGATE_FourParams(FSeinComponentDataDrawDelegate,
	const TArray<FInstancedStruct>& /*ComponentData*/,
	const FQuat& /*ActorQuat*/, const FVector& /*ActorPos*/,
	FPrimitiveDrawInterface* /*PDI*/);

class SEINARTSEDITOR_API FSeinARTSEditorModule : public IModuleInterface
{
public:
	// Non-inline ctor/dtor declared explicitly so TUniquePtr<UDSValidator> +
	// TSharedPtr<SeinPinFactory> with forward-declared element types only
	// need their full definitions in the cpp, not in this header. Without
	// these, the dllexport-induced implicit destructor instantiation forces
	// the validator + pin-factory headers into every external module that
	// includes us — defeats the whole point of forward declarations.
	FSeinARTSEditorModule();
	~FSeinARTSEditorModule();

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static EAssetTypeCategories::Type GetAssetCategoryBit();

	// ====================================================================
	// Per-component-type draw callback registry
	// ====================================================================

	/** Register a draw callback for the entity bridge visualizer. `Key` is a
	 *  human-readable identifier (typically the registering struct's name,
	 *  e.g. `"SeinCoverComponent"`) used for Unregister + diagnostic logging.
	 *  Re-registering with the same key replaces the existing delegate. */
	void RegisterComponentDataDraw(FName Key, FSeinComponentDataDrawDelegate Draw);

	/** Remove a previously-registered callback. No-op if `Key` was never
	 *  registered or has already been removed. Safe to call from
	 *  ShutdownModule even when the editor module is already torn down
	 *  (the manager handles missing modules gracefully). */
	void UnregisterComponentDataDraw(FName Key);

	/** Const read access to the registry — used by the entity visualizer's
	 *  draw pass. Map order is insertion-order via TMap; the per-frame walk
	 *  is O(N) where N is the number of registered layers (currently small
	 *  — extents is built-in, cover + FoW are the optional registrants). */
	const TMap<FName, FSeinComponentDataDrawDelegate>& GetComponentDataDraws() const
	{
		return ComponentDataDraws;
	}

private:
	void RegisterAssetTypeActions();
	void UnregisterAssetTypeActions();

	TArray<TSharedPtr<IAssetTypeActions>> RegisteredActions;
	TSharedPtr<FGraphPanelPinFactory> SeinPinFactory;
	TUniquePtr<FSeinDeterministicStructValidator> UDSValidator;
	TMap<FName, FSeinComponentDataDrawDelegate> ComponentDataDraws;
	FDelegateHandle OnAssetRenamedHandle;  // auto-tag-generation rename hook
	static EAssetTypeCategories::Type SeinARTSCategoryBit;
};

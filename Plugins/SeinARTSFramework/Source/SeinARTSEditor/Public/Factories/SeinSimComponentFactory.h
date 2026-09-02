/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinSimComponentFactory.h
 * @author       RJ Macklem
 * @created      2 Jun 2026
 * @latest       14 Aug 2026
 * @brief        Declares the designer Component-struct factory and durable UDS
 *               metadata helpers used by entity components and generated
 *               deterministic sub-data.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "SeinSimComponentFactory.generated.h"

class UStruct;
class UUserDefinedStruct;

UCLASS()
class SEINARTSEDITOR_API USeinSimComponentFactory : public UFactory
{
	GENERATED_BODY()

public:
	USeinSimComponentFactory();

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	/** Retired from the New-asset menu (2026-09-01): raw UDS components were
	 *  authored into the bridge's ComponentData picker, which is now hidden —
	 *  designer components are Blueprint subclasses of USeinDataComponent
	 *  (see USeinDataComponentBlueprintFactory). The factory class remains
	 *  for its Mark/eligibility helpers, used by the payload sync and the
	 *  movement tuning export. */
	virtual bool ShouldShowInNewMenu() const override { return false; }
	virtual FName GetNewAssetThumbnailOverride() const override { return TEXT("ClassThumbnail.SeinSimComponent"); }
	virtual FName GetNewAssetIconOverride() const override { return TEXT("ClassIcon.SeinSimComponent"); }

	/** Struct-level UField metadata key written on UDSes created by this factory,
	 *  and present on every native USTRUCT marked `USTRUCT(meta = (SeinDeterministic))`.
	 *  Pin-type + struct-viewer filters key off this via UStruct::HasMetaData. The
	 *  UDS marker is also stored in its editor-data metadata map so UE restores it
	 *  after every structure compile. */
	static const FName SeinDeterministicMetaKey;

	/** Struct-level UField metadata key signalling "this struct is composable as
	 *  a top-level entity component" — i.e. it can be picked as an entry of
	 *  `USeinEntityBridgeComponent::ComponentData`. Written on UDSes created by this
	 *  factory; native USTRUCTs that subclass `FSeinPayload` are
	 *  automatically eligible via inheritance (the filter checks IsChildOf
	 *  for natives, the meta tag for UDSes — UE clears UDS supersuper to
	 *  nullptr on every compile, so inheritance isn't observable there).
	 *
	 *  Native structs that should be hidden from the entity bridge picker
	 *  even though they inherit FSeinPayload can opt out via
	 *  `USTRUCT(meta = (SeinSubData))` — e.g. per-class movement sub-data
	 *  (`FSeinWheeledMovementData`) which surfaces only inside
	 *  `FSeinMovementPayload::MovementClassData`, never directly. */
	static const FName SeinEntityComponentMetaKey;

	/** Struct-level UField metadata key marking a struct as sub-data for
	 *  another component's polymorphic FInstancedStruct field. Such structs
	 *  are filtered OUT of the entity bridge's top-level ComponentData
	 *  picker but remain visible in the sub-data picker that owns them. */
	static const FName SeinSubDataMetaKey;

	/** Durably mark a designer-authored UDS as a top-level entity component.
	 *  Updates both the live struct metadata and UE's persistent editor-data map. */
	static void MarkUserDefinedStructAsEntityComponent(
		UUserDefinedStruct* Struct);

	/** Durably mark a generated UDS as deterministic per-component sub-data.
	 *  Updates both the live struct metadata and UE's persistent editor-data map. */
	static void MarkUserDefinedStructAsSubData(UUserDefinedStruct* Struct);

	/** Returns true iff the struct carries the `SeinDeterministic` UField metadata.
	 *  Works for both native USTRUCTs (meta populated by UHT from the USTRUCT macro)
	 *  and UDSes (meta populated by this factory on creation). */
	static bool IsSeinDeterministicStruct(const UStruct* Struct);

	/** Returns true iff the struct is eligible as a top-level entity component
	 *  — i.e. acceptable as a `USeinEntityBridgeComponent::ComponentData` entry.
	 *  Rules:
	 *    - Native USTRUCT: must inherit `FSeinPayload` AND must carry
	 *      `SeinDeterministic` AND must NOT carry `SeinSubData`.
	 *    - UDS: must carry `SeinDeterministic` AND `SeinEntityComponent`.
	 *
	 *  The asymmetry is forced by UE's UDS compiler — it strips supersuper
	 *  pointers on every compile (see UserDefinedStructureCompilerUtils.cpp's
	 *  `StructToClean->SetSuperStruct(nullptr)`), so IsChildOf is unreliable
	 *  on UDS and we fall back to a discrete marker tag. */
	static bool IsSeinEntityComponentStruct(const UStruct* Struct);
};

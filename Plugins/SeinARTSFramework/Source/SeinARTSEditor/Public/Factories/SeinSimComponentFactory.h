/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimComponentFactory.h
 * @brief   Right-click → Component factory. Creates a UUserDefinedStruct
 *          tagged with the `SeinDeterministic` package metadata key so
 *          the framework's pin-type / struct-viewer filters know to apply
 *          the deterministic-types whitelist when designers edit it.
 *
 *          The resulting UDS is accepted directly as an entry in an entity
 *          bridge's `ComponentData` array (the picker filters on the
 *          `SeinDeterministic` + `SeinEntityComponent` metadata); spawn injects
 *          the struct payload directly — there is no wrapper actor-component.
 */

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "SeinSimComponentFactory.generated.h"

class UStruct;
class UUserDefinedStruct;

UCLASS()
class USeinSimComponentFactory : public UFactory
{
	GENERATED_BODY()

public:
	USeinSimComponentFactory();

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
	virtual FName GetNewAssetThumbnailOverride() const override { return TEXT("ClassThumbnail.SeinSimComponent"); }
	virtual FName GetNewAssetIconOverride() const override { return TEXT("ClassIcon.SeinSimComponent"); }

	/** Struct-level UField metadata key written on UDSes created by this factory,
	 *  and present on every native USTRUCT marked `USTRUCT(meta = (SeinDeterministic))`.
	 *  Pin-type + struct-viewer filters key off this via UStruct::HasMetaData. */
	static const FName SeinDeterministicMetaKey;

	/** Struct-level UField metadata key signalling "this struct is composable as
	 *  a top-level entity component" — i.e. it can be picked as an entry of
	 *  `USeinEntityComponent::ComponentData`. Written on UDSes created by this
	 *  factory; native USTRUCTs that subclass `FSeinComponent` are
	 *  automatically eligible via inheritance (the filter checks IsChildOf
	 *  for natives, the meta tag for UDSes — UE clears UDS supersuper to
	 *  nullptr on every compile, so inheritance isn't observable there).
	 *
	 *  Native structs that should be hidden from the entity bridge picker
	 *  even though they inherit FSeinComponent can opt out via
	 *  `USTRUCT(meta = (SeinSubData))` — e.g. per-class movement sub-data
	 *  (`FSeinWheeledMovementData`) which surfaces only inside
	 *  `FSeinMovementComponent::MovementClassData`, never directly. */
	static const FName SeinEntityComponentMetaKey;

	/** Struct-level UField metadata key marking a struct as sub-data for
	 *  another component's polymorphic FInstancedStruct field. Such structs
	 *  are filtered OUT of the entity bridge's top-level ComponentData
	 *  picker but remain visible in the sub-data picker that owns them. */
	static const FName SeinSubDataMetaKey;

	/** Returns true iff the struct carries the `SeinDeterministic` UField metadata.
	 *  Works for both native USTRUCTs (meta populated by UHT from the USTRUCT macro)
	 *  and UDSes (meta populated by this factory on creation). */
	static bool IsSeinDeterministicStruct(const UStruct* Struct);

	/** Returns true iff the struct is eligible as a top-level entity component
	 *  — i.e. acceptable as a `USeinEntityComponent::ComponentData` entry.
	 *  Rules:
	 *    - Native USTRUCT: must inherit `FSeinComponent` AND must carry
	 *      `SeinDeterministic` AND must NOT carry `SeinSubData`.
	 *    - UDS: must carry `SeinDeterministic` AND `SeinEntityComponent`.
	 *
	 *  The asymmetry is forced by UE's UDS compiler — it strips supersuper
	 *  pointers on every compile (see UserDefinedStructureCompilerUtils.cpp's
	 *  `StructToClean->SetSuperStruct(nullptr)`), so IsChildOf is unreliable
	 *  on UDS and we fall back to a discrete marker tag. */
	static bool IsSeinEntityComponentStruct(const UStruct* Struct);
};

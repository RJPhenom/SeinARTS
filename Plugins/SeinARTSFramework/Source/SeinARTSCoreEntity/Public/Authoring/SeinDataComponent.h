/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinDataComponent.h
 * @date:    9/1/2026
 * @author:  RJ Macklem
 * @brief:   PROTOTYPE (AC-authoring gate): abstract data-only ActorComponent
 *           base for authoring sim components with native Unreal component
 *           ergonomics — add to the unit Blueprint, tweak per placed instance,
 *           edit live in PIE — while the deterministic sim keeps injecting
 *           plain payload structs exactly as today.
 *
 *           Two-layer contract:
 *             - AUTHORING layer (this class + subclasses): editor-only
 *               ActorComponents (`bIsEditorOnly` — never serialized into
 *               cooked builds, excluded from uncooked -game via
 *               NeedsLoadForEditorGame). Native subclasses embed their
 *               payload struct directly; designer Blueprint subclasses get a
 *               paired UserDefinedStruct auto-synced from their variables
 *               (SeinDataComponentSync, editor module).
 *             - RUNTIME carrier: the entity bridge's ComponentData array,
 *               baked from these components (see
 *               USeinEntityBridgeComponent::BakeAuthoredDataComponents). Spawn
 *               injection is unchanged.
 *
 *           Data-only is a hard contract: no tick (sealed in the ctor), no
 *           event graphs, no latent state — the editor compile gate errors on
 *           any graph content in Blueprint subclasses. Logic belongs in
 *           abilities, effects, and systems.
 *
 *           NAMING NOTE: this base takes the name `USeinEntityBridgeComponent`
 *           after the bridge is renamed to `USeinEntityBridgeComponent`
 *           (agreed migration Phase 1). The scratch name keeps the prototype
 *           diff independent of that rename.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SeinExtentsComponent.h"
#include "StructUtils/InstancedStruct.h"
#include "SeinDataComponent.generated.h"

class UUserDefinedStruct;

/**
 * Abstract base for authoring-side sim data components. Designers add
 * concrete subclasses to a unit Blueprint via the ordinary Add Component
 * flow; per-instance edits on placed actors and PIE-time edits use native
 * Unreal component semantics. The bridge bakes enabled components into its
 * ComponentData array, which remains the injected runtime carrier.
 */
UCLASS(Abstract, Blueprintable, ClassGroup = (SeinARTS),
	meta = (DisplayName = "Sein Data Component"))
class SEINARTSCOREENTITY_API USeinDataComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USeinDataComponent();

	/** Include this component's payload at spawn injection. Uncheck on a
	 *  placed instance for the native "this unit doesn't carry X" gesture —
	 *  Unreal has no per-instance component removal, so this is the supported
	 *  substitute. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS",
		meta = (DisplayName = "Injection Enabled"))
	bool bInjectionEnabled = true;

	/** Blueprint-authored subclasses: the payload struct auto-synced from the
	 *  Blueprint's variables on compile (field identity is rename-stable via
	 *  source-variable GUID stamps). Native subclasses ignore this — they
	 *  override GetPayloadStruct with their embedded struct's type. */
	UPROPERTY(VisibleDefaultsOnly, AdvancedDisplay, Category = "SeinARTS",
		meta = (DisplayName = "Payload Struct"))
	TObjectPtr<UUserDefinedStruct> PayloadStruct;

	/** The struct type this component bakes into ComponentData, or null when
	 *  unresolvable (BP subclass whose payload has not synced yet). */
	virtual const UScriptStruct* GetPayloadStruct() const;

	/** Fill Out with this component's current values as one payload-struct
	 *  instance. Base implementation covers Blueprint subclasses: initialize
	 *  the paired payload struct, then copy same-named, same-typed properties
	 *  from this component instance onto the struct's fields (the sync keeps
	 *  names and types mirrored). Native subclasses override with a direct
	 *  struct copy. */
	virtual bool WritePayload(FInstancedStruct& Out) const;

#if WITH_EDITOR
	/** Route edits into the bridge: bake this component's payload into the
	 *  owner's ComponentData through the ordinary edit pipeline, which
	 *  already handles instance-override bookkeeping and PIE live-tuning
	 *  command dispatch. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

/**
 * Native authoring component for the entity's physical extents. The payload
 * struct is embedded directly — per-field instance overrides and Blueprint
 * inheritance work property-by-property with zero mirror drift.
 *
 * NAMING: UHT forbids a U class and an F struct sharing one engine name, so
 * native authoring components carry the DataComponent suffix in C++ while the
 * payload structs keep their names. Designers only ever see the DisplayName.
 * (Productization may instead rename payload structs to *Payload and reclaim
 * the short class names — RJ's call, cosmetic either way.)
 */
UCLASS(NotBlueprintable, ClassGroup = (SeinARTS),
	meta = (BlueprintSpawnableComponent, DisplayName = "Extents Component"))
class SEINARTSCOREENTITY_API USeinExtentsDataComponent : public USeinDataComponent
{
	GENERATED_BODY()

public:
	/** Authored extents payload, baked verbatim into ComponentData. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS",
		meta = (DisplayName = "Extents", ShowOnlyInnerProperties))
	FSeinExtentsComponent Extents;

	virtual const UScriptStruct* GetPayloadStruct() const override;
	virtual bool WritePayload(FInstancedStruct& Out) const override;
};

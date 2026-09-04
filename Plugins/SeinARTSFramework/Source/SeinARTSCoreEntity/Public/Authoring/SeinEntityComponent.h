/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinEntityComponent.h
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
 *           NAMING NOTE: this base takes the name `USeinEntityComponent` once
 *           the content resave retires the bridge's ClassRedirect (the bridge
 *           is already `USeinEntityBridgeComponent`). Until then the scratch
 *           name stands — redirects apply to imports unconditionally, so the
 *           freed name cannot be reused while its redirect lives.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SeinAbilityPayload.h"
#include "Components/SeinActiveEffectsPayload.h"
#include "Components/SeinChildTransformsPayload.h"
#include "Components/SeinConstructionPayload.h"
#include "Components/SeinEntityControlPayload.h"
#include "Components/SeinExtentsPayload.h"
#include "Components/SeinIdentityPayload.h"
#include "Components/SeinMovementPayload.h"
#include "Components/SeinNavigationPayload.h"
#include "Components/SeinProduciblePayload.h"
#include "Components/SeinProductionPayload.h"
#include "Components/SeinSquadMemberPayload.h"
#include "Components/SeinSquadPayload.h"
#include "StructUtils/InstancedStruct.h"
#include "SeinEntityComponent.generated.h"

class UUserDefinedStruct;

/**
 * Abstract base for authoring-side sim data components. Designers add
 * concrete subclasses to a unit Blueprint via the ordinary Add Component
 * flow; per-instance edits on placed actors and PIE-time edits use native
 * Unreal component semantics. The bridge bakes enabled components into its
 * ComponentData array, which remains the injected runtime carrier.
 */
UCLASS(Abstract, Blueprintable, ClassGroup = (SeinARTS))
class SEINARTSCOREENTITY_API USeinEntityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USeinEntityComponent();

	/** Include this component's payload at spawn injection. Uncheck on a
	 *  placed instance for the native "this unit doesn't carry X" gesture —
	 *  Unreal has no per-instance component removal, so this is the supported
	 *  substitute. AdvancedDisplay: rarely touched, so it sits in the Advanced
	 *  section beside PayloadStruct instead of leading the payload fields. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "SeinARTS",
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
	/** Inverse of WritePayload, used ONCE at first capture: when this
	 *  component newly takes over a payload type that already has a baked
	 *  (possibly hand-authored legacy) ComponentData entry, the entry's
	 *  values are copied INTO this component so migration preserves tuned
	 *  data instead of stomping it with class defaults. Only properties
	 *  still identical to this component's archetype are seeded — a
	 *  designer's explicit pre-bake edits win. Handles both shapes
	 *  generically: a native subclass's single embedded payload-struct
	 *  member, or a Blueprint subclass's mirrored top-level properties. */
	void SeedFromPayload(const FInstancedStruct& Entry);
#endif

#if WITH_EDITOR
	/** Route edits into the bridge: bake this component's payload into the
	 *  owner's ComponentData through the ordinary edit pipeline, which
	 *  already handles instance-override bookkeeping and PIE live-tuning
	 *  command dispatch. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

// =============================================================================
// Native authoring components
// =============================================================================
//
// NAMING CONTRACT (blessed 2026-09-01): classes carry the exact designer-
// facing names and NO DisplayName metas — the engine derives both surfaces
// from the class name itself (Add menu: prefix-stripped, "Component"-chopped,
// camel-split → "Sein Extents"; hierarchy variable: prefix/suffix-stripped →
// "SeinExtents"), identical to how USkeletalMeshComponent becomes
// "Skeletal Mesh" / "SkeletalMesh". The payload structs vacated these names
// (FSein*Component → FSein*Payload) because UHT forbids a U class and an F
// struct sharing one engine name.
//
// Each concrete embeds its payload struct directly: per-field Blueprint
// inheritance and per-field instance overrides are native engine behavior,
// with zero mirror drift. (No shared macro for these bodies: UHT does not
// expand user macros, so a UPROPERTY inside one is INVISIBLE to reflection —
// caught 2026-09-01 when the fleet shipped unreflected. Longhand or bust.)
// FSeinFogVisibilityPayload has no authoring
// component — it is auto-injected from the bridge's top-level fields.


/** Physical extents (footprint / bounds) of the entity. */
UCLASS(NotBlueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSCOREENTITY_API USeinExtentsComponent : public USeinEntityComponent
{
	GENERATED_BODY()

public:
	/** Authored extents payload, baked verbatim into ComponentData. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS",
		meta = (ShowOnlyInnerProperties))
	FSeinExtentsPayload Extents;

	virtual const UScriptStruct* GetPayloadStruct() const override;
	virtual bool WritePayload(FInstancedStruct& Out) const override;
};

/** Abilities this entity can activate. */
UCLASS(NotBlueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSCOREENTITY_API USeinAbilitiesComponent : public USeinEntityComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "SeinARTS", meta = (ShowOnlyInnerProperties))
	FSeinAbilityPayload Abilities;

	virtual const UScriptStruct* GetPayloadStruct() const override
	{
		return FSeinAbilityPayload::StaticStruct();
	}
	virtual bool WritePayload(FInstancedStruct& Out) const override
	{
		Out.InitializeAs<FSeinAbilityPayload>(Abilities);
		return true;
	}
};

/** Identity: display name, description, icons, identity tag. */
UCLASS(NotBlueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSCOREENTITY_API USeinIdentityComponent : public USeinEntityComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "SeinARTS", meta = (ShowOnlyInnerProperties))
	FSeinIdentityPayload Identity;

	virtual const UScriptStruct* GetPayloadStruct() const override
	{
		return FSeinIdentityPayload::StaticStruct();
	}
	virtual bool WritePayload(FInstancedStruct& Out) const override
	{
		Out.InitializeAs<FSeinIdentityPayload>(Identity);
		return true;
	}
};

/** Movement configuration (mode class + tuning sub-data). */
UCLASS(NotBlueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSCOREENTITY_API USeinMovementComponent : public USeinEntityComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "SeinARTS", meta = (ShowOnlyInnerProperties))
	FSeinMovementPayload Movement;

	virtual const UScriptStruct* GetPayloadStruct() const override
	{
		return FSeinMovementPayload::StaticStruct();
	}
	virtual bool WritePayload(FInstancedStruct& Out) const override
	{
		Out.InitializeAs<FSeinMovementPayload>(Movement);
		return true;
	}
};

/** Navigation configuration (layer mask, footprint routing). */
UCLASS(NotBlueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSCOREENTITY_API USeinNavigationComponent : public USeinEntityComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "SeinARTS", meta = (ShowOnlyInnerProperties))
	FSeinNavigationPayload Navigation;

	virtual const UScriptStruct* GetPayloadStruct() const override
	{
		return FSeinNavigationPayload::StaticStruct();
	}
	virtual bool WritePayload(FInstancedStruct& Out) const override
	{
		Out.InitializeAs<FSeinNavigationPayload>(Navigation);
		return true;
	}
};

/** Production queue capability (this entity can produce). */
UCLASS(NotBlueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSCOREENTITY_API USeinProductionComponent : public USeinEntityComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "SeinARTS", meta = (ShowOnlyInnerProperties))
	FSeinProductionPayload Production;

	virtual const UScriptStruct* GetPayloadStruct() const override
	{
		return FSeinProductionPayload::StaticStruct();
	}
	virtual bool WritePayload(FInstancedStruct& Out) const override
	{
		Out.InitializeAs<FSeinProductionPayload>(Production);
		return true;
	}
};

/** Producible capability (this entity can be produced; costs/time). */
UCLASS(NotBlueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSCOREENTITY_API USeinProducibleComponent : public USeinEntityComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "SeinARTS", meta = (ShowOnlyInnerProperties))
	FSeinProduciblePayload Producible;

	virtual const UScriptStruct* GetPayloadStruct() const override
	{
		return FSeinProduciblePayload::StaticStruct();
	}
	virtual bool WritePayload(FInstancedStruct& Out) const override
	{
		Out.InitializeAs<FSeinProduciblePayload>(Producible);
		return true;
	}
};

/** Construction-site state (built by workers over time). */
UCLASS(NotBlueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSCOREENTITY_API USeinConstructionComponent : public USeinEntityComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "SeinARTS", meta = (ShowOnlyInnerProperties))
	FSeinConstructionPayload Construction;

	virtual const UScriptStruct* GetPayloadStruct() const override
	{
		return FSeinConstructionPayload::StaticStruct();
	}
	virtual bool WritePayload(FInstancedStruct& Out) const override
	{
		Out.InitializeAs<FSeinConstructionPayload>(Construction);
		return true;
	}
};

/** Entity control routing (selectability, command brokering). */
UCLASS(NotBlueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSCOREENTITY_API USeinEntityControlComponent : public USeinEntityComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "SeinARTS", meta = (ShowOnlyInnerProperties))
	FSeinEntityControlPayload EntityControl;

	virtual const UScriptStruct* GetPayloadStruct() const override
	{
		return FSeinEntityControlPayload::StaticStruct();
	}
	virtual bool WritePayload(FInstancedStruct& Out) const override
	{
		Out.InitializeAs<FSeinEntityControlPayload>(EntityControl);
		return true;
	}
};

/** Active-effects storage seed. */
UCLASS(NotBlueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSCOREENTITY_API USeinActiveEffectsComponent : public USeinEntityComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "SeinARTS", meta = (ShowOnlyInnerProperties))
	FSeinActiveEffectsPayload ActiveEffects;

	virtual const UScriptStruct* GetPayloadStruct() const override
	{
		return FSeinActiveEffectsPayload::StaticStruct();
	}
	virtual bool WritePayload(FInstancedStruct& Out) const override
	{
		Out.InitializeAs<FSeinActiveEffectsPayload>(ActiveEffects);
		return true;
	}
};

/** Child transform sockets (turrets, hardpoints, attachments). */
UCLASS(NotBlueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSCOREENTITY_API USeinChildTransformsComponent : public USeinEntityComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "SeinARTS", meta = (ShowOnlyInnerProperties))
	FSeinChildTransformsPayload ChildTransforms;

	virtual const UScriptStruct* GetPayloadStruct() const override
	{
		return FSeinChildTransformsPayload::StaticStruct();
	}
	virtual bool WritePayload(FInstancedStruct& Out) const override
	{
		Out.InitializeAs<FSeinChildTransformsPayload>(ChildTransforms);
		return true;
	}
};

/** Squad definition (slots, formation, reinforcement). */
UCLASS(NotBlueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSCOREENTITY_API USeinSquadComponent : public USeinEntityComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "SeinARTS", meta = (ShowOnlyInnerProperties))
	FSeinSquadPayload Squad;

	virtual const UScriptStruct* GetPayloadStruct() const override
	{
		return FSeinSquadPayload::StaticStruct();
	}
	virtual bool WritePayload(FInstancedStruct& Out) const override
	{
		Out.InitializeAs<FSeinSquadPayload>(Squad);
		return true;
	}
};

/** Squad-member linkage for entities that join squads. */
UCLASS(NotBlueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent))
class SEINARTSCOREENTITY_API USeinSquadMemberComponent : public USeinEntityComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "SeinARTS", meta = (ShowOnlyInnerProperties))
	FSeinSquadMemberPayload SquadMember;

	virtual const UScriptStruct* GetPayloadStruct() const override
	{
		return FSeinSquadMemberPayload::StaticStruct();
	}
	virtual bool WritePayload(FInstancedStruct& Out) const override
	{
		Out.InitializeAs<FSeinSquadMemberPayload>(SquadMember);
		return true;
	}
};

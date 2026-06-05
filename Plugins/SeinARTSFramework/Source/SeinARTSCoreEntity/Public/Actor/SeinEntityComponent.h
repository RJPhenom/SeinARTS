/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinEntityComponent.h
 * @date:    2/28/2026 (originally as SeinActorBridge; renamed 2026-05-18)
 * @author:  RJ Macklem
 * @brief:   The single actor component linking a UE render actor to its
 *           SeinARTS sim entity. Default subobject on every ASeinActor.
 *           One component, one source of truth — there is no separate
 *           "bridge" / "data" / "definition" split anymore.
 *
 *           Responsibilities:
 *             - Owns the FSeinEntityHandle that bridges actor ↔ sim entity.
 *             - Interpolates between sim-tick transform snapshots for smooth
 *               rendering each render frame.
 *             - Forwards visual events from the sim to the owning ASeinActor.
 *             - AUTHORING surface: top-level bIsAbstract flag (no actor at all)
 *               + a homogeneous TArray<FInstancedStruct> ComponentData that
 *               injects every authored sim-data struct into deterministic
 *               component storage at spawn.
 *
 *           Note on filename history: kept as `SeinActorBridge.h/.cpp` for
 *           several months under the "Bridge" name to distinguish it from
 *           FSeinComponent (the sim USTRUCT base class). The component grew
 *           to encompass authoring as part of the Phase-1 unified-component
 *           refactor; the rename to `SeinEntityComponent` reflects that
 *           expanded role. Class display name in the editor is "Entity
 *           Component"; subobject name on ASeinActor is preserved as
 *           "ActorBridge" to keep existing BP saves stable.
 *
 *           Editor-preview history: an in-editor squad-slot preview that
 *           cloned member meshes onto the squad actor was attempted and
 *           removed (2026-05-20). The implementation interacted badly with
 *           UE's BP-compile reinstantiation (a transient SquadPreviewRoot
 *           USceneComponent was being auto-promoted to the actor's
 *           RootComponent, displacing DefaultSceneRoot; subsequent compiles
 *           lost the placed transform and orphaned BP-authored components
 *           like Widget). The preview path has been stripped entirely; a
 *           future preview should NOT touch the actor's root component, NOT
 *           live on USeinEntityComponent, and NOT run during BP
 *           reinstantiation lifecycle hooks. The runtime cover module's
 *           formation-preview decals (`USeinFormationPreviewSubsystem`) are
 *           a separate render-time surface and are NOT affected.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SeinFogVisibilityPolicy.h"   // FogVisibilityPolicy enum
#include "Core/SeinEntityHandle.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "Types/Transform.h"
#include "SeinEntityComponent.generated.h"

class USeinWorldSubsystem;
struct FSeinVisualEvent;

/** Per-entity multicast delegate fired by `USeinEntityComponent::HandleVisualEvent`
 *  when the bridge subsystem routes a visual event to this entity. Subscribed
 *  to by render-side components (e.g. `USeinConstructionRenderComponent`) so
 *  they react without inheriting from a SeinARTS base class — keeps the
 *  "render ACs are UE-native" boundary clean.
 *
 *  Dynamic so BP-authored render components can `Bind Event` to it. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSeinEntityVisualEvent, const FSeinVisualEvent&, Event);

/**
 * The unified entity component. Designers find this as "Entity Component"
 * in the actor's Details panel — the one place that ties an actor BP to
 * its sim entity. New designers see two things on it:
 *   - Is Abstract (top-level bool)
 *   - Component Data (array of sim-data structs, picker filtered to
 *     FSeinComponent substructs + SeinDeterministic UDS)
 *
 * Render-side fields (transform sync, interpolation) remain available
 * under their own categories for power users; they affect only the visual
 * representation, not the sim.
 */
UCLASS(ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent, DisplayName = "SeinARTS Entity Bridge"))
class SEINARTSCOREENTITY_API USeinEntityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USeinEntityComponent();

	// =========================================================================
	// Entity authoring (Phase-1 unified-component refactor)
	// =========================================================================

	/** Top-level: this entity has no render-side existence at all. Abstract
	 *  entities exist purely in the sim — no actor bridge, no visual presence.
	 *  Examples: squads (members have actors, the squad itself doesn't),
	 *  command brokers, scenario owners. The framework reads this BEFORE any
	 *  component-data lookup; abstract entities skip the actor spawn path
	 *  entirely.
	 *
	 *  Default false — most entities have actor presence. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS")
	bool bIsAbstract = false;

	/** Authored gameplay tags this entity carries. Seeds the entity's
	 *  BaseTags + refcounted CombinedTags at spawn — designers don't add a
	 *  TagsComponent or array entry to author tags; this is THE authoring
	 *  surface. Runtime tag mutation flows through
	 *  USeinWorldSubsystem::GrantTag/UngrantTag (and AddBaseTag/
	 *  RemoveBaseTag for persistent-set edits) so refcounts and the global
	 *  EntityTagIndex stay consistent.
	 *
	 *  Tags are universal to every entity — there's no per-entity sim
	 *  component for tag state. The world subsystem maintains a centralized
	 *  EntityTagStates map; this UPROPERTY is the seed it reads at spawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS")
	FGameplayTagContainer BaseTags;

	// =========================================================================
	// Fog-of-war visibility (universal — every entity has this)
	// =========================================================================
	//
	// Authored here on the bridge (top-level) rather than as an optional
	// ComponentData entry, because every entity has a visibility policy +
	// emission mask — defaults aren't a meaningful design choice, they're
	// "you forgot to set it." Same shape as `bIsAbstract` and `BaseTags` —
	// universal attrs live on the bridge directly. At spawn,
	// `InjectAuthoredComponents` populates a sim-side
	// `FSeinFogVisibilityComponent` with these values so FoW sim code
	// (`USeinFogOfWar::IsEntityVisibleToObserver`) can read them without
	// touching the actor.

	/** Persistence policy AFTER reveal:
	 *    VisionLayersOnly (default) — visible only while currently spotted.
	 *      Standard for enemy units.
	 *    VisibleOnceExplored        — visible once scouted; ghost-revealed
	 *      forever after. Standard for enemy buildings.
	 *    AlwaysVisible              — bypasses fog entirely. Cover providers,
	 *      persistent destructibles, self-occluding effects whose stamp
	 *      blocks vision past them but whose actor must stay rendered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|FogOfWar")
	ESeinFogVisibilityPolicy FogVisibilityPolicy = ESeinFogVisibilityPolicy::VisionLayersOnly;

	/** Which observer FoW layer bits actually see this entity. An observer's
	 *  vision query on a given layer bit only "spots" this entity if its mask
	 *  is non-zero in that bit. Independent of FogVisibilityPolicy (policy =
	 *  persistence after reveal; this = who can reveal in the first place).
	 *
	 *  Default 0xFE (all bits set except Explored / E, which is sticky-reveal
	 *  and shouldn't grant initial spotting). Designers narrow this for
	 *  stealth / camo units (e.g. set to N0 only; only observers stamping the
	 *  N0 layer can see it). Runtime mutation flows through
	 *  `USeinFogOfWarBPFL::SeinSetEntityEmissionMask` and friends, which
	 *  flip the sim-side `FSeinFogVisibilityComponent::FogVisibilityLayerMask`
	 *  for cloak / detect mechanics at ability time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|FogOfWar",
		meta = (Bitmask, BitmaskEnum = "/Script/SeinARTSFogOfWar.ESeinFogOfWarLayerBit"))
	uint8 FogVisibilityLayerMask = 0xFE;

	/** Composed sim data for this entity. Each entry is a deterministic struct
	 *  injected into component storage at spawn — one entry per unique struct
	 *  type. Order doesn't matter (storage is keyed by `UScriptStruct*`).
	 *
	 *  Filter: each array element's struct picker is narrowed via
	 *  `SeinEntityComponentsOnly`, which `FSeinInstancedStructFilter` routes
	 *  through `USeinSimComponentFactory::IsSeinEntityComponentStruct`.
	 *  Acceptance rules:
	 *    - Native USTRUCT must inherit `FSeinComponent`, carry
	 *      `SeinDeterministic`, and NOT carry `SeinSubData`.
	 *    - UDS must carry both `SeinDeterministic` and `SeinEntityComponent`
	 *      (stamped by `USeinSimComponentFactory` on creation).
	 *  This excludes engine structs, non-sim USTRUCTs, and per-class sub-data
	 *  (FSeinWheeledMovementData etc.) which surface only on their owning
	 *  component's polymorphic sub-data picker, never here.
	 *
	 *  NOTE: do NOT add `ShowOnlyInnerProperties` here — that meta is for
	 *  single FInstancedStruct properties (it inlines the inner fields
	 *  directly). On a TArray it silently breaks the details panel layout
	 *  and renders the entire component blank. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS",
		meta = (SeinEntityComponentsOnly))
	TArray<FInstancedStruct> ComponentData;

	/** Find the first authored entry of the given struct type, or nullptr.
	 *  Edit-time / CDO-side convenience — runtime should query component
	 *  storage through `USeinWorldSubsystem::GetComponent<T>` instead. */
	template<typename T>
	const T* FindAuthoredData() const
	{
		const UScriptStruct* Wanted = T::StaticStruct();
		for (const FInstancedStruct& Entry : ComponentData)
		{
			if (Entry.IsValid() && Entry.GetScriptStruct() == Wanted)
			{
				return Entry.GetPtr<const T>();
			}
		}
		return nullptr;
	}

	/** Iterate ComponentData and inject every entry into the world subsystem's
	 *  component storage for `Handle`. Called by `USeinWorldSubsystem::SpawnEntity`
	 *  after the legacy typed-AC walk; duplicate struct types (already injected
	 *  by a typed AC) overwrite with a warning during the migration window. */
	void InjectAuthoredComponents(USeinWorldSubsystem& World, FSeinEntityHandle Handle) const;

	// =========================================================================
	// Bridge runtime (carried over from the previous USeinActorBridge role)
	// =========================================================================

	// UActorComponent interface
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	/** Watches manual edits to ComponentData entries.
	 *
	 *  Two jobs:
	 *    1. When a designer edits the `IdentityTag` field inside an
	 *       FSeinIdentityComponent entry, flip the matching
	 *       `bAutoGeneratedTag` flag to false so the auto-tag system knows
	 *       the designer owns it.
	 *    2. Mirror BP-CDO ComponentData edits into placed-actor instances
	 *       whenever the changed property chain includes the ComponentData
	 *       array. This covers both direct field edits in the picker UI AND
	 *       struct-customization writes that go through `SetPerObjectValues`
	 *       (e.g. cover Generate Slots) — `SetPerObjectValues` builds a full
	 *       property chain from the inner struct up through ComponentData, so
	 *       the propagation is reached here without the customization calling
	 *       it directly. (`PropagateComponentDataEntryToInstances` is also
	 *       public for any future write that bypasses a ComponentData-bearing
	 *       chain and must trigger the mirror explicitly.) */
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;

	/** Copy `ComponentData[ChangedArrayIndex]` from this CDO bridge to every
	 *  placed-actor bridge that inherits from this CDO across all loaded
	 *  editor / preview worlds. Mark instance + level packages dirty and
	 *  redraw viewports.
	 *
	 *  Instances are enumerated via the engine's `GetArchetypeInstances`
	 *  (the same machinery UE's Details panel uses), called on the owner
	 *  actor CDO — a class-bucketed lookup, not an all-worlds/all-actors
	 *  scan — then filtered to Editor/EditorPreview worlds. Instances whose
	 *  entry already equals the CDO entry are skipped, so no spurious
	 *  Modify()/MarkPackageDirty() churn is generated for unchanged data.
	 *
	 *  No-op if this bridge isn't a CDO subobject (`IsTemplate() == false`)
	 *  — the propagation is one-way, CDO → instances. Per-instance
	 *  customizations of the same entry get clobbered; FInstancedStruct
	 *  delta tracking in UE 5.x doesn't preserve per-field nested overrides
	 *  reliably anyway, so we treat the CDO as authoritative for the
	 *  entire entry.
	 *
	 *  Public so a future struct customization that mutates an entry WITHOUT
	 *  producing a ComponentData-bearing property chain can trigger the mirror
	 *  explicitly. The shipped paths — direct picker edits and cover Generate
	 *  Slots (via `SetPerObjectValues`) — reach this through
	 *  `PostEditChangeChainProperty` and do NOT call it directly. */
	void PropagateComponentDataEntryToInstances(int32 ChangedArrayIndex);
#endif

	/** Set the entity handle this component represents.
	 *  Takes an initial transform snapshot from the entity. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS")
	void SetEntityHandle(FSeinEntityHandle InHandle);

	/** Get the entity handle this component represents. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS")
	FSeinEntityHandle GetEntityHandle() const { return EntityHandle; }

	/** Check if this component has a valid, living entity.
	 *  Validates both handle generation and entity pool liveness. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS")
	bool HasValidEntity() const;

	/** Enable/disable automatic transform synchronization.
	 *  When enabled, actor transform is updated each tick to match simulation. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Sync")
	void SetTransformSyncEnabled(bool bEnable);

	/** Check if transform sync is enabled. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Sync")
	bool IsTransformSyncEnabled() const { return bSyncTransform; }

	/** Called by the subsystem after each simulation tick to capture
	 *  transform snapshots for interpolation. Shifts CurrentSimTransform
	 *  into PreviousSimTransform, then reads the new current from the entity. */
	void OnSimTick();

	/** Handle a visual event dispatched from the simulation. Two outputs:
	 *    1. Routes the event to the owning ASeinActor's Receive* BlueprintImplementable
	 *       events (existing behavior — designers can react in BP without an AC).
	 *    2. Broadcasts `OnVisualEvent` (below) so render-side ACs subscribed to
	 *       this entity (USeinConstructionRenderComponent, designer BP render
	 *       components, etc.) receive the event without inheriting from a
	 *       SeinARTS base class. */
	void HandleVisualEvent(const FSeinVisualEvent& Event);

	/** Per-entity visual event delegate. Render-side ACs subscribe in BeginPlay
	 *  via `Bridge->OnVisualEvent.AddDynamic(this, &MyType::HandleVisualEvent)`
	 *  (C++) or `Bind Event to On Visual Event` (BP). Fires for every visual
	 *  event the bridge subsystem routes to this entity. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS")
	FOnSeinEntityVisualEvent OnVisualEvent;

protected:
	/** Generational entity handle this component represents */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS")
	FSeinEntityHandle EntityHandle;

	/** Whether to automatically sync actor transform to simulation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Sync")
	bool bSyncTransform = true;

	/** Whether to interpolate between sim tick snapshots for smooth visuals */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Sync")
	bool bInterpolateTransform = true;

private:
	/** Cached subsystem reference */
	UPROPERTY(Transient)
	TObjectPtr<USeinWorldSubsystem> CachedSubsystem;

	/** Transform snapshot from the previous simulation tick */
	FFixedTransform PreviousSimTransform;

	/** Transform snapshot from the most recent simulation tick */
	FFixedTransform CurrentSimTransform;

	/** Whether we have received at least one sim tick snapshot */
	bool bHasSimSnapshot = false;

	/** Get or lazily cache the simulation subsystem */
	USeinWorldSubsystem* GetSubsystem();

	/** Update actor transform from simulation state (with optional interpolation) */
	void SyncTransformToActor();
};

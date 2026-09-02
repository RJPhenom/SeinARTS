/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinEntityBridgeComponent.h
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
 *           live on USeinEntityBridgeComponent, and NOT run during BP
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
#include "Simulation/SeinComponentLiveTuning.h"
#include "StructUtils/InstancedStruct.h"
#include "Types/Transform.h"
#include "SeinEntityBridgeComponent.generated.h"

class FObjectPreSaveContext;
class USeinDataComponent;
class USeinWorldSubsystem;
struct FSeinVisualEvent;

/** Editor-only inheritance history for one Blueprint ComponentData property.
 * Stored on the class-default bridge so an unopened level can later decide
 * whether its serialized value was the old inherited default or an override. */
USTRUCT()
struct FSeinComponentDataDefaultChangeRecord
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Revision = 0;

	UPROPERTY()
	FSeinComponentPropertyPatch PreviousValue;

	UPROPERTY()
	FSeinComponentPropertyPatch NewValue;
};

/** Editor-only snapshot of one authored ComponentData entry: struct type path
 *  plus the entry's full exported value text. */
USTRUCT()
struct FSeinComponentDataEntrySnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	FString ComponentTypePath;

	UPROPERTY()
	FString ExportedValue;
};

/** Editor-only history record for one STRUCTURAL ComponentData change on a
 *  class default — entries added, removed, or retyped. Value-only edits use
 *  FSeinComponentDataDefaultChangeRecord (the property-patch lane); the two
 *  lanes share one revision counter and replay together in revision order.
 *  Full before/after snapshots are stored because structural adoption is
 *  wholesale: an instance still equal to Before (modulo its recorded property
 *  overrides) is rebuilt as After with those overrides re-applied. */
USTRUCT()
struct FSeinComponentDataStructuralChangeRecord
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Revision = 0;

	UPROPERTY()
	TArray<FSeinComponentDataEntrySnapshot> Before;

	UPROPERTY()
	TArray<FSeinComponentDataEntrySnapshot> After;
};

/** One problem found while validating an authored ComponentData array — see
 *  `USeinEntityBridgeComponent::ValidateComponentData`. Never serialized; a plain
 *  C++ struct is sufficient since it only carries editor/validation-time
 *  diagnostics between call sites in the same process. */
struct FSeinComponentDataIssue
{
	/** Index into the ComponentData array this issue applies to. */
	int32 EntryIndex = INDEX_NONE;

	/** Human-readable reason, e.g. "ComponentData[3] has no resolvable
	 *  struct type ...". Written so it reads naturally whether appended to
	 *  a runtime warning, a Blueprint compiler error, or a bootstrap
	 *  failure string. */
	FString Description;
};

/** Render-only ray-tracing geometry policy for one simulation-backed actor.
 *  Large RTS crowds should not put every skinned unit mesh into the hardware
 *  ray-tracing geometry pool: animation forces expensive BLAS updates and the
 *  aggregate geometry can exhaust both UE's RT pool and GPU-local memory.
 *  This policy never enters deterministic state and does not affect gameplay. */
UENUM(BlueprintType)
enum class ESeinRayTracingGeometryPolicy : uint8
{
	/** Preserve every primitive component's authored Unreal setting. */
	ComponentDefaults UMETA(DisplayName = "Component Defaults"),

	/** Exclude skinned meshes while preserving static/other primitives. This is
	 *  the crowd-safe default; unit actors remain visible in the raster passes. */
	ExcludeSkinnedMeshes UMETA(DisplayName = "Exclude Skinned Meshes"),

	/** Exclude every primitive owned by this actor from ray-tracing geometry. */
	ExcludeAllPrimitives UMETA(DisplayName = "Exclude All Primitives"),
};

/** Render-only skeletal-mesh update policy for simulation-backed actors.
 *  SeinARTS transforms, collision, targeting, and lockstep state never depend
 *  on Unreal skeletal physics, so ordinary RTS unit meshes can use the
 *  engine's crowd update-rate path and avoid rebuilding kinematic physics
 *  bodies for every animated bone. Units that deliberately use partial
 *  ragdolls, physical animation, or bone-driven UE overlap queries retain a
 *  separate policy. This policy never enters deterministic state. */
UENUM(BlueprintType)
enum class ESeinSkeletalMeshPerformancePolicy : uint8
{
	/** Preserve all skeletal-mesh component settings authored in Unreal. */
	ComponentDefaults UMETA(DisplayName = "Component Defaults"),

	/** Standard sim-authoritative RTS unit. Enables Unreal's animation
	 *  update-rate optimization, stops full AnimBP work when not rendered,
	 *  and skips animation-to-physics bone copies and overlap refreshes. */
	RTSVisualMesh UMETA(DisplayName = "RTS Visual Mesh"),

	/** Retains authored kinematic-bone and overlap behavior for units whose
	 *  visual mesh participates in partial ragdolls or UE-side physics, while
	 *  still enabling update-rate and offscreen animation optimization. */
	RTSPhysicsMesh UMETA(DisplayName = "RTS Physics Mesh"),
};

/** Per-entity multicast delegate fired by `USeinEntityBridgeComponent::HandleVisualEvent`
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
class SEINARTSCOREENTITY_API USeinEntityBridgeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USeinEntityBridgeComponent();

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
	 *  EntityTagStates table (slot-indexed FSeinEntityTagStateTable); this
	 *  UPROPERTY is the seed it reads at spawn. */
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
	 *    VisibleOnceSeen            — visible once a source has actually
	 *      spotted THIS ENTITY; ghost-revealed forever after. Something built
	 *      in already-explored-but-unseen fog stays hidden until seen.
	 *      Standard for enemy buildings.
	 *    VisibleOnceExplored        — visible once the entity's CELL is
	 *      explored, even if the entity was never seen; coarser terrain-scout
	 *      reveal.
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

	/** Validate a ComponentData array for authoring correctness: every entry
	 *  must resolve to a real struct type (an empty picker row, or a struct
	 *  asset later deleted/renamed, does not), and no two entries may author
	 *  the same struct type. Returns true iff OutIssues is left empty.
	 *
	 *  This is the SINGLE shared definition of "valid ComponentData" — used
	 *  by the Blueprint pre-compile gate (SeinARTSEditorModule's
	 *  OnBlueprintPreCompile hook), match bootstrap
	 *  (FSeinMatchBootstrapTransaction::ValidateEntityComponentData), and
	 *  this component's own runtime InjectAuthoredComponents. All three call
	 *  through here so they can never silently disagree about what is
	 *  broken; only what happens on failure (warn at compile, abort the
	 *  match, or skip-and-warn a live spawn) differs per call site.
	 *
	 *  The compile gate reads the CDO of Blueprint->GeneratedClass, which at
	 *  OnBlueprintPreCompile time is still the class from the LAST compile —
	 *  edits made through the property editor land on that same live CDO
	 *  directly (independent of compilation), so ordinary ComponentData
	 *  value edits are always caught on the very next compile. The one gap:
	 *  a USeinEntityBridgeComponent added for the first time via the SCS in this
	 *  same edit session isn't on that old CDO yet, so a bad entry on it
	 *  slips through THIS compile and is caught on the next one instead —
	 *  moot for ASeinActor units, where the entity bridge is already a
	 *  native default subobject before any authoring happens. Match
	 *  bootstrap remains the authoritative backstop regardless. */
	static bool ValidateComponentData(
		const TArray<FInstancedStruct>& ComponentData,
		TArray<FSeinComponentDataIssue>& OutIssues);

#if WITH_EDITORONLY_DATA
	/** True when this placed instance's ComponentData deliberately (or, for
	 *  pre-history content, irrecoverably) differs in SHAPE from its class
	 *  default. Structural adoption skips such instances; Map Check
	 *  (ASeinActor::CheckForErrors) surfaces them with the repair action. */
	bool HasStructuralComponentDataOverride() const
	{
		return bComponentDataStructuralOverride;
	}
#endif

	/** Iterate ComponentData and inject every entry into the world subsystem's
	 *  component storage for `Handle`. Duplicate authored struct types are an
	 *  authoring error; the deterministic later-entry value wins with a warning. */
	void InjectAuthoredComponents(USeinWorldSubsystem& World, FSeinEntityHandle Handle) const;

	// =========================================================================
	// Bridge runtime (carried over from the previous USeinActorBridge role)
	// =========================================================================

	// UActorComponent interface
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	/** Strip the class default's inherited history copy off non-template objects
	 *  so it is never serialized into placed actors. */
	virtual void PostInitProperties() override;

	/** Instances: establish override metadata and adopt class-default changes
	 *  that happened while this level was unopened. Class defaults: adopt
	 *  parent-class default changes that happened while this Blueprint was
	 *  unopened (same value-delta rule Unreal applies to ordinary defaults). */
	virtual void PostLoad() override;

	/** Capture the old authored payload so nested FInstancedStruct edits can use
	 *  Unreal's ordinary old-default comparison at PostEdit time. */
	virtual void PreEditChange(FEditPropertyChain& PropertyAboutToChange) override;

	/** Watches manual edits to ComponentData entries.
	 *
	 *  Two jobs:
	 *    1. When a designer edits the `IdentityTag` field inside an
	 *       FSeinIdentityComponent entry, flip the matching
	 *       `bAutoGeneratedTag` flag to false so the auto-tag system knows
	 *       the designer owns it.
	 *    2. Run the ComponentData authoring pipeline (HandleComponentDataEdited):
	 *       on a class default, record history, mirror the change into loaded
	 *       instances and derived class defaults that still inherit the value,
	 *       and broadcast the class-scoped live-tuning request; on an instance,
	 *       maintain the override set and broadcast the entity-scoped request
	 *       while PIE is running. This covers both direct field edits in the
	 *       picker UI AND struct-customization writes that go through
	 *       `SetPerObjectValues` (e.g. cover Generate Slots), which build a full
	 *       property chain from the inner struct up through ComponentData. */
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;

	/** Bracket an out-of-band ComponentData write that bypasses the property
	 *  editor (no PreEditChange / PostEditChangeChainProperty), e.g. a tool
	 *  writing values straight into the entries. Begin snapshots the authored
	 *  payload; End runs exactly the pipeline a Details-panel edit would:
	 *  class-default history, instance/derived-class propagation with Unreal's
	 *  two-layer inherit/override semantics, and the live-tuning broadcast. */
	void BeginComponentDataEdit();
	void EndComponentDataEdit();

	// =========================================================================
	// AC-authoring prototype: bake USeinDataComponent payloads into
	// ComponentData (the array stays the injected runtime carrier).
	// =========================================================================

	/** One authoring data component was edited. Bakes ONLY that component's
	 *  payload into ComponentData through the bracketed edit pipeline (which
	 *  supplies instance-override bookkeeping in editor worlds, the
	 *  entity-scoped live-tuning command in PIE, and class history/propagation
	 *  on class defaults). Single-component on purpose: a full re-bake here
	 *  can pin OTHER components' mid-transition values as instance overrides
	 *  the designer never authored. */
	void NotifyAuthoringComponentEdited(const USeinDataComponent& Component);

	/** True when this instance's entry-type shape differs from its class
	 *  default only in ways its own authoring components explain (e.g. a
	 *  per-instance Injection Enabled toggle). Map Check suppresses the
	 *  stale-override nag for explained shapes — the divergence is intent. */
	bool IsComponentDataShapeExplainedByAuthoring() const;

	/** Bake every enabled USeinDataComponent on the owner into ComponentData.
	 *  Entries are managed per payload type: upserted while a component for
	 *  the type is present and enabled, removed when it is disabled or the
	 *  component is gone (tracked via the baked-types ledger). bInteractive
	 *  runs the bake through the Begin/End edit bracket so history,
	 *  propagation, and live tuning fire (compile hooks, instance edits);
	 *  non-interactive is a silent value refresh for PreSave/cook. */
	void BakeAuthoredDataComponents(bool bInteractive);

	/** PreSave belt: refresh the baked entries (silently) so saved and cooked
	 *  packages always carry the authoring components' current values. */
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;
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

	/** Apply RayTracingGeometryPolicy to the owner's current primitive
	 *  components. ASeinActor applies it after component registration in editor
	 *  and runtime worlds, then BeginPlay reapplies it for runtime safety. Call
	 *  again after adding render components dynamically. Render-only and
	 *  lockstep-safe. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Rendering",
		meta = (DisplayName = "Apply Ray Tracing Geometry Policy"))
	void ApplyRayTracingGeometryPolicy();

	/** Apply SkeletalMeshPerformancePolicy to the owner's current skeletal mesh
	 *  components. ASeinActor applies it after component registration in editor
	 *  and runtime worlds, then BeginPlay reapplies it for runtime safety. Call
	 *  again after adding skeletal components dynamically or changing policy. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Rendering",
		meta = (DisplayName = "Apply Skeletal Mesh Performance Policy"))
	void ApplySkeletalMeshPerformancePolicy();

	/** Called after an engine-frame simulation pump to capture the latest
	 *  transform. A single completed tick advances the interpolation pair.
	 *  Catch-up pumps snap both snapshots to the latest state because the
	 *  render alpha represents only one fixed-tick interval. */
	void OnSimFrame(int32 TicksProcessed);

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

	/** Controls whether this entity contributes primitive geometry to hardware
	 *  ray tracing. The crowd-safe default excludes animated/skinned meshes,
	 *  avoiding per-unit BLAS memory and update cost while retaining ordinary
	 *  raster visibility, shadows, and static ray-traced scene geometry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Rendering")
	ESeinRayTracingGeometryPolicy RayTracingGeometryPolicy =
		ESeinRayTracingGeometryPolicy::ExcludeSkinnedMeshes;

	/** Controls native Unreal animation/physics work for the render meshes on
	 *  this sim-backed actor. RTS Visual Mesh is the crowd-safe default: the
	 *  deterministic sim remains authoritative for collision and gameplay,
	 *  while animation stays a scalable presentation concern. Select RTS
	 *  Physics Mesh for partial ragdolls/physical animation, or Component
	 *  Defaults when a Blueprint owns the complete policy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Rendering")
	ESeinSkeletalMeshPerformancePolicy SkeletalMeshPerformancePolicy =
		ESeinSkeletalMeshPerformancePolicy::RTSVisualMesh;

private:
#if WITH_EDITORONLY_DATA
	/** Stable property keys explicitly overridden on this actor instance. This is
	 *  the internal delta metadata needed because ComponentData is one reflected
	 *  TArray even though designers edit the fields inside its instanced structs.
	 *  It preserves Unreal's class-default-under-instance-override behavior when
	 *  an unloaded level is opened after a Blueprint default changed. */
	UPROPERTY()
	TArray<FString> ComponentDataPropertyOverrides;

	/** One-time legacy migration marker. Existing instances conservatively adopt
	 *  every current CDO difference as an override so an editor upgrade never
	 *  destroys a designer-authored value. */
	UPROPERTY()
	bool bComponentDataOverrideMetadataInitialized = false;

	/** Monotonic history source revision. Meaningful only on the class default;
	 * instances never consult their inherited copy of this field. */
	UPROPERTY()
	int32 ComponentDataDefaultRevision = 0;

	/** Latest class-default revision this instance has reconciled. Kept separate
	 * from the CDO source revision so ordinary archetype propagation cannot make
	 * an unopened instance appear caught up before its values are examined. */
	UPROPERTY()
	int32 ComponentDataInheritedDefaultRevision = 0;

	/** CDO-owned property history. Editor-only and intentionally retained so
	 * level packages need not all be loaded when a Blueprint default changes. */
	UPROPERTY()
	TArray<FSeinComponentDataDefaultChangeRecord>
		ComponentDataDefaultChangeHistory;

	/** CDO-owned structural history (entries added/removed/retyped), replayed
	 * together with the property history in shared-revision order. Retained for
	 * the same unopened-level reason. Snapshots are bounded per entry; an edit
	 * whose snapshot cannot be captured is warned about and not recorded. */
	UPROPERTY()
	TArray<FSeinComponentDataStructuralChangeRecord>
		ComponentDataStructuralChangeHistory;

	/** Instance-owned: this placed actor's ComponentData deliberately (or, for
	 * pre-history content, irrecoverably) differs in SHAPE from its class
	 * default — entry set mismatch that no recorded transition explains. While
	 * set, structural adoption skips this instance and reconciliation stays
	 * quiet; reverting the Component Data property to the class default clears
	 * it. Surfaced by ASeinActor::CheckForErrors in Map Check. */
	UPROPERTY()
	bool bComponentDataStructuralOverride = false;

	/** AC-authoring prototype: payload type paths currently managed by the
	 * authoring-component bake. Lets the bake remove an entry whose authoring
	 * component was deleted or disabled without touching hand-authored
	 * entries of other types. */
	UPROPERTY()
	TArray<FString> BakedComponentDataTypes;
#endif

#if WITH_EDITOR
	/** Transaction-local old value for one ComponentData edit. Never serialized. */
	TArray<FInstancedStruct> ComponentDataBeforeEditorChange;
	bool bCapturedComponentDataBeforeEditorChange = false;

	void EnsureComponentDataOverrideMetadataInitialized();
	/** Class-default structural edit (entries added/removed/retyped): record a
	 *  structural history transition and adopt it into every loaded instance and
	 *  derived class default still equal to the old default (modulo recorded
	 *  property overrides). PIE actor instances are skipped — a live match's
	 *  bridge topology must stay in step with its injected sim storage — and
	 *  reconcile on their next editor load. */
	void HandleStructuralClassDefaultEdit();
	/** Outcome of replaying one structural record against this bridge. */
	enum class EStructuralAdoptOutcome : uint8
	{
		Adopted,        // shape matched Before — rebuilt as After, value overrides preserved
		AlreadyCurrent, // shape already matches After — nothing to do
		Flagged,        // shape matches neither — structural divergence
		Skipped         // snapshot unusable (deleted struct, bound) — left untouched
	};
	/** Replay one structural record. Shape (entry-type multiset) equal to
	 *  Before → rebuild as After, preserving EVERY value difference from Before
	 *  (a value diff from the old default IS an override, whether recorded or
	 *  legacy — same philosophy as the property lane's else-override rule); on
	 *  instances the override ledger is rebuilt to exactly the re-applied keys.
	 *  Shape equal to After → nothing to do. Anything else → Flagged; the
	 *  caller decides layer semantics (instances set the structural-override
	 *  flag; a derived class default's divergence IS its override, quietly). */
	EStructuralAdoptOutcome TryAdoptStructuralRecord(
		const FSeinComponentDataStructuralChangeRecord& Record);
	/** Snapshot BeforeEntries -> current ComponentData into this class
	 *  default's own structural history under a fresh revision. False (with a
	 *  warning) when a snapshot exceeds its bound or cannot be captured. */
	bool RecordStructuralChangeToOwnHistory(
		const TArray<FInstancedStruct>& BeforeEntries);
	/** Clear/set bComponentDataStructuralOverride against the current class
	 *  default after an instance edit or refresh; returns true when it changed. */
	bool ReconcileStructuralOverrideFlag();
	/** Append one revision of (old -> new) records to this class default's
	 *  history. Before is the authored payload prior to the change. */
	void RecordComponentDataClassDefaultChange(
		const TArray<FInstancedStruct>& Before,
		const TArray<FSeinComponentPropertyPatch>& NewPatches);
	/** Walk the inherited class default's history past this object's cursor:
	 *  a value still equal to the old default adopts the new one; on instances
	 *  anything else becomes an override; on derived class defaults adopted
	 *  transitions are re-recorded into this class's own history. */
	void CatchUpComponentDataClassDefaultHistory();
	void RefreshInheritedComponentDataFromClassDefaults();
	/** Shared tail of PostEditChangeChainProperty and EndComponentDataEdit. */
	void HandleComponentDataEdited();

	/** Mirror the captured property-level differences from this class-default
	 *  bridge into every loaded object that inherits from it: placed/preview/PIE
	 *  instances of this class, derived Blueprint class defaults, and instances
	 *  of derived classes (engine `GetArchetypeInstances` on the owner actor CDO,
	 *  filtered by archetype chain). A leaf is written only where it still equals
	 *  the old class default, so instance overrides and derived-class overrides
	 *  survive. Derived class defaults that received a value are recorded into
	 *  their own history. Every derived class default that was reached is
	 *  returned in OutDerivedClassEntries with explicit per-key values (the
	 *  new value where it inherited, a pin of its own value where it
	 *  overrides) so the sim holds an exact record for each observable class. */
	void PropagateComponentDataChangesToInstances(
		const TArray<FSeinComponentPropertyPatch>& Patches,
		TArray<FSeinComponentLiveTuningClassEntry>& OutDerivedClassEntries);
#endif

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

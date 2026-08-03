/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinEntityComponent.cpp
 * @date:    2/28/2026 (originally SeinActorBridge.cpp; renamed 2026-05-18)
 * @author:  RJ Macklem
 * @brief:   Implementation of the unified entity component â€” owns the
 *           entity handle, syncs transforms, forwards visual events,
 *           AND injects the authored ComponentData array into deterministic
 *           sim storage at spawn.
 */

#include "Actor/SeinEntityComponent.h"

#include "Actor/SeinActor.h"
#include "Components/SeinFogVisibilityComponent.h"   // auto-injected from bridge top-level fields
#include "Components/SeinIdentityComponent.h"        // ComponentData entry edit-watching
#include "Simulation/ComponentStorage.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Events/SeinVisualEvent.h"
#include "Types/Entity.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"

#if WITH_EDITOR
#include "Editor.h"                       // GEditor for viewport redraw
#include "Engine/World.h"                 // UWorld / EWorldType (per-instance world filter)
#include "GameFramework/Actor.h"          // AActor / GetComponents
#endif

// NOTE: editor-preview code (in-editor squad-slot mesh previews) was removed
// 2026-05-20 â€” see header docblock for the rationale. If a future preview is
// added, it MUST live outside this component to avoid the BP-reinstantiation
// root-component-swap class of bug we tripped on.

#include "SeinARTSCoreEntityLog.h"  // LogSeinBridge, LogSeinSim (module-shared)
DEFINE_LOG_CATEGORY_STATIC(LogSeinEntityComp, Log, All);

USeinEntityComponent::USeinEntityComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	bSyncTransform = true;
	bInterpolateTransform = true;
	CachedSubsystem = nullptr;
}

// =============================================================================
// Authoring â€” ComponentData array injection
// =============================================================================

void USeinEntityComponent::InjectAuthoredComponents(USeinWorldSubsystem& World, FSeinEntityHandle Handle) const
{
	// One pass through the authored array. Each entry should be a
	// `FSeinComponent` substruct (the editor's `BaseStruct` filter enforces
	// this on the picker); we guard against an empty array entry (a designer
	// might add a row and not pick a type yet).
	//
	// One simulation storage exists per struct type. A duplicate authored entry
	// would otherwise silently overwrite the earlier value, so retain the
	// deterministic last-entry-wins behavior but surface the authoring error.
	for (const FInstancedStruct& Entry : ComponentData)
	{
		if (!Entry.IsValid())
		{
			UE_LOG(LogSeinEntityComp, Verbose,
				TEXT("Skipping invalid ComponentData entry on %s â€” empty struct picker?"),
				*GetNameSafe(GetOwner()));
			continue;
		}

		UScriptStruct* StructType = const_cast<UScriptStruct*>(Entry.GetScriptStruct());
		if (!StructType) continue;

		ISeinComponentStorage* Storage = World.GetOrCreateStorageForType(StructType);
		if (!Storage) continue;

		const bool bAlreadyInjected = (Storage->GetComponentRaw(Handle) != nullptr);
		if (bAlreadyInjected)
		{
			UE_LOG(LogSeinEntityComp, Warning,
				TEXT("Entity %s has duplicate authored component type %s; "
					 "the later USeinEntityComponent entry will overwrite the earlier value. "
					 "Remove the duplicate entry from %s."),
				*Handle.ToString(), *StructType->GetName(), *GetNameSafe(GetOwner()));
			// Storage->AddComponent overwrites; intentional.
		}

		Storage->AddComponent(Handle, Entry.GetMemory());
	}

	// Auto-inject the universal FSeinFogVisibilityComponent from the bridge's
	// top-level fields. Authoring lives on USeinEntityComponent directly
	// (FogVisibilityPolicy + FogVisibilityLayerMask) â€” same pattern as
	// BaseTags. The sim-side struct is just the storage mirror so FoW code
	// can read it without touching the actor.
	//
	// Always injected â€” every entity has a visibility policy whether the
	// designer authored non-default values or not. Defaults from the bridge
	// fields' UPROPERTY initializers carry through.
	{
		FSeinFogVisibilityComponent FogVis;
		FogVis.FogVisibilityPolicy = FogVisibilityPolicy;
		FogVis.FogVisibilityLayerMask = FogVisibilityLayerMask;
		ISeinComponentStorage* FogVisStorage = World.GetOrCreateStorageForType(FSeinFogVisibilityComponent::StaticStruct());
		if (FogVisStorage)
		{
			FogVisStorage->AddComponent(Handle, &FogVis);
		}
	}
}

// =============================================================================
// Bridge runtime â€” transform sync, visual event forwarding
// =============================================================================

void USeinEntityComponent::BeginPlay()
{
	Super::BeginPlay();

	// Cache subsystem reference early
	GetSubsystem();
	ApplyRayTracingGeometryPolicy();
	ApplySkeletalMeshPerformancePolicy();
	SetComponentTickEnabled(bSyncTransform);
}

void USeinEntityComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bSyncTransform && EntityHandle.IsValid())
	{
		SyncTransformToActor();
	}
}

#if WITH_EDITOR
void USeinEntityComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	// Resolve which ComponentData array entry (if any) was edited via the
	// property chain. UE's chain walks parent→child; we look for the array
	// index of the ComponentData entry being mutated. Used by both:
	//   1. Identity-tag bAutoGeneratedTag surrender (below)
	//   2. CDO-to-placed-instance propagation (below)
	const FProperty* ArrayProp = nullptr;
	int32 ArrayIndex = INDEX_NONE;
	for (auto Node = PropertyChangedEvent.PropertyChain.GetHead(); Node; Node = Node->GetNextNode())
	{
		const FProperty* Prop = Node->GetValue();
		if (Prop && Prop->GetFName() == GET_MEMBER_NAME_CHECKED(USeinEntityComponent, ComponentData))
		{
			ArrayProp = Prop;
			ArrayIndex = PropertyChangedEvent.GetArrayIndex(Prop->GetFName().ToString());
			break;
		}
	}

	// Watch for designer edits to `FSeinIdentityComponent::IdentityTag` inside
	// the ComponentData array. When the property chain ends at IdentityTag,
	// we flip the matching entry's `bAutoGeneratedTag = false` so the auto-
	// tag-generation system surrenders ownership.
	const FName ChangedLeaf = PropertyChangedEvent.Property
		? PropertyChangedEvent.Property->GetFName()
		: NAME_None;
	if (ChangedLeaf == GET_MEMBER_NAME_CHECKED(FSeinIdentityComponent, IdentityTag)
		&& ArrayIndex != INDEX_NONE && ComponentData.IsValidIndex(ArrayIndex))
	{
		FInstancedStruct& Entry = ComponentData[ArrayIndex];
		if (Entry.IsValid() && Entry.GetScriptStruct() == FSeinIdentityComponent::StaticStruct())
		{
			FSeinIdentityComponent& Identity = Entry.GetMutable<FSeinIdentityComponent>();
			Identity.bAutoGeneratedTag = false;
		}
	}

	// CDO → placed-instance propagation for ComponentData edits.
	//
	// UE's automatic archetype propagation is unreliable for nested
	// `FInstancedStruct`-inside-array properties on SCS components — the
	// property walker doesn't descend through the InstancedStruct's opaque
	// storage. Without this, editing a BP-CDO entry (e.g. clicking Generate
	// Slots on a cover component, or hand-tweaking a slot position) leaves
	// every actor already placed in a level frozen with their old data,
	// and the only way to pick up the change is to delete + re-place the
	// actor. We instead mirror the changed entry into each loaded instance
	// of this BP class manually.
	//
	// Scope: only fires when this bridge is a CDO subobject (designer
	// editing the BP class, not editing a level-placed instance directly).
	// Per-instance customizations get clobbered by the CDO mirror — that's
	// the intended behavior; FInstancedStruct entry delta tracking is
	// effectively all-or-nothing in UE 5.x and per-instance authoring of
	// individual nested fields isn't reliably preserved by the engine
	// anyway.
	if (ArrayIndex != INDEX_NONE && ComponentData.IsValidIndex(ArrayIndex) && IsTemplate())
	{
		PropagateComponentDataEntryToInstances(ArrayIndex);
	}
}

void USeinEntityComponent::PropagateComponentDataEntryToInstances(int32 ChangedArrayIndex)
{
	if (!ComponentData.IsValidIndex(ChangedArrayIndex)) return;
	if (!IsTemplate()) return;

	// Enumerate placed instances via the engine's archetype-instance machinery
	// rather than scanning every actor in every loaded editor world. This is the
	// same path UE's own Details-panel value propagation uses
	// (FPropertyNode::PropagatePropertyChange → UObject::GetArchetypeInstances):
	// a class-bucketed lookup (UObjectHash ClassToObjectListMap) instead of an
	// O(worlds × all-actors) sweep. We call it on the OWNER ACTOR CDO — which is
	// guaranteed RF_ClassDefaultObject (GetArchetypeInstances keys off the flag
	// on the object itself) — and pull the bridge off each returned instance.
	// This also reaches bridges on subclass-actor owners that the previous
	// TActorIterator<OwnerActorClass> filter could miss, while the per-instance
	// archetype check below keeps the propagation scoped to exactly this CDO.
	AActor* OwnerActorCDO = GetTypedOuter<AActor>();
	if (!OwnerActorCDO) return;
	UClass* OwnerActorClass = OwnerActorCDO->GetClass();

	const FInstancedStruct& CDOEntry = ComponentData[ChangedArrayIndex];

	TArray<UObject*> ArchetypeInstances;
	OwnerActorCDO->GetArchetypeInstances(ArchetypeInstances);

	int32 InstancesUpdated = 0;
	int32 InstancesScanned = 0;
	TSet<UPackage*> DirtiedPackages;

	for (UObject* InstanceObj : ArchetypeInstances)
	{
		AActor* Instance = Cast<AActor>(InstanceObj);
		if (!Instance) continue;

		// Editor-authoring only. GetArchetypeInstances is world-agnostic, so we
		// apply the old Editor + EditorPreview filter per-instance here. PIE /
		// Game instances are skipped (propagation is an authoring concern, not a
		// runtime sync), and CDOs / archetypes — which have a null world — drop
		// out too.
		const UWorld* InstanceWorld = Instance->GetWorld();
		if (!InstanceWorld) continue;
		const EWorldType::Type WT = InstanceWorld->WorldType;
		if (WT != EWorldType::Editor && WT != EWorldType::EditorPreview) continue;

		// Find the placed instance's bridge component. Most BPs have exactly one
		// USeinEntityComponent — we walk all just in case (defensive, no real
		// cost since the count is small).
		TArray<USeinEntityComponent*> InstanceBridges;
		Instance->GetComponents<USeinEntityComponent>(InstanceBridges);
		for (USeinEntityComponent* InstBridge : InstanceBridges)
		{
			if (!InstBridge || InstBridge == this) continue;
			// Match by archetype — only propagate to bridges that inherit from
			// THIS specific CDO (not unrelated bridges, and not bridges belonging
			// to a subclass BP whose archetype is its own CDO bridge).
			if (InstBridge->GetArchetype() != this) continue;
			if (!InstBridge->ComponentData.IsValidIndex(ChangedArrayIndex)) continue;
			++InstancesScanned;

			// Skip instances already equal to the CDO entry. Without this, an
			// unconditional Modify() records a spurious undo snapshot and
			// MarkPackageDirty() flags packages that didn't actually change — the
			// over-dirtying that made CDO edits slow to save. The engine's own
			// propagation likewise only writes + dirties on a real value change
			// (FInstancedStruct::operator== is type- and null-safe). Instances
			// that genuinely differ are still clobbered: the CDO stays
			// authoritative for the whole entry, exactly as before.
			if (InstBridge->ComponentData[ChangedArrayIndex] == CDOEntry) continue;

			InstBridge->Modify();
			InstBridge->ComponentData[ChangedArrayIndex] = CDOEntry;

			if (UPackage* Pkg = InstBridge->GetPackage())
			{
				Pkg->MarkPackageDirty();
				DirtiedPackages.Add(Pkg);
			}
			if (UPackage* APkg = Instance->GetPackage())
			{
				APkg->MarkPackageDirty();
				DirtiedPackages.Add(APkg);
			}
			++InstancesUpdated;
		}
	}

	UE_LOG(LogSeinEntityComp, Log,
		TEXT("[PropagateComponentDataEntry] CDO=%s, Class=%s, EntryIdx=%d: scanned=%d, updated=%d, dirtied %d package(s)."),
		*GetName(), *GetNameSafe(OwnerActorClass), ChangedArrayIndex,
		InstancesScanned, InstancesUpdated, DirtiedPackages.Num());

	if (InstancesUpdated > 0 && GEditor)
	{
		// Push the change to the level viewport so designers see the update
		// without clicking off + back on the actor.
		GEditor->RedrawAllViewports(/*bInvalidateHitProxies*/ true);
	}
}
#endif

void USeinEntityComponent::SetEntityHandle(FSeinEntityHandle InHandle)
{
	EntityHandle = InHandle;

	if (EntityHandle.IsValid())
	{
		// Verbose: fires per-actor on map travel; spammy at Log level.
		// Re-enable with `Log LogSeinSim Verbose` when diagnosing
		// bridge-to-entity binding races.
		UE_LOG(LogSeinSim, Verbose, TEXT("SeinEntityComponent linked to entity %s"), *EntityHandle.ToString());

		// Take initial transform snapshot so interpolation has valid data from frame one
		USeinWorldSubsystem* Subsystem = GetSubsystem();
		if (Subsystem)
		{
			const FSeinEntity* Entity = Subsystem->GetEntity(EntityHandle);
			if (Entity)
			{
				CurrentSimTransform = Entity->Transform;
				PreviousSimTransform = Entity->Transform;
				bHasSimSnapshot = true;
			}
		}
	}
}

bool USeinEntityComponent::HasValidEntity() const
{
	if (!EntityHandle.IsValid())
	{
		return false;
	}

	USeinWorldSubsystem* Subsystem = const_cast<USeinEntityComponent*>(this)->GetSubsystem();
	if (!Subsystem)
	{
		return false;
	}

	return Subsystem->IsEntityAlive(EntityHandle);
}

void USeinEntityComponent::SetTransformSyncEnabled(bool bEnable)
{
	bSyncTransform = bEnable;
	SetComponentTickEnabled(bEnable);
}

void USeinEntityComponent::ApplyRayTracingGeometryPolicy()
{
	if (RayTracingGeometryPolicy == ESeinRayTracingGeometryPolicy::ComponentDefaults)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Owner->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	for (UPrimitiveComponent* Primitive : PrimitiveComponents)
	{
		if (!Primitive)
		{
			continue;
		}

		const bool bExclude =
			RayTracingGeometryPolicy == ESeinRayTracingGeometryPolicy::ExcludeAllPrimitives
			|| Primitive->IsA<USkinnedMeshComponent>();
		if (bExclude)
		{
			Primitive->SetVisibleInRayTracing(false);
		}
	}
}

void USeinEntityComponent::ApplySkeletalMeshPerformancePolicy()
{
	if (SkeletalMeshPerformancePolicy ==
		ESeinSkeletalMeshPerformancePolicy::ComponentDefaults)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	TArray<USkeletalMeshComponent*> SkeletalMeshes;
	Owner->GetComponents<USkeletalMeshComponent>(SkeletalMeshes);
	for (USkeletalMeshComponent* SkeletalMesh : SkeletalMeshes)
	{
		if (!SkeletalMesh)
		{
			continue;
		}

		// URO chooses an evaluation cadence from visibility and screen size,
		// interpolating skipped frames. The two skip flags avoid paying the
		// expensive physics/bounds copies on those interpolation-only frames.
		SkeletalMesh->bEnableUpdateRateOptimizations = true;
		SkeletalMesh->bSkipKinematicUpdateWhenInterpolating = true;
		SkeletalMesh->bSkipBoundsUpdateWhenInterpolating = true;

		// Preserve montage timing when hidden (attacks/deaths may use montages),
		// but do not evaluate the complete AnimBP graph or refresh bones until
		// the mesh is rendered again.
		SkeletalMesh->VisibilityBasedAnimTickOption =
			EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered;

		if (SkeletalMeshPerformancePolicy ==
			ESeinSkeletalMeshPerformancePolicy::RTSVisualMesh)
		{
			// Gameplay collision lives in deterministic extents, not in an
			// animated UE physics asset. Avoid copying every animated bone into
			// Chaos and recomputing overlaps for purely visual unit meshes.
			SkeletalMesh->KinematicBonesUpdateType =
				EKinematicBonesUpdateToPhysics::SkipAllBones;
			SkeletalMesh->bUpdateOverlapsOnAnimationFinalize = false;
		}
	}
}

void USeinEntityComponent::OnSimFrame(int32 TicksProcessed)
{
	USeinWorldSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem || !EntityHandle.IsValid())
	{
		return;
	}

	const FSeinEntity* Entity = Subsystem->GetEntity(EntityHandle);
	if (!Entity)
	{
		return;
	}

	// The render interpolation alpha is the residual fraction of one fixed
	// tick. Preserve normal adjacent snapshots for the common single-tick
	// frame, but snap a coalesced catch-up pump instead of interpolating across
	// several ticks with a one-tick alpha (which produces visual lag/smearing).
	if (TicksProcessed > 1 || !bHasSimSnapshot)
	{
		PreviousSimTransform = Entity->Transform;
		CurrentSimTransform = Entity->Transform;
	}
	else
	{
		PreviousSimTransform = CurrentSimTransform;
		CurrentSimTransform = Entity->Transform;
	}
	bHasSimSnapshot = true;

	UE_LOG(LogSeinBridge, Verbose,
		TEXT("Snapshot %s: entityRot=(x=%.3f y=%.3f z=%.3f w=%.3f) capturedRot=(x=%.3f y=%.3f z=%.3f w=%.3f)"),
		*GetOwner()->GetName(),
		Entity->Transform.Rotation.X.ToFloat(), Entity->Transform.Rotation.Y.ToFloat(),
		Entity->Transform.Rotation.Z.ToFloat(), Entity->Transform.Rotation.W.ToFloat(),
		CurrentSimTransform.Rotation.X.ToFloat(), CurrentSimTransform.Rotation.Y.ToFloat(),
		CurrentSimTransform.Rotation.Z.ToFloat(), CurrentSimTransform.Rotation.W.ToFloat());
}

void USeinEntityComponent::HandleVisualEvent(const FSeinVisualEvent& Event)
{
	// Broadcast to subscribed render-side ACs FIRST so they observe the same
	// event ordering the ASeinActor's BP events see. Listeners that need to
	// respond before the actor's BP graph runs (e.g. construction visual swap)
	// land here. Fired even if the owner isn't an ASeinActor â€” non-SeinActor
	// owners can still host render components subscribed to this delegate.
	OnVisualEvent.Broadcast(Event);

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	ASeinActor* SeinActor = Cast<ASeinActor>(Owner);
	if (!SeinActor)
	{
		return;
	}

	switch (Event.Type)
	{
	case ESeinVisualEventType::DamageApplied:
		SeinActor->ReceiveDamageApplied(Event.Value, Event.SecondaryEntity, Event.Tag);
		break;

	case ESeinVisualEventType::HealApplied:
		SeinActor->ReceiveHealApplied(Event.Value, Event.SecondaryEntity, Event.Tag);
		break;

	case ESeinVisualEventType::Kill:
		SeinActor->ReceiveKill(Event.SecondaryEntity);
		break;

	case ESeinVisualEventType::AbilityActivated:
		SeinActor->ReceiveAbilityActivated(Event.Tag);
		break;

	case ESeinVisualEventType::AbilityEnded:
		SeinActor->ReceiveAbilityEnded(Event.Tag);
		break;

	case ESeinVisualEventType::EffectApplied:
		SeinActor->ReceiveEffectApplied(Event.Tag);
		break;

	case ESeinVisualEventType::EffectRemoved:
		SeinActor->ReceiveEffectRemoved(Event.Tag);
		break;

	case ESeinVisualEventType::Death:
		// Sim-side death â€” route to the actor's ReceiveDeath for death FX / animation.
		// The entity is destroyed in the same tick; ReceiveEntityDestroyed fires on
		// the EntityDestroyed event below.
		SeinActor->ReceiveDeath();
		break;

	case ESeinVisualEventType::EntityDestroyed:
		SeinActor->ReceiveEntityDestroyed();
		break;

	default:
		break;
	}
}

USeinWorldSubsystem* USeinEntityComponent::GetSubsystem()
{
	if (!CachedSubsystem)
	{
		UWorld* World = GetWorld();
		if (World)
		{
			CachedSubsystem = World->GetSubsystem<USeinWorldSubsystem>();
		}
	}

	return CachedSubsystem;
}

void USeinEntityComponent::SyncTransformToActor()
{
	USeinWorldSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return;
	}

	const FSeinEntity* Entity = Subsystem->GetEntity(EntityHandle);
	if (!Entity)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	FTransform TargetTransform;

	if (bInterpolateTransform && bHasSimSnapshot)
	{
		// Interpolate between previous and current sim snapshots
		// using the subsystem's interpolation alpha (0 = previous tick, 1 = current tick)
		const float Alpha = Subsystem->GetInterpolationAlpha();

		const FVector PrevLocation = PreviousSimTransform.Location.ToVector();
		const FVector CurrLocation = CurrentSimTransform.Location.ToVector();
		const FVector InterpLocation = FMath::Lerp(PrevLocation, CurrLocation, Alpha);

		const FQuat PrevRotation = PreviousSimTransform.Rotation.ToQuat();
		const FQuat CurrRotation = CurrentSimTransform.Rotation.ToQuat();
		const FQuat InterpRotation = FQuat::Slerp(PrevRotation, CurrRotation, Alpha);

		const FVector PrevScale = PreviousSimTransform.Scale.ToVector();
		const FVector CurrScale = CurrentSimTransform.Scale.ToVector();
		const FVector InterpScale = FMath::Lerp(PrevScale, CurrScale, Alpha);

		TargetTransform = FTransform(InterpRotation, InterpLocation, InterpScale);
	}
	else
	{
		// No interpolation: use the entity's current sim transform directly
		TargetTransform = Entity->Transform.ToTransform();
	}

	// Idle entities produce the same fixed->float transform every frame. Avoid
	// pushing an unchanged transform through UE: even a no-op SetActorTransform
	// dirties component transforms/bounds and can cascade into skeletal render
	// and ray-tracing update work for hundreds of RTS actors.
	if (Owner->GetActorTransform().Equals(TargetTransform))
	{
		return;
	}

	Owner->SetActorTransform(TargetTransform);

	// Diagnostic: enable with `log LogSeinBridge Verbose`. Fires once per
	// entity-component tick with the rotation yaw we just pushed to the actor.
	UE_LOG(LogSeinBridge, Verbose,
		TEXT("EntityComponent %s: pos=(%.1f,%.1f,%.1f) yaw=%.1f deg [interp=%d]"),
		*Owner->GetName(),
		TargetTransform.GetLocation().X, TargetTransform.GetLocation().Y, TargetTransform.GetLocation().Z,
		TargetTransform.Rotator().Yaw,
		bInterpolateTransform ? 1 : 0);
}


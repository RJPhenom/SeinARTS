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
#include "Simulation/SeinComponentLiveTuning.h"
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
namespace
{
	const FInstancedStruct* FindComponentDataEntryByPath(
		const TArray<FInstancedStruct>& Entries,
		const FString& TypePath)
	{
		for (const FInstancedStruct& Entry : Entries)
		{
			if (Entry.IsValid()
				&& Entry.GetScriptStruct()->GetPathName() == TypePath)
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	FInstancedStruct* FindMutableComponentDataEntryByPath(
		TArray<FInstancedStruct>& Entries,
		const FString& TypePath)
	{
		return const_cast<FInstancedStruct*>(
			FindComponentDataEntryByPath(Entries, TypePath));
	}

	/** True when Bridge is the entity bridge on an actor class-default object. */
	bool IsClassDefaultBridge(const USeinEntityComponent& Bridge)
	{
		const AActor* Owner = Bridge.GetOwner();
		return Bridge.IsTemplate() && Owner
			&& Owner->HasAnyFlags(RF_ClassDefaultObject);
	}

	/** The bridge this one inherits ComponentData defaults from, or null at the
	 *  root. An actor instance inherits from its own class default; a class
	 *  default inherits from its archetype, which is the parent class default
	 *  for the native entity bridge. This is the single hierarchy walk every
	 *  inheritance decision in this file uses. */
	const USeinEntityComponent* FindInheritedDefaultBridge(
		const USeinEntityComponent& Source)
	{
		if (Source.IsTemplate())
		{
			const USeinEntityComponent* Parent =
				Cast<USeinEntityComponent>(Source.GetArchetype());
			return Parent && Parent != &Source && IsClassDefaultBridge(*Parent)
				? Parent : nullptr;
		}
		const AActor* Owner = Source.GetOwner();
		const UClass* OwnerClass = Owner ? Owner->GetClass() : nullptr;
		const AActor* ActorCDO = OwnerClass
			? Cast<AActor>(OwnerClass->GetDefaultObject())
			: nullptr;
		const USeinEntityComponent* ClassDefault = ActorCDO
			? ActorCDO->FindComponentByClass<USeinEntityComponent>()
			: nullptr;
		return ClassDefault != &Source ? ClassDefault : nullptr;
	}

	/** Transient / stale class generations (skeleton, reinstanced, trashed)
	 *  are enumerated by GetArchetypeInstances but must never be authored into. */
	bool IsLiveAuthoringClass(const UClass* Class)
	{
		if (!Class || Class->HasAnyClassFlags(CLASS_NewerVersionExists)
			|| Class->HasAnyFlags(RF_Transient))
		{
			return false;
		}
		const FString Name = Class->GetName();
		return !Name.StartsWith(TEXT("SKEL_"))
			&& !Name.StartsWith(TEXT("REINST_"))
			&& !Name.StartsWith(TEXT("TRASHCLASS_"))
			&& !Name.StartsWith(TEXT("PLACEHOLDER-CLASS"));
	}

	bool ImportPatchValue(
		USeinEntityComponent& Target,
		const FSeinComponentPropertyPatch& Patch,
		FString& OutError)
	{
		FInstancedStruct* Entry = FindMutableComponentDataEntryByPath(
			Target.ComponentData, Patch.ComponentTypePath);
		if (!Entry)
		{
			OutError = FString::Printf(
				TEXT("Component type '%s' is absent from '%s'."),
				*Patch.ComponentTypePath, *Target.GetPathName());
			return false;
		}
		FProperty* Property = nullptr;
		void* Value = nullptr;
		if (!SeinResolveComponentPropertyPath(
			Entry->GetScriptStruct(), Entry->GetMutableMemory(),
			Patch.PropertyPath, Property, Value, OutError))
		{
			return false;
		}
		const TCHAR* End = Property->ImportText_Direct(
			*Patch.ExportedValue, Value, &Target, PPF_None);
		if (!End)
		{
			OutError = FString::Printf(
				TEXT("Could not import live-tuning value for '%s'."),
				*Property->GetName());
			return false;
		}
		while (*End && FChar::IsWhitespace(*End))
		{
			++End;
		}
		if (*End)
		{
			OutError = FString::Printf(
				TEXT("Live-tuning value for '%s' has trailing input."),
				*Property->GetName());
			return false;
		}
		return true;
	}

	bool DoesPatchEqualComponentValue(
		const FSeinComponentPropertyPatch& Patch,
		const USeinEntityComponent& Left,
		const USeinEntityComponent& Right)
	{
		const FInstancedStruct* LeftEntry = FindComponentDataEntryByPath(
			Left.ComponentData, Patch.ComponentTypePath);
		const FInstancedStruct* RightEntry = FindComponentDataEntryByPath(
			Right.ComponentData, Patch.ComponentTypePath);
		if (!LeftEntry || !RightEntry
			|| LeftEntry->GetScriptStruct() != RightEntry->GetScriptStruct())
		{
			return false;
		}
		const FProperty* LeftProperty = nullptr;
		const FProperty* RightProperty = nullptr;
		const void* LeftValue = nullptr;
		const void* RightValue = nullptr;
		FString IgnoredError;
		return SeinResolveComponentPropertyPath(
				LeftEntry->GetScriptStruct(), LeftEntry->GetMemory(),
				Patch.PropertyPath, LeftProperty, LeftValue, IgnoredError)
			&& SeinResolveComponentPropertyPath(
				RightEntry->GetScriptStruct(), RightEntry->GetMemory(),
				Patch.PropertyPath, RightProperty, RightValue, IgnoredError)
			&& LeftProperty->SameType(RightProperty)
			&& LeftProperty->Identical(LeftValue, RightValue, PPF_None);
	}

	bool DoesPatchEqualExportedValue(
		const FSeinComponentPropertyPatch& Patch,
		const USeinEntityComponent& Component)
	{
		const FInstancedStruct* ExistingEntry = FindComponentDataEntryByPath(
			Component.ComponentData, Patch.ComponentTypePath);
		if (!ExistingEntry) return false;

		FInstancedStruct Candidate = *ExistingEntry;
		FProperty* CandidateProperty = nullptr;
		void* CandidateValue = nullptr;
		FString Error;
		if (!SeinResolveComponentPropertyPath(
				Candidate.GetScriptStruct(), Candidate.GetMutableMemory(),
				Patch.PropertyPath, CandidateProperty, CandidateValue, Error))
		{
			return false;
		}
		const TCHAR* End = CandidateProperty->ImportText_Direct(
			*Patch.ExportedValue, CandidateValue,
			const_cast<USeinEntityComponent*>(&Component), PPF_None);
		if (!End) return false;
		while (*End && FChar::IsWhitespace(*End)) ++End;
		if (*End) return false;

		const FProperty* ExistingProperty = nullptr;
		const void* ExistingValue = nullptr;
		return SeinResolveComponentPropertyPath(
				ExistingEntry->GetScriptStruct(), ExistingEntry->GetMemory(),
				Patch.PropertyPath, ExistingProperty, ExistingValue, Error)
			&& ExistingProperty->SameType(CandidateProperty)
			&& ExistingProperty->Identical(
				ExistingValue, CandidateValue, PPF_None);
	}

	/** Export the bridge's current value at Template's key as a patch. Used to
	 *  pin a class default's own value into the sim overlay set. */
	bool ExportCurrentValuePatch(
		const USeinEntityComponent& Bridge,
		const FSeinComponentPropertyPatch& Template,
		FSeinComponentPropertyPatch& OutPatch)
	{
		const FInstancedStruct* Entry = FindComponentDataEntryByPath(
			Bridge.ComponentData, Template.ComponentTypePath);
		if (!Entry) return false;
		const FProperty* Property = nullptr;
		const void* Value = nullptr;
		FString Error;
		if (!SeinResolveComponentPropertyPath(
			Entry->GetScriptStruct(), Entry->GetMemory(),
			Template.PropertyPath, Property, Value, Error))
		{
			return false;
		}
		FString Exported;
		Property->ExportText_Direct(Exported, Value, nullptr, nullptr, PPF_None);
		if (Exported.Len() > 64 * 1024) return false;
		OutPatch = Template;
		OutPatch.ExportedValue = MoveTemp(Exported);
		OutPatch.InstanceOverrideOperation =
			ESeinComponentInstanceOverrideOperation::None;
		return true;
	}

	/** Dirty an asset package for a real authoring change. PIE packages are
	 *  discarded with the session and never need the flag. */
	void MarkAuthoringPackageDirty(UObject& Object)
	{
		UPackage* Package = Object.GetPackage();
		if (Package && !Package->HasAnyPackageFlags(PKG_PlayInEditor))
		{
			Package->MarkPackageDirty();
		}
	}
}

void USeinEntityComponent::PostInitProperties()
{
	Super::PostInitProperties();
	// Archetype initialization copies every UPROPERTY from the template,
	// including the class default's history. Instances never consult it, and
	// keeping the copy would serialize a stale duplicate of the whole history
	// into every placed actor once the class default's history grows.
	if (!IsTemplate())
	{
		ComponentDataDefaultRevision = 0;
		ComponentDataDefaultChangeHistory.Reset();
	}
}

void USeinEntityComponent::EnsureComponentDataOverrideMetadataInitialized()
{
	if (IsTemplate())
	{
		return;
	}
	CatchUpComponentDataClassDefaultHistory();
	if (bComponentDataOverrideMetadataInitialized) return;

	bComponentDataOverrideMetadataInitialized = true;
	const USeinEntityComponent* ClassDefault =
		FindInheritedDefaultBridge(*this);
	if (!ClassDefault)
	{
		return;
	}

	TArray<FSeinComponentPropertyPatch> Differences;
	FString Error;
	if (!SeinBuildComponentPropertyPatches(
		ClassDefault->ComponentData, ComponentData, Differences, Error))
	{
		UE_LOG(LogSeinEntityComp, Warning,
			TEXT("Could not initialize ComponentData override metadata for %s: %s"),
			*GetPathName(), *Error);
		return;
	}
	for (const FSeinComponentPropertyPatch& Difference : Differences)
	{
		ComponentDataPropertyOverrides.AddUnique(
			SeinMakeComponentPropertyPatchKey(
				Difference.ComponentTypePath, Difference.PropertyPath));
	}
	ComponentDataPropertyOverrides.Sort();
	// Only dirty when this instance genuinely owns overrides worth persisting.
	// The initialization marker itself is recomputed for free on the next load,
	// so dirtying unconditionally here made every level containing a SeinARTS
	// actor come up modified the moment it was opened.
	if (!Differences.IsEmpty())
	{
		MarkAuthoringPackageDirty(*this);
	}
}

void USeinEntityComponent::RecordComponentDataClassDefaultChange(
	const TArray<FInstancedStruct>& Before,
	const TArray<FSeinComponentPropertyPatch>& NewPatches)
{
	if (!IsTemplate() || NewPatches.IsEmpty())
	{
		return;
	}
	TArray<FSeinComponentPropertyPatch> PreviousPatches;
	FString Error;
	if (!SeinBuildComponentPropertyPatches(
			ComponentData, Before, PreviousPatches, Error))
	{
		UE_LOG(LogSeinEntityComp, Warning,
			TEXT("Could not record ComponentData class-default history for %s: %s"),
			*GetPathName(), *Error);
		return;
	}

	TMap<FString, const FSeinComponentPropertyPatch*> PreviousByKey;
	for (const FSeinComponentPropertyPatch& Patch : PreviousPatches)
	{
		PreviousByKey.Add(SeinMakeComponentPropertyPatchKey(
			Patch.ComponentTypePath, Patch.PropertyPath), &Patch);
	}
	// Keep the source revision at or above the parent cursor. Instances copy
	// ComponentDataInheritedDefaultRevision from this template at creation, so
	// the cursor must never run ahead of the revisions recorded here or a
	// freshly placed instance could skip a later record.
	ComponentDataDefaultRevision = FMath::Max(
		ComponentDataDefaultRevision, ComponentDataInheritedDefaultRevision) + 1;
	const int32 NewRevision = ComponentDataDefaultRevision;
	for (const FSeinComponentPropertyPatch& NewPatch : NewPatches)
	{
		const FString Key = SeinMakeComponentPropertyPatchKey(
			NewPatch.ComponentTypePath, NewPatch.PropertyPath);
		const FSeinComponentPropertyPatch* const* Previous =
			PreviousByKey.Find(Key);
		if (!Previous || !*Previous)
		{
			UE_LOG(LogSeinEntityComp, Warning,
				TEXT("ComponentData class-default history is missing the old value for %s on %s."),
				*Key, *GetPathName());
			continue;
		}
		FSeinComponentDataDefaultChangeRecord& Record =
			ComponentDataDefaultChangeHistory.AddDefaulted_GetRef();
		Record.Revision = NewRevision;
		Record.PreviousValue = **Previous;
		Record.PreviousValue.InstanceOverrideOperation =
			ESeinComponentInstanceOverrideOperation::None;
		Record.NewValue = NewPatch;
		Record.NewValue.InstanceOverrideOperation =
			ESeinComponentInstanceOverrideOperation::None;
	}
	ComponentDataDefaultChangeHistory.Sort([](
		const FSeinComponentDataDefaultChangeRecord& Left,
		const FSeinComponentDataDefaultChangeRecord& Right)
	{
		if (Left.Revision != Right.Revision)
		{
			return Left.Revision < Right.Revision;
		}
		return SeinMakeComponentPropertyPatchKey(
			Left.NewValue.ComponentTypePath, Left.NewValue.PropertyPath)
			< SeinMakeComponentPropertyPatchKey(
				Right.NewValue.ComponentTypePath, Right.NewValue.PropertyPath);
	});
}

void USeinEntityComponent::CatchUpComponentDataClassDefaultHistory()
{
	const USeinEntityComponent* Ancestor = FindInheritedDefaultBridge(*this);
	if (!Ancestor
		|| ComponentDataInheritedDefaultRevision
			>= Ancestor->ComponentDataDefaultRevision)
	{
		return;
	}
	// A class default that inherits from another class default follows the
	// same history lane as a placed instance: a value still equal to the old
	// parent default adopts the new one, anything else is an override. The
	// adopted transitions are then re-recorded here so this class's own
	// unopened instances can catch up through their direct class default.
	if (IsTemplate() && !IsClassDefaultBridge(*this)) return;
	const bool bIsDerivedClassDefault = IsTemplate();

	bool bChanged = false;
	TArray<FInstancedStruct> BeforeCatchUp;
	TArray<FSeinComponentPropertyPatch> AdoptedPatches;
	if (bIsDerivedClassDefault)
	{
		BeforeCatchUp = ComponentData;
	}
	for (const FSeinComponentDataDefaultChangeRecord& Record :
		Ancestor->ComponentDataDefaultChangeHistory)
	{
		if (Record.Revision <= ComponentDataInheritedDefaultRevision)
		{
			continue;
		}
		const FString Key = SeinMakeComponentPropertyPatchKey(
			Record.NewValue.ComponentTypePath,
			Record.NewValue.PropertyPath);
		if (!bIsDerivedClassDefault
			&& ComponentDataPropertyOverrides.Contains(Key))
		{
			continue;
		}
		if (DoesPatchEqualExportedValue(Record.NewValue, *this))
		{
			continue;
		}
		FString Error;
		if (DoesPatchEqualExportedValue(Record.PreviousValue, *this))
		{
			if (ImportPatchValue(*this, Record.NewValue, Error))
			{
				bChanged = true;
				if (bIsDerivedClassDefault)
				{
					AdoptedPatches.Add(Record.NewValue);
				}
				continue;
			}
			UE_LOG(LogSeinEntityComp, Warning,
				TEXT("Could not apply deferred ComponentData default to %s: %s"),
				*GetPathName(), *Error);
		}
		// The serialized value matches neither side of this class-default
		// transition, so it is a genuine override. A derived class default
		// needs no marker: its divergence from the parent IS the override, the
		// same value-delta rule Unreal applies to ordinary class defaults.
		if (!bIsDerivedClassDefault)
		{
			ComponentDataPropertyOverrides.AddUnique(Key);
			bChanged = true;
		}
	}
	ComponentDataPropertyOverrides.Sort();
	const int32 PreviousCursor = ComponentDataInheritedDefaultRevision;
	ComponentDataInheritedDefaultRevision =
		Ancestor->ComponentDataDefaultRevision;
	if (bIsDerivedClassDefault)
	{
		if (!AdoptedPatches.IsEmpty())
		{
			RecordComponentDataClassDefaultChange(BeforeCatchUp, AdoptedPatches);
		}
		// Even without adopted records, hold the source revision at or above
		// the parent cursor (see RecordComponentDataClassDefaultChange).
		ComponentDataDefaultRevision = FMath::Max(
			ComponentDataDefaultRevision, ComponentDataInheritedDefaultRevision);

		// A class default that loads while a PIE match is running was not
		// observable when its ancestors were edited, so the sim resolved its
		// entities to the nearest ancestor record. Publish this class's own
		// current value for every key the walk examined (adopted or
		// overriding) so every peer pins it exactly. Ignored outside PIE.
		FSeinComponentLiveTuningRequest Pins;
		Pins.Scope = ESeinComponentLiveTuningScope::ActorClass;
		Pins.ActorClassPath = GetOwner()->GetClass()->GetPathName();
		TSet<FString> PinnedKeys;
		for (const FSeinComponentDataDefaultChangeRecord& Record :
			Ancestor->ComponentDataDefaultChangeHistory)
		{
			if (Record.Revision <= PreviousCursor) continue;
			const FString Key = SeinMakeComponentPropertyPatchKey(
				Record.NewValue.ComponentTypePath, Record.NewValue.PropertyPath);
			if (PinnedKeys.Contains(Key)) continue;
			FSeinComponentPropertyPatch Pin;
			if (ExportCurrentValuePatch(*this, Record.NewValue, Pin))
			{
				PinnedKeys.Add(Key);
				Pins.Patches.Add(MoveTemp(Pin));
			}
		}
		if (!Pins.Patches.IsEmpty() && Pins.Patches.Num() <= 256)
		{
			SeinOnComponentLiveTuningEditorRequest().Broadcast(*this, Pins);
		}
	}
	// The reconciled revision is a pure catch-up accelerator: when nothing was
	// actually adopted or reclassified, re-walking the history next load reaches
	// the same result, so do not dirty a package the designer never touched.
	if (bChanged)
	{
		MarkAuthoringPackageDirty(*this);
	}
}

void USeinEntityComponent::RefreshInheritedComponentDataFromClassDefaults()
{
	if (IsTemplate()) return;
	const USeinEntityComponent* ClassDefault =
		FindInheritedDefaultBridge(*this);
	if (!ClassDefault) return;

	TArray<FSeinComponentPropertyPatch> Defaults;
	FString Error;
	if (!SeinBuildComponentPropertyPatches(
		ComponentData, ClassDefault->ComponentData, Defaults, Error))
	{
		return;
	}
	bool bChanged = false;
	for (const FSeinComponentPropertyPatch& DefaultPatch : Defaults)
	{
		const FString Key = SeinMakeComponentPropertyPatchKey(
			DefaultPatch.ComponentTypePath, DefaultPatch.PropertyPath);
		if (ComponentDataPropertyOverrides.Contains(Key))
		{
			continue;
		}
		if (ImportPatchValue(*this, DefaultPatch, Error))
		{
			bChanged = true;
		}
	}
	if (bChanged)
	{
		MarkAuthoringPackageDirty(*this);
	}
}

void USeinEntityComponent::PostLoad()
{
	Super::PostLoad();
	if (IsTemplate())
	{
		// A Blueprint class default that was not open while its parent's
		// defaults changed. ConditionalPostLoad guarantees the archetype chain
		// has already been post-loaded, so the parent history is complete.
		CatchUpComponentDataClassDefaultHistory();
		return;
	}
	EnsureComponentDataOverrideMetadataInitialized();
	RefreshInheritedComponentDataFromClassDefaults();
}

void USeinEntityComponent::BeginComponentDataEdit()
{
	ComponentDataBeforeEditorChange = ComponentData;
	bCapturedComponentDataBeforeEditorChange = true;
}

void USeinEntityComponent::EndComponentDataEdit()
{
	HandleComponentDataEdited();
}

void USeinEntityComponent::PreEditChange(FEditPropertyChain& PropertyAboutToChange)
{
	bCapturedComponentDataBeforeEditorChange = false;
	for (auto Node = PropertyAboutToChange.GetHead(); Node; Node = Node->GetNextNode())
	{
		const FProperty* Property = Node->GetValue();
		if (Property
			&& Property->GetFName()
				== GET_MEMBER_NAME_CHECKED(USeinEntityComponent, ComponentData))
		{
			BeginComponentDataEdit();
			break;
		}
	}
	UObject::PreEditChange(PropertyAboutToChange);
}

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

	HandleComponentDataEdited();
}

void USeinEntityComponent::HandleComponentDataEdited()
{
	TArray<FSeinComponentPropertyPatch> Patches;
	FString PatchError;
	const bool bHasCapturedEdit = bCapturedComponentDataBeforeEditorChange;
	const bool bBuiltPatches = bHasCapturedEdit
		&& SeinBuildComponentPropertyPatches(
			ComponentDataBeforeEditorChange, ComponentData, Patches, PatchError);

	if (bHasCapturedEdit && !bBuiltPatches)
	{
		// Adding/removing a component changes storage topology and intentionally
		// does not use the property-patch lane.
		UE_LOG(LogSeinEntityComp, Warning,
			TEXT("ComponentData structural edit on %s was not live tuned: %s"),
			*GetPathName(), *PatchError);
		bCapturedComponentDataBeforeEditorChange = false;
		return;
	}
	if (Patches.IsEmpty())
	{
		bCapturedComponentDataBeforeEditorChange = false;
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (IsClassDefaultBridge(*this))
	{
		RecordComponentDataClassDefaultChange(
			ComponentDataBeforeEditorChange, Patches);
		TArray<FSeinComponentLiveTuningClassEntry> DerivedClassEntries;
		PropagateComponentDataChangesToInstances(Patches, DerivedClassEntries);

		// One class-scoped request carries the edited class plus an explicit
		// entry for every loaded derived class default: inheriting ones carry
		// the new value, overriding ones pin their own. Hierarchy is resolved
		// here, in the only process that can observe live class defaults; the
		// sim resolves entities nearest-derived-first along the class chain so
		// a class it could not observe falls through to the closest ancestor.
		FSeinComponentLiveTuningRequest Payload;
		Payload.Scope = ESeinComponentLiveTuningScope::ActorClass;
		Payload.ActorClassPath = OwnerActor->GetClass()->GetPathName();
		Payload.Patches = Patches;
		Payload.DerivedClassEntries = MoveTemp(DerivedClassEntries);
		SeinOnComponentLiveTuningEditorRequest().Broadcast(*this, Payload);
		bCapturedComponentDataBeforeEditorChange = false;
		return;
	}
	if (IsTemplate())
	{
		// Non-CDO archetypes are not authoring surfaces for ComponentData.
		bCapturedComponentDataBeforeEditorChange = false;
		return;
	}

	// Maintain the normal Unreal two-layer model on every actor instance:
	// equal-to-CDO means inherited; any different leaf is an instance override.
	EnsureComponentDataOverrideMetadataInitialized();
	const USeinEntityComponent* ClassDefault = FindInheritedDefaultBridge(*this);
	if (ClassDefault)
	{
		for (const FSeinComponentPropertyPatch& Patch : Patches)
		{
			const FString Key = SeinMakeComponentPropertyPatchKey(
				Patch.ComponentTypePath, Patch.PropertyPath);
			if (DoesPatchEqualComponentValue(Patch, *this, *ClassDefault))
			{
				ComponentDataPropertyOverrides.Remove(Key);
			}
			else
			{
				ComponentDataPropertyOverrides.AddUnique(Key);
			}
		}
		ComponentDataPropertyOverrides.Sort();
	}

	const UWorld* World = OwnerActor ? OwnerActor->GetWorld() : nullptr;
	if (World && World->WorldType == EWorldType::PIE
		&& EntityHandle.IsValid())
	{
		FSeinComponentLiveTuningRequest Payload;
		Payload.Scope = ESeinComponentLiveTuningScope::Entity;
		Payload.TargetEntity = EntityHandle;
		Payload.ActorClassPath = OwnerActor->GetClass()->GetPathName();
		Payload.Patches = MoveTemp(Patches);
		if (ClassDefault)
		{
			for (FSeinComponentPropertyPatch& Patch : Payload.Patches)
			{
				Patch.InstanceOverrideOperation =
					DoesPatchEqualComponentValue(Patch, *this, *ClassDefault)
					? ESeinComponentInstanceOverrideOperation::Clear
					: ESeinComponentInstanceOverrideOperation::Set;
			}
		}
		SeinOnComponentLiveTuningEditorRequest().Broadcast(*this, Payload);
	}
	bCapturedComponentDataBeforeEditorChange = false;
}

void USeinEntityComponent::PropagateComponentDataChangesToInstances(
	const TArray<FSeinComponentPropertyPatch>& Patches,
	TArray<FSeinComponentLiveTuningClassEntry>& OutDerivedClassEntries)
{
	OutDerivedClassEntries.Reset();
	if (!IsClassDefaultBridge(*this)) return;
	if (!bCapturedComponentDataBeforeEditorChange || Patches.IsEmpty()) return;

	// Enumerate placed instances via the engine's archetype-instance machinery
	// rather than scanning every actor in every loaded editor world. This is the
	// same path UE's own Details-panel value propagation uses
	// (FPropertyNode::PropagatePropertyChange -> UObject::GetArchetypeInstances):
	// a class-bucketed lookup (UObjectHash ClassToObjectListMap) instead of an
	// O(worlds x all-actors) sweep. We call it on the OWNER ACTOR CDO -- which is
	// the RF_ClassDefaultObject -- so the engine yields every actor of this class
	// AND of every derived class: placed instances, preview actors, PIE actors,
	// and the class defaults of derived Blueprints. Each receives the new value
	// only where it still carried the old one, exactly like a stock Unreal
	// parent-default edit flowing down a Blueprint hierarchy.
	AActor* OwnerActorCDO = GetOwner();
	UClass* OwnerActorClass = OwnerActorCDO->GetClass();

	TArray<UObject*> ArchetypeInstances;
	OwnerActorCDO->GetArchetypeInstances(ArchetypeInstances);

	int32 InstancesScanned = 0;
	int32 InstancesUpdated = 0;
	int32 DerivedDefaultsUpdated = 0;
	TSet<UPackage*> DirtiedPackages;

	for (UObject* InstanceObj : ArchetypeInstances)
	{
		AActor* Instance = Cast<AActor>(InstanceObj);
		if (!Instance) continue;

		const bool bIsDerivedClassDefault =
			Instance->HasAnyFlags(RF_ClassDefaultObject);
		const UWorld* InstanceWorld = Instance->GetWorld();
		EWorldType::Type WT = EWorldType::None;
		if (bIsDerivedClassDefault)
		{
			if (!IsLiveAuthoringClass(Instance->GetClass())) continue;
		}
		else
		{
			// Mirror the authoring value into loaded editor, preview, and PIE
			// actor objects. PIE's bridge remains only the Details-panel
			// authoring mirror; the authoritative sim change travels separately
			// through the command.
			if (!InstanceWorld) continue;
			WT = InstanceWorld->WorldType;
			if (WT != EWorldType::Editor
				&& WT != EWorldType::EditorPreview
				&& WT != EWorldType::PIE) continue;
		}

		TArray<USeinEntityComponent*> InstBridges;
		Instance->GetComponents<USeinEntityComponent>(InstBridges);
		for (USeinEntityComponent* InstBridge : InstBridges)
		{
			// Only bridges whose archetype CHAIN reaches this CDO bridge: direct
			// instances, derived class defaults, and instances of derived classes.
			if (!InstBridge || InstBridge == this
				|| !InstBridge->IsBasedOnArchetype(this)) continue;
			++InstancesScanned;
			if (!bIsDerivedClassDefault)
			{
				InstBridge->EnsureComponentDataOverrideMetadataInitialized();
			}

			bool bModifiedInstance = false;
			TArray<FInstancedStruct> DerivedBefore;
			TArray<FSeinComponentPropertyPatch> AppliedPatches;
			FSeinComponentLiveTuningClassEntry DerivedEntry;
			for (const FSeinComponentPropertyPatch& Patch : Patches)
			{
				const FString PatchKey = SeinMakeComponentPropertyPatchKey(
					Patch.ComponentTypePath, Patch.PropertyPath);
				if (!bIsDerivedClassDefault
					&& InstBridge->ComponentDataPropertyOverrides.Contains(PatchKey))
				{
					continue;
				}
				const FInstancedStruct* OldEntry = FindComponentDataEntryByPath(
					ComponentDataBeforeEditorChange, Patch.ComponentTypePath);
				FInstancedStruct* InstanceEntry = FindMutableComponentDataEntryByPath(
					InstBridge->ComponentData, Patch.ComponentTypePath);
				if (!OldEntry || !InstanceEntry
					|| OldEntry->GetScriptStruct() != InstanceEntry->GetScriptStruct())
				{
					continue;
				}

				const FProperty* OldProperty = nullptr;
				const FProperty* InstanceProperty = nullptr;
				const void* OldValue = nullptr;
				const void* InstanceValue = nullptr;
				FString ResolveError;
				if (!SeinResolveComponentPropertyPath(
						OldEntry->GetScriptStruct(), OldEntry->GetMemory(),
						Patch.PropertyPath, OldProperty, OldValue, ResolveError)
					|| !SeinResolveComponentPropertyPath(
						InstanceEntry->GetScriptStruct(), InstanceEntry->GetMemory(),
						Patch.PropertyPath, InstanceProperty, InstanceValue, ResolveError)
					|| !OldProperty->SameType(InstanceProperty)
					|| !OldProperty->Identical(OldValue, InstanceValue, PPF_None))
				{
					// A value that already diverged from the old default is an
					// override at this layer (instance or derived class default).
					// A derived class default pins its own current value so the
					// sim's nearest-ancestor resolution can never clobber it.
					FSeinComponentPropertyPatch Pin;
					if (bIsDerivedClassDefault
						&& ExportCurrentValuePatch(*InstBridge, Patch, Pin))
					{
						DerivedEntry.Patches.Add(MoveTemp(Pin));
					}
					continue;
				}

				if (!bModifiedInstance)
				{
					if (bIsDerivedClassDefault)
					{
						DerivedBefore = InstBridge->ComponentData;
					}
					if (WT != EWorldType::PIE)
					{
						InstBridge->Modify();
					}
				}
				bModifiedInstance = true;
				if (ImportPatchValue(*InstBridge, Patch, ResolveError))
				{
					AppliedPatches.Add(Patch);
					FSeinComponentPropertyPatch Inherited = Patch;
					Inherited.InstanceOverrideOperation =
						ESeinComponentInstanceOverrideOperation::None;
					DerivedEntry.Patches.Add(MoveTemp(Inherited));
				}
				else
				{
					UE_LOG(LogSeinEntityComp, Warning,
						TEXT("Could not propagate ComponentData property to %s: %s"),
						*InstBridge->GetPathName(), *ResolveError);
				}
			}
			if (bIsDerivedClassDefault && !DerivedEntry.Patches.IsEmpty())
			{
				// Explicit per-class values for the sim: inherited keys carry the
				// new value, overriding keys carry this class default's pin.
				DerivedEntry.ActorClassPath = Instance->GetClass()->GetPathName();
				OutDerivedClassEntries.Add(MoveTemp(DerivedEntry));
			}
			if (!bModifiedInstance) continue;

			if (bIsDerivedClassDefault)
			{
				// The derived class default now carries the inherited value. Record
				// the transition in ITS history so its own unopened instances catch
				// up through their direct class default.
				InstBridge->RecordComponentDataClassDefaultChange(
					DerivedBefore, AppliedPatches);
				++DerivedDefaultsUpdated;
			}
			if (WT != EWorldType::PIE)
			{
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
			}
			++InstancesUpdated;
		}
	}
	OutDerivedClassEntries.Sort([](
		const FSeinComponentLiveTuningClassEntry& Left,
		const FSeinComponentLiveTuningClassEntry& Right)
	{
		return Left.ActorClassPath < Right.ActorClassPath;
	});

	UE_LOG(LogSeinEntityComp, Log,
		TEXT("[PropagateComponentData] CDO=%s, Class=%s: patches=%d, scanned=%d, updated=%d (derived class defaults=%d), dirtied %d package(s)."),
		*GetName(), *GetNameSafe(OwnerActorClass), Patches.Num(),
		InstancesScanned, InstancesUpdated, DerivedDefaultsUpdated,
		DirtiedPackages.Num());

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


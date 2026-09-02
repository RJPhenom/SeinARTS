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
#include "Authoring/SeinDataComponent.h"
#include "UObject/ObjectSaveContext.h"
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
#include "UObject/UObjectGlobals.h"       // LoadObject (structural snapshot materialization)
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

bool USeinEntityComponent::ValidateComponentData(
	const TArray<FInstancedStruct>& ComponentData,
	TArray<FSeinComponentDataIssue>& OutIssues)
{
	OutIssues.Reset();

	TMap<const UScriptStruct*, int32> FirstIndexByType;
	FirstIndexByType.Reserve(ComponentData.Num());

	for (int32 Index = 0; Index < ComponentData.Num(); ++Index)
	{
		const FInstancedStruct& Entry = ComponentData[Index];
		if (!Entry.IsValid())
		{
			OutIssues.Add(FSeinComponentDataIssue{Index, FString::Printf(
				TEXT("ComponentData[%d] has no resolvable struct type (never assigned in the picker, or its struct asset was deleted/renamed)"),
				Index)});
			continue;
		}

		const UScriptStruct* Type = Entry.GetScriptStruct();
		if (const int32* FirstIndex = FirstIndexByType.Find(Type))
		{
			OutIssues.Add(FSeinComponentDataIssue{Index, FString::Printf(
				TEXT("ComponentData[%d] duplicates struct type %s already authored at ComponentData[%d]"),
				Index, *Type->GetPathName(), *FirstIndex)});
			continue;
		}
		FirstIndexByType.Add(Type, Index);
	}

	return OutIssues.Num() == 0;
}

void USeinEntityComponent::InjectAuthoredComponents(USeinWorldSubsystem& World, FSeinEntityHandle Handle) const
{
	// ValidateComponentData is the single shared definition of "valid
	// ComponentData" (also used by the Blueprint pre-compile gate and match
	// bootstrap — see its declaration). This runtime spawn path stays
	// tolerant of a bad entry (skip / last-entry-wins) rather than failing
	// the spawn outright: match bootstrap is where the same defect is
	// fatal; the compile gate surfaces it early as compiler warnings.
	TArray<FSeinComponentDataIssue> Issues;
	if (!ValidateComponentData(ComponentData, Issues))
	{
		for (const FSeinComponentDataIssue& Issue : Issues)
		{
			UE_LOG(LogSeinEntityComp, Warning, TEXT("%s on %s."),
				*Issue.Description, *GetNameSafe(GetOwner()));
		}
	}

	// One simulation storage exists per struct type. A within-array duplicate
	// (already warned about above) overwrites deterministically — last entry
	// in ComponentData wins. Storage occupancy is also checked here directly
	// (not just via the array-only pre-scan above) because it catches any
	// collision with a type this handle already carries from a source OTHER
	// than this ComponentData array — the pre-scan can't see that.
	for (const FInstancedStruct& Entry : ComponentData)
	{
		if (!Entry.IsValid()) continue;

		UScriptStruct* StructType = const_cast<UScriptStruct*>(Entry.GetScriptStruct());
		if (!StructType) continue;

		ISeinComponentStorage* Storage = World.GetOrCreateStorageForType(StructType);
		if (!Storage) continue;

		if (Storage->GetComponentRaw(Handle) != nullptr)
		{
			UE_LOG(LogSeinEntityComp, Warning,
				TEXT("Entity %s already has a component of type %s from another source; "
					 "this ComponentData entry on %s will overwrite it."),
				*Handle.ToString(), *StructType->GetName(), *GetNameSafe(GetOwner()));
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

	// ------------------------------------------------------------------
	// Structural-lane helpers (see FSeinComponentDataStructuralChangeRecord)
	// ------------------------------------------------------------------

	constexpr int32 MaxComponentDataSnapshotValueLen = 64 * 1024;

	/** Export every VALID entry as (type path, full exported value). Invalid
	 *  rows carry nothing adoptable and are skipped — rebuilding from a
	 *  snapshot therefore also heals stale empty rows. False when an entry's
	 *  export exceeds the snapshot bound. */
	bool SnapshotComponentDataEntries(
		const TArray<FInstancedStruct>& Entries,
		TArray<FSeinComponentDataEntrySnapshot>& OutSnapshots)
	{
		OutSnapshots.Reset();
		for (const FInstancedStruct& Entry : Entries)
		{
			if (!Entry.IsValid())
			{
				continue;
			}
			FSeinComponentDataEntrySnapshot& Snapshot =
				OutSnapshots.AddDefaulted_GetRef();
			Snapshot.ComponentTypePath = Entry.GetScriptStruct()->GetPathName();
			Entry.GetScriptStruct()->ExportText(
				Snapshot.ExportedValue, Entry.GetMemory(),
				nullptr, nullptr, PPF_None, nullptr);
			if (Snapshot.ExportedValue.Len() > MaxComponentDataSnapshotValueLen)
			{
				OutSnapshots.Reset();
				return false;
			}
		}
		return true;
	}

	/** Materialize a snapshot list back into ComponentData entries. All-or-
	 *  nothing: a type that no longer resolves (deleted/renamed struct asset)
	 *  fails the whole build so adoption never half-applies. */
	bool BuildComponentDataFromSnapshots(
		const TArray<FSeinComponentDataEntrySnapshot>& Snapshots,
		TArray<FInstancedStruct>& OutEntries,
		FString& OutError)
	{
		OutEntries.Reset();
		for (const FSeinComponentDataEntrySnapshot& Snapshot : Snapshots)
		{
			UScriptStruct* Type = LoadObject<UScriptStruct>(
				nullptr, *Snapshot.ComponentTypePath);
			if (!Type)
			{
				OutError = FString::Printf(
					TEXT("Component struct '%s' could not be resolved."),
					*Snapshot.ComponentTypePath);
				OutEntries.Reset();
				return false;
			}
			FInstancedStruct& Entry = OutEntries.AddDefaulted_GetRef();
			Entry.InitializeAs(Type);
			const TCHAR* End = Type->ImportText(
				*Snapshot.ExportedValue, Entry.GetMutableMemory(),
				nullptr, PPF_None, GWarn, Type->GetName());
			if (!End)
			{
				OutError = FString::Printf(
					TEXT("Snapshot value for '%s' could not be imported."),
					*Snapshot.ComponentTypePath);
				OutEntries.Reset();
				return false;
			}
			// Roundtrip fidelity: a snapshot recorded against an older struct
			// layout (e.g. a UDS field renamed since) imports its unknown
			// fields as silent skips, leaving defaults behind. Fail the build
			// instead — a loud Skipped beats a silent half-default adoption.
			FString Reexported;
			Type->ExportText(Reexported, Entry.GetMemory(),
				nullptr, nullptr, PPF_None, nullptr);
			if (Reexported != Snapshot.ExportedValue)
			{
				OutError = FString::Printf(
					TEXT("Snapshot for '%s' does not survive an import/export roundtrip (struct layout changed since it was recorded)."),
					*Snapshot.ComponentTypePath);
				OutEntries.Reset();
				return false;
			}
		}
		return true;
	}

	TArray<FString> SortedComponentTypePaths(
		const TArray<FInstancedStruct>& Entries)
	{
		TArray<FString> Paths;
		for (const FInstancedStruct& Entry : Entries)
		{
			if (Entry.IsValid())
			{
				Paths.Add(Entry.GetScriptStruct()->GetPathName());
			}
		}
		Paths.Sort();
		return Paths;
	}

	TArray<FString> SortedComponentTypePaths(
		const TArray<FSeinComponentDataEntrySnapshot>& Snapshots)
	{
		TArray<FString> Paths;
		for (const FSeinComponentDataEntrySnapshot& Snapshot : Snapshots)
		{
			Paths.Add(Snapshot.ComponentTypePath);
		}
		Paths.Sort();
		return Paths;
	}

	/** Entry-type multiset equality over the VALID entries of each side. */
	bool DoTypeMultisetsMatch(
		const TArray<FInstancedStruct>& Entries,
		const TArray<FSeinComponentDataEntrySnapshot>& Snapshots)
	{
		return SortedComponentTypePaths(Entries)
			== SortedComponentTypePaths(Snapshots);
	}

	bool DoTypeMultisetsMatch(
		const TArray<FInstancedStruct>& Left,
		const TArray<FInstancedStruct>& Right)
	{
		return SortedComponentTypePaths(Left)
			== SortedComponentTypePaths(Right);
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
		ComponentDataStructuralChangeHistory.Reset();
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
		// Only a genuine SHAPE mismatch (no recorded structural transition
		// explained it — content predating the structural lane, or a deleted
		// struct asset) earns the structural-override flag. A same-shape
		// patch-build failure (depth/size pathologies) keeps the old
		// warn-and-return behavior. Data is never destroyed either way;
		// Map Check surfaces the flag per actor.
		if (!DoTypeMultisetsMatch(ComponentData, ClassDefault->ComponentData))
		{
			if (!bComponentDataStructuralOverride)
			{
				bComponentDataStructuralOverride = true;
				UE_LOG(LogSeinEntityComp, Warning,
					TEXT("%s: ComponentData differs in SHAPE from class default %s and no recorded transition explains it (%s). ")
					TEXT("This instance will not receive Blueprint ComponentData updates until re-synced — ")
					TEXT("revert the Component Data property on the placed actor to re-join inheritance."),
					*GetPathName(), *ClassDefault->GetPathName(), *Error);
				MarkAuthoringPackageDirty(*this);
			}
		}
		else
		{
			UE_LOG(LogSeinEntityComp, Warning,
				TEXT("Could not initialize ComponentData override metadata for %s: %s"),
				*GetPathName(), *Error);
		}
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

	// Merge both history lanes into one revision-ordered replay. One edit is
	// one kind (a structural edit's value changes ride inside its snapshots),
	// so revisions never collide across lanes; sort structural-first on a tie
	// anyway for determinism.
	struct FReplayItem
	{
		int32 Revision = 0;
		const FSeinComponentDataDefaultChangeRecord* Property = nullptr;
		const FSeinComponentDataStructuralChangeRecord* Structural = nullptr;
	};
	TArray<FReplayItem> Replay;
	for (const FSeinComponentDataDefaultChangeRecord& Record :
		Ancestor->ComponentDataDefaultChangeHistory)
	{
		if (Record.Revision > ComponentDataInheritedDefaultRevision)
		{
			Replay.Add(FReplayItem{Record.Revision, &Record, nullptr});
		}
	}
	for (const FSeinComponentDataStructuralChangeRecord& Record :
		Ancestor->ComponentDataStructuralChangeHistory)
	{
		if (Record.Revision > ComponentDataInheritedDefaultRevision)
		{
			Replay.Add(FReplayItem{Record.Revision, nullptr, &Record});
		}
	}
	Replay.StableSort([](const FReplayItem& Left, const FReplayItem& Right)
	{
		if (Left.Revision != Right.Revision)
		{
			return Left.Revision < Right.Revision;
		}
		return Left.Structural != nullptr && Right.Structural == nullptr;
	});

	// A derived class default batches adopted PROPERTY transitions into one
	// record of its own. That batch must flush before any structural adoption:
	// its Previous values are derived from the current array, which a
	// structural adoption reshapes.
	auto FlushAdoptedPatches = [this, &BeforeCatchUp, &AdoptedPatches,
		bIsDerivedClassDefault]()
	{
		if (!bIsDerivedClassDefault)
		{
			return;
		}
		if (!AdoptedPatches.IsEmpty())
		{
			RecordComponentDataClassDefaultChange(BeforeCatchUp, AdoptedPatches);
			AdoptedPatches.Reset();
		}
		BeforeCatchUp = ComponentData;
	};

	for (const FReplayItem& Item : Replay)
	{
		if (Item.Structural)
		{
			if (!bIsDerivedClassDefault && bComponentDataStructuralOverride)
			{
				continue; // stays deliberately diverged
			}
			if (!bIsDerivedClassDefault)
			{
				// Live-match bridges keep the topology their sim entities were
				// injected with — never reshape them mid-session (the edit-time
				// push path skips PIE too); they reconcile on next editor load.
				const AActor* OwnerActor = GetOwner();
				const UWorld* OwnerWorld = OwnerActor ? OwnerActor->GetWorld() : nullptr;
				if (OwnerWorld
					&& (OwnerWorld->WorldType == EWorldType::PIE
						|| OwnerWorld->WorldType == EWorldType::Game))
				{
					continue;
				}
			}
			FlushAdoptedPatches();
			TArray<FInstancedStruct> StructuralBefore;
			if (bIsDerivedClassDefault)
			{
				StructuralBefore = ComponentData;
			}
			switch (TryAdoptStructuralRecord(*Item.Structural))
			{
			case EStructuralAdoptOutcome::Adopted:
				bChanged = true;
				if (bIsDerivedClassDefault)
				{
					// Re-record in THIS class's history so its own unopened
					// instances catch up through their direct class default.
					if (!RecordStructuralChangeToOwnHistory(StructuralBefore))
					{
						UE_LOG(LogSeinEntityComp, Warning,
							TEXT("%s adopted a structural ComponentData default but could not re-record it; its unopened instances will flag divergence on load."),
							*GetPathName());
					}
					BeforeCatchUp = ComponentData;
				}
				break;
			case EStructuralAdoptOutcome::Flagged:
				// A derived class default's structural divergence IS its
				// override — quiet, same value-delta rule as everywhere else.
				//
				// Instance idempotency guard: a freshly placed instance replays
				// the FULL history (its cursor is archetype-copied from the
				// template's cursor into ITS parent, not the template's own
				// revision), so an in-sync instance can match neither side of
				// an OLD record. Shape equal to the ancestor's CURRENT array
				// means in sync with the end of history — skip, never flag.
				if (!bIsDerivedClassDefault
					&& !DoTypeMultisetsMatch(ComponentData, Ancestor->ComponentData))
				{
					bComponentDataStructuralOverride = true;
					bChanged = true;
					UE_LOG(LogSeinEntityComp, Warning,
						TEXT("%s: ComponentData matches neither side of a recorded structural default change of %s. ")
						TEXT("This instance will not receive Blueprint ComponentData updates until re-synced — ")
						TEXT("revert the Component Data property on the placed actor to re-join inheritance."),
						*GetPathName(), *Ancestor->GetPathName());
				}
				break;
			case EStructuralAdoptOutcome::AlreadyCurrent:
			case EStructuralAdoptOutcome::Skipped:
				break;
			}
			continue;
		}

		const FSeinComponentDataDefaultChangeRecord& Record = *Item.Property;
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
		FlushAdoptedPatches();
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
		// Structural divergence: flagged (loudly) by Ensure/CatchUp when it is
		// first discovered; here we keep quiet on an already-known flag and
		// catch any path that reached refresh without one. Same-shape
		// patch-build failures (depth/size pathologies) are not shape
		// divergence and never earn the flag.
		if (!bComponentDataStructuralOverride
			&& !DoTypeMultisetsMatch(ComponentData, ClassDefault->ComponentData))
		{
			bComponentDataStructuralOverride = true;
			UE_LOG(LogSeinEntityComp, Warning,
				TEXT("%s: ComponentData differs in SHAPE from class default %s (%s). ")
				TEXT("Revert the Component Data property on the placed actor to re-join inheritance."),
				*GetPathName(), *ClassDefault->GetPathName(), *Error);
			MarkAuthoringPackageDirty(*this);
		}
		return;
	}
	if (bComponentDataStructuralOverride)
	{
		// Shapes reconcile again — a hand revert, or structural adoption above.
		bComponentDataStructuralOverride = false;
		MarkAuthoringPackageDirty(*this);
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
		// Patch-build failure is USUALLY a structural edit (entries added/
		// removed/retyped) but not always — pathological value edits fail too
		// (recursion depth, oversize exports). Only a changed entry-type
		// multiset takes the structural lane; storage topology is intentionally
		// never live tuned (a running match keeps the component set its
		// entities were injected with), but the authoring-inheritance lanes
		// handle it: class defaults record + propagate the transition,
		// instances reconcile their structural-override flag.
		const bool bMultisetChanged = !DoTypeMultisetsMatch(
			ComponentDataBeforeEditorChange, ComponentData);
		if (!bMultisetChanged)
		{
			// Same-shape edit whose values cannot be patch-encoded: neither
			// lane can represent it. Keep the old loud behavior — the change
			// stays local to this object and never propagates.
			UE_LOG(LogSeinEntityComp, Warning,
				TEXT("ComponentData edit on %s could not be patch-encoded and will not propagate or live tune: %s"),
				*GetPathName(), *PatchError);
			bCapturedComponentDataBeforeEditorChange = false;
			return;
		}
		if (IsClassDefaultBridge(*this))
		{
			HandleStructuralClassDefaultEdit();
		}
		else if (!IsTemplate())
		{
			// Instance structural edit. Reverting to the class default is the
			// one structural edit that re-joins inheritance; anything else is
			// a deliberate structural override (no warning — the designer is
			// looking right at it; Map Check reports it thereafter).
			EnsureComponentDataOverrideMetadataInitialized();
			if (ReconcileStructuralOverrideFlag())
			{
				MarkAuthoringPackageDirty(*this);
			}
		}
		UE_LOG(LogSeinEntityComp, Verbose,
			TEXT("ComponentData structural edit on %s (not live tuned): %s"),
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

bool USeinEntityComponent::RecordStructuralChangeToOwnHistory(
	const TArray<FInstancedStruct>& BeforeEntries)
{
	if (!IsTemplate())
	{
		return false;
	}
	FSeinComponentDataStructuralChangeRecord Record;
	if (!SnapshotComponentDataEntries(BeforeEntries, Record.Before)
		|| !SnapshotComponentDataEntries(ComponentData, Record.After))
	{
		UE_LOG(LogSeinEntityComp, Warning,
			TEXT("%s: structural ComponentData change exceeds the snapshot bound and was not recorded; unopened instances will flag divergence on load."),
			*GetPathName());
		return false;
	}
	// Same cursor rule as RecordComponentDataClassDefaultChange: the source
	// revision never trails the parent cursor, so a freshly placed instance
	// cannot skip a later record.
	ComponentDataDefaultRevision = FMath::Max(
		ComponentDataDefaultRevision, ComponentDataInheritedDefaultRevision) + 1;
	Record.Revision = ComponentDataDefaultRevision;
	ComponentDataStructuralChangeHistory.Add(MoveTemp(Record));
	return true;
}

USeinEntityComponent::EStructuralAdoptOutcome
USeinEntityComponent::TryAdoptStructuralRecord(
	const FSeinComponentDataStructuralChangeRecord& Record)
{
	// Legacy content can carry duplicate entry types (already a validator
	// error and match-fatal at bootstrap). Adoption matches entries BY TYPE,
	// so duplicates would reshuffle values between the first and last dup and
	// flip the injected last-entry-wins value — refuse to touch such content.
	auto HasDuplicateTypes = [](const TArray<FString>& SortedPaths)
	{
		for (int32 Index = 1; Index < SortedPaths.Num(); ++Index)
		{
			if (SortedPaths[Index] == SortedPaths[Index - 1])
			{
				return true;
			}
		}
		return false;
	};
	if (HasDuplicateTypes(SortedComponentTypePaths(ComponentData))
		|| HasDuplicateTypes(SortedComponentTypePaths(Record.Before))
		|| HasDuplicateTypes(SortedComponentTypePaths(Record.After)))
	{
		UE_LOG(LogSeinEntityComp, Warning,
			TEXT("%s: structural ComponentData adoption skipped — duplicate entry types present. Fix the duplicates first; they abort match bootstrap regardless."),
			*GetPathName());
		return EStructuralAdoptOutcome::Skipped;
	}

	// A structural change always alters the entry-type multiset (a same-shape
	// edit is value-lane by construction — the edit path gates on multiset
	// inequality), so at most one side can match.
	if (DoTypeMultisetsMatch(ComponentData, Record.After))
	{
		return EStructuralAdoptOutcome::AlreadyCurrent;
	}
	if (!DoTypeMultisetsMatch(ComponentData, Record.Before))
	{
		return EStructuralAdoptOutcome::Flagged;
	}

	FString Error;
	TArray<FInstancedStruct> BeforeEntries;
	if (!BuildComponentDataFromSnapshots(Record.Before, BeforeEntries, Error))
	{
		UE_LOG(LogSeinEntityComp, Warning,
			TEXT("%s: structural ComponentData default (revision %d) could not be replayed: %s"),
			*GetPathName(), Record.Revision, *Error);
		return EStructuralAdoptOutcome::Skipped;
	}

	// Every value difference from the OLD default is an override to carry
	// across — recorded keys and legacy-unrecorded ones alike, the same
	// philosophy as the property lane's else-override rule.
	TArray<FSeinComponentPropertyPatch> OverrideDiffs;
	if (!SeinBuildComponentPropertyPatches(
		BeforeEntries, ComponentData, OverrideDiffs, Error))
	{
		UE_LOG(LogSeinEntityComp, Warning,
			TEXT("%s: could not compute value overrides against the old structural default: %s"),
			*GetPathName(), *Error);
		return EStructuralAdoptOutcome::Skipped;
	}

	TArray<FInstancedStruct> AfterEntries;
	if (!BuildComponentDataFromSnapshots(Record.After, AfterEntries, Error))
	{
		UE_LOG(LogSeinEntityComp, Warning,
			TEXT("%s: structural ComponentData default (revision %d) could not be replayed: %s"),
			*GetPathName(), Record.Revision, *Error);
		return EStructuralAdoptOutcome::Skipped;
	}

	ComponentData = MoveTemp(AfterEntries);

	// Re-apply carried values whose component type survives; an override on a
	// removed type dies with its entry.
	TArray<FString> ReappliedKeys;
	for (const FSeinComponentPropertyPatch& Diff : OverrideDiffs)
	{
		if (!FindComponentDataEntryByPath(ComponentData, Diff.ComponentTypePath))
		{
			UE_LOG(LogSeinEntityComp, Log,
				TEXT("%s: value override on removed component type %s was dropped with its entry."),
				*GetPathName(), *Diff.ComponentTypePath);
			continue;
		}
		FString ImportError;
		if (ImportPatchValue(*this, Diff, ImportError))
		{
			ReappliedKeys.Add(SeinMakeComponentPropertyPatchKey(
				Diff.ComponentTypePath, Diff.PropertyPath));
		}
		else
		{
			UE_LOG(LogSeinEntityComp, Warning,
				TEXT("%s: could not re-apply a value override after structural adoption: %s"),
				*GetPathName(), *ImportError);
		}
	}
	if (!IsTemplate())
	{
		// The ledger now describes exactly the surviving overrides. A key whose
		// value equaled the old default adopts the new default and drops off —
		// the same equal-means-inherited rule the edit path applies. (Known
		// mirror-image over-pin: a re-applied value that happens to EQUAL the
		// new default stays keyed where the edit path would dissolve it; it
		// resolves on that property's next edit and is preferable to guessing.)
		ComponentDataPropertyOverrides = MoveTemp(ReappliedKeys);
		ComponentDataPropertyOverrides.Sort();
	}
	return EStructuralAdoptOutcome::Adopted;
}

bool USeinEntityComponent::ReconcileStructuralOverrideFlag()
{
	if (IsTemplate())
	{
		return false;
	}
	const USeinEntityComponent* ClassDefault = FindInheritedDefaultBridge(*this);
	if (!ClassDefault)
	{
		return false;
	}
	const bool bDesired =
		!DoTypeMultisetsMatch(ComponentData, ClassDefault->ComponentData);
	if (bDesired == bComponentDataStructuralOverride)
	{
		return false;
	}
	bComponentDataStructuralOverride = bDesired;
	return true;
}

void USeinEntityComponent::HandleStructuralClassDefaultEdit()
{
	if (!RecordStructuralChangeToOwnHistory(ComponentDataBeforeEditorChange))
	{
		return; // warned inside; without a record there is nothing to adopt from
	}
	const FSeinComponentDataStructuralChangeRecord Record =
		ComponentDataStructuralChangeHistory.Last();

	// Same enumeration as the property lane (see
	// PropagateComponentDataChangesToInstances). PIE actor instances are
	// skipped: a live match's bridge must keep the topology its sim entities
	// were injected with; they reconcile on their next editor load.
	AActor* OwnerActorCDO = GetOwner();
	TArray<UObject*> ArchetypeInstances;
	OwnerActorCDO->GetArchetypeInstances(ArchetypeInstances);

	// Derived class defaults FIRST, then actor instances. An instance of a
	// derived class must find its own class default already caught up and
	// re-recorded, so its Ensure adopts the DERIVED record; adopting the
	// parent record directly would pin the derived class's value deltas into
	// the INSTANCE override ledger (enumeration order is otherwise
	// unspecified — notably scrambled after Blueprint reinstancing).
	TArray<UObject*> OrderedInstances;
	OrderedInstances.Reserve(ArchetypeInstances.Num());
	for (UObject* Obj : ArchetypeInstances)
	{
		if (Obj && Obj->HasAnyFlags(RF_ClassDefaultObject))
		{
			OrderedInstances.Add(Obj);
		}
	}
	for (UObject* Obj : ArchetypeInstances)
	{
		if (Obj && !Obj->HasAnyFlags(RF_ClassDefaultObject))
		{
			OrderedInstances.Add(Obj);
		}
	}

	int32 InstancesScanned = 0;
	int32 InstancesAdopted = 0;
	int32 InstancesFlagged = 0;
	int32 DerivedDefaultsAdopted = 0;

	for (UObject* InstanceObj : OrderedInstances)
	{
		AActor* Instance = Cast<AActor>(InstanceObj);
		if (!Instance) continue;

		const bool bIsDerivedClassDefault =
			Instance->HasAnyFlags(RF_ClassDefaultObject);
		if (bIsDerivedClassDefault)
		{
			if (!IsLiveAuthoringClass(Instance->GetClass())) continue;
		}
		else
		{
			const UWorld* InstanceWorld = Instance->GetWorld();
			if (!InstanceWorld) continue;
			const EWorldType::Type WT = InstanceWorld->WorldType;
			if (WT != EWorldType::Editor && WT != EWorldType::EditorPreview)
			{
				continue;
			}
		}

		TArray<USeinEntityComponent*> InstBridges;
		Instance->GetComponents<USeinEntityComponent>(InstBridges);
		for (USeinEntityComponent* InstBridge : InstBridges)
		{
			if (!InstBridge || InstBridge == this
				|| !InstBridge->IsBasedOnArchetype(this)) continue;
			++InstancesScanned;
			if (!bIsDerivedClassDefault)
			{
				if (InstBridge->bComponentDataStructuralOverride) continue;
				// Modify BEFORE Ensure: Ensure's catch-up may itself replay the
				// record just added above (the explicit adopt below then lands
				// on AlreadyCurrent; counters undercount in that path). With
				// the snapshot taken first, Ctrl+Z of this structural edit
				// restores the instance's array, ledger, and cursor together
				// with the CDO — instances cannot be stranded on an undone
				// shape with cursors past a reused revision number.
				InstBridge->Modify();
				InstBridge->EnsureComponentDataOverrideMetadataInitialized();
				if (InstBridge->bComponentDataStructuralOverride) continue;
			}
			else
			{
				// Derived class default: only the Adopted path mutates, and it
				// requires shape == Before — pre-check so a non-adopting
				// derived Blueprint is never Modify()-dirtied by a base edit.
				if (!DoTypeMultisetsMatch(
					InstBridge->ComponentData, Record.Before))
				{
					continue;
				}
				InstBridge->Modify();
			}

			TArray<FInstancedStruct> InstanceBefore = InstBridge->ComponentData;
			switch (InstBridge->TryAdoptStructuralRecord(Record))
			{
			case EStructuralAdoptOutcome::Adopted:
				if (bIsDerivedClassDefault)
				{
					if (!InstBridge->RecordStructuralChangeToOwnHistory(
						InstanceBefore))
					{
						UE_LOG(LogSeinEntityComp, Warning,
							TEXT("%s adopted a structural ComponentData default but could not re-record it; its unopened instances will flag divergence on load."),
							*InstBridge->GetPathName());
					}
					++DerivedDefaultsAdopted;
				}
				else
				{
					++InstancesAdopted;
				}
				MarkAuthoringPackageDirty(*InstBridge);
				if (AActor* OwnerActor = InstBridge->GetOwner())
				{
					MarkAuthoringPackageDirty(*OwnerActor);
				}
				break;
			case EStructuralAdoptOutcome::Flagged:
				// A derived class default's structural divergence IS its
				// override — quiet, same value-delta rule as everywhere else.
				if (!bIsDerivedClassDefault)
				{
					InstBridge->bComponentDataStructuralOverride = true;
					++InstancesFlagged;
					UE_LOG(LogSeinEntityComp, Warning,
						TEXT("%s: ComponentData did not match the pre-edit class default and was left as a structural override — revert the Component Data property on the placed actor to re-join inheritance."),
						*InstBridge->GetPathName());
					MarkAuthoringPackageDirty(*InstBridge);
				}
				break;
			case EStructuralAdoptOutcome::AlreadyCurrent:
			case EStructuralAdoptOutcome::Skipped:
				break;
			}
		}
	}

	// NOTE: unlike the property lane's summary (where updated INCLUDES derived
	// class defaults), these two counters are mutually exclusive.
	UE_LOG(LogSeinEntityComp, Log,
		TEXT("[StructuralComponentData] CDO=%s, Class=%s: scanned=%d, adopted instances=%d, adopted derived class defaults=%d, flagged=%d."),
		*GetName(), *GetNameSafe(OwnerActorCDO->GetClass()), InstancesScanned,
		InstancesAdopted, DerivedDefaultsAdopted, InstancesFlagged);

	if (InstancesAdopted > 0 && GEditor)
	{
		GEditor->RedrawAllViewports(/*bInvalidateHitProxies*/ true);
	}
}

// =============================================================================
// AC-authoring prototype — bake USeinDataComponent payloads into ComponentData
// =============================================================================

void USeinEntityComponent::NotifyAuthoringComponentEdited(
	const USeinDataComponent& Component)
{
	const UScriptStruct* PayloadType = Component.GetPayloadStruct();
	if (!PayloadType)
	{
		return; // BP component whose payload has not synced yet
	}
	const FString TypePath = PayloadType->GetPathName();

	// Transacted: the details-panel edit that got us here Modify()'d only the
	// authoring component; without this, Ctrl+Z restores the component but
	// leaves the baked array (what PIE injects) at the post-edit value.
	Modify();
	BeginComponentDataEdit();

	if (!BakedComponentDataTypes.Contains(TypePath)
		&& FindComponentDataEntryByPath(ComponentData, TypePath))
	{
		UE_LOG(LogSeinEntityComp, Warning,
			TEXT("%s: authoring component %s now manages ComponentData type %s; the existing hand-authored entry was replaced by the component's values."),
			*GetPathName(), *Component.GetPathName(), *TypePath);
	}

	if (!Component.bInjectionEnabled)
	{
		ComponentData.RemoveAll([&TypePath](const FInstancedStruct& Entry)
		{
			return Entry.IsValid()
				&& Entry.GetScriptStruct()->GetPathName() == TypePath;
		});
	}
	else
	{
		FInstancedStruct Payload;
		if (Component.WritePayload(Payload) && Payload.IsValid())
		{
			if (FInstancedStruct* Existing =
				FindMutableComponentDataEntryByPath(ComponentData, TypePath))
			{
				*Existing = MoveTemp(Payload);
			}
			else
			{
				ComponentData.Add(MoveTemp(Payload));
			}
		}
	}
	BakedComponentDataTypes.AddUnique(TypePath);
	BakedComponentDataTypes.Sort();

	EndComponentDataEdit();
}

bool USeinEntityComponent::IsComponentDataShapeExplainedByAuthoring() const
{
	if (IsTemplate())
	{
		return false;
	}
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	TArray<USeinDataComponent*> AuthoringComponents;
	Owner->GetComponents<USeinDataComponent>(AuthoringComponents);
	if (AuthoringComponents.IsEmpty() && BakedComponentDataTypes.IsEmpty())
	{
		return false;
	}

	// Predicted shape = unmanaged entries as they are, plus one entry per
	// enabled authoring component. Matching the live shape means the
	// divergence from the class default is authored intent, not staleness.
	TArray<FString> Predicted;
	for (const FInstancedStruct& Entry : ComponentData)
	{
		if (Entry.IsValid()
			&& !BakedComponentDataTypes.Contains(
				Entry.GetScriptStruct()->GetPathName()))
		{
			Predicted.Add(Entry.GetScriptStruct()->GetPathName());
		}
	}
	for (const USeinDataComponent* Component : AuthoringComponents)
	{
		const UScriptStruct* PayloadType =
			Component ? Component->GetPayloadStruct() : nullptr;
		if (PayloadType && Component->bInjectionEnabled)
		{
			Predicted.Add(PayloadType->GetPathName());
		}
	}
	Predicted.Sort();
	return Predicted == SortedComponentTypePaths(ComponentData);
}

void USeinEntityComponent::BakeAuthoredDataComponents(bool bInteractive)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	TArray<const USeinDataComponent*> AuthoringComponents;
	if (IsTemplate())
	{
		AActor::GetActorClassDefaultComponents<USeinDataComponent>(
			Owner->GetClass(), AuthoringComponents);
	}
	else
	{
		TArray<USeinDataComponent*> OwnedComponents;
		Owner->GetComponents<USeinDataComponent>(OwnedComponents);
		AuthoringComponents.Append(OwnedComponents);
	}

	// Fully inert when nothing is authored and nothing was ever baked — the
	// pre-migration authoring model must not pay for the prototype.
	if (AuthoringComponents.IsEmpty() && BakedComponentDataTypes.IsEmpty())
	{
		return;
	}

	// Deterministic bake order (entry order in ComponentData is semantically
	// irrelevant, but stable output keeps diffs and digests quiet).
	AuthoringComponents.Sort(
		[](const USeinDataComponent& Left, const USeinDataComponent& Right)
	{
		const UScriptStruct* LeftStruct = Left.GetPayloadStruct();
		const UScriptStruct* RightStruct = Right.GetPayloadStruct();
		return (LeftStruct ? LeftStruct->GetPathName() : FString())
			< (RightStruct ? RightStruct->GetPathName() : FString());
	});

	if (bInteractive)
	{
		// Same undo-coherence rule as NotifyAuthoringComponentEdited.
		Modify();
		BeginComponentDataEdit();
	}

	auto RemoveEntryOfType = [this](const FString& TypePath)
	{
		ComponentData.RemoveAll([&TypePath](const FInstancedStruct& Entry)
		{
			return Entry.IsValid()
				&& Entry.GetScriptStruct()->GetPathName() == TypePath;
		});
	};

	bool bAllPayloadsResolved = true;
	TArray<FString> NewLedger;
	for (const USeinDataComponent* Component : AuthoringComponents)
	{
		const UScriptStruct* PayloadType =
			Component ? Component->GetPayloadStruct() : nullptr;
		if (!PayloadType)
		{
			// A Blueprint component whose payload struct has not synced yet
			// (first compile pending, or its variables are transiently gone).
			// Its previously managed entry must NOT be treated as an orphan
			// below — deleting authored data on a transient authoring state
			// is the failure mode, so management is deferred instead.
			bAllPayloadsResolved = false;
			continue;
		}
		const FString TypePath = PayloadType->GetPathName();
		if (NewLedger.Contains(TypePath))
		{
			UE_LOG(LogSeinEntityComp, Warning,
				TEXT("%s: multiple authoring components produce payload type %s — the last one in bake order wins; remove the duplicate."),
				*GetPathName(), *TypePath);
		}
		NewLedger.AddUnique(TypePath);

		if (!BakedComponentDataTypes.Contains(TypePath)
			&& FindComponentDataEntryByPath(ComponentData, TypePath))
		{
			UE_LOG(LogSeinEntityComp, Warning,
				TEXT("%s: authoring component %s now manages ComponentData type %s; the existing hand-authored entry was replaced by the component's values."),
				*GetPathName(), *Component->GetPathName(), *TypePath);
		}

		if (!Component->bInjectionEnabled)
		{
			RemoveEntryOfType(TypePath);
			continue;
		}

		FInstancedStruct Payload;
		if (!Component->WritePayload(Payload) || !Payload.IsValid())
		{
			UE_LOG(LogSeinEntityComp, Warning,
				TEXT("%s: authoring component %s failed to write its payload; its baked entry was left untouched."),
				*GetPathName(), *Component->GetPathName());
			continue;
		}

		if (FInstancedStruct* Existing =
			FindMutableComponentDataEntryByPath(ComponentData, TypePath))
		{
			*Existing = MoveTemp(Payload);
		}
		else
		{
			ComponentData.Add(MoveTemp(Payload));
		}
	}

	// Types the bake managed before whose authoring component is GONE leave
	// with their component — loudly. Deferred entirely while any component's
	// payload is unresolved: a half-synced authoring set cannot be told apart
	// from a deleted one, and destroying authored data on that ambiguity is
	// exactly the transient-state failure this module's gates warn about.
	if (bAllPayloadsResolved)
	{
		for (const FString& PreviousType : BakedComponentDataTypes)
		{
			if (!NewLedger.Contains(PreviousType))
			{
				UE_LOG(LogSeinEntityComp, Warning,
					TEXT("%s: authoring component for managed type %s is gone; its baked ComponentData entry was removed."),
					*GetPathName(), *PreviousType);
				RemoveEntryOfType(PreviousType);
			}
		}
		NewLedger.Sort();
		BakedComponentDataTypes = MoveTemp(NewLedger);
	}
	else
	{
		for (const FString& PreviousType : BakedComponentDataTypes)
		{
			NewLedger.AddUnique(PreviousType);
		}
		NewLedger.Sort();
		BakedComponentDataTypes = MoveTemp(NewLedger);
	}

	if (bInteractive)
	{
		EndComponentDataEdit();
	}
}

void USeinEntityComponent::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);
	// Cook loads already-saved packages whose arrays were baked at editor
	// save time; mutating during cook saves only trips the cooker's
	// modified-during-PreSave determinism diagnostics.
	if (SaveContext.IsCooking())
	{
		return;
	}
	// Silent refresh: interactive edits already flowed through the bracketed
	// pipeline; this guarantees saved packages carry the authoring
	// components' current values even when an edit path was missed.
	BakeAuthoredDataComponents(/*bInteractive=*/false);
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


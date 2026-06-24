/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinBalanceProfile.h
 * @date:    6/23/2026
 * @author:  RJ Macklem
 * @brief:   Editor-authoring data asset that scopes a balance-tuning pass:
 *           which entity classes to target, which component structs become
 *           tunable columns, and where the generated DataTable lives.
 *
 *           This asset is editor tooling only — it is never read by the
 *           running sim. The unit Blueprints remain the source of truth; the
 *           generated table is a bulk EDITING VIEW that gathers their authored
 *           ComponentData and (later) pushes edits back. See the project-root
 *           `Balance_Table_Plan.md` for the full design + phase ladder.
 *
 *           Phase A (this file): targeting + target resolution only. Column
 *           synthesis (Gather) and write-back (Push) are wired as no-op
 *           buttons here and implemented in later phases.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"   // FDirectoryPath
#include "SeinBalanceProfile.generated.h"

class ASeinActor;
class UDataTable;

/**
 * Scope + output config for one balance table. A designer points it at a class
 * hierarchy, optionally narrows the components tracked, and generates a flat
 * DataTable for tuning. Make one profile per concern (e.g. "vehicle movement",
 * "infantry combat") to keep each table dense.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Balance Data"))
class SEINARTSCOREENTITY_API USeinBalanceProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	// =========================================================================
	// Targeting
	// =========================================================================

	/** Root classes to include. Every concrete ASeinActor subclass under each
	 *  root (loaded or not) is matched — opt in a parent, its children appear.
	 *  Native or Blueprint roots both work. */
	UPROPERTY(EditAnywhere, Category = "Targeting", meta = (DisplayName = "Included Roots"))
	TArray<TSoftClassPtr<ASeinActor>> IncludedRoots;

	/** Classes to exclude. Each entry removes that class AND its whole subtree
	 *  from the matched set, even if a broader Included Root would have caught it. */
	UPROPERTY(EditAnywhere, Category = "Targeting", meta = (DisplayName = "Excluded Classes"))
	TArray<TSoftClassPtr<ASeinActor>> ExcludedClasses;

	/** Include classes flagged Abstract. Off by default — abstract bases carry
	 *  no shippable tuning of their own. */
	UPROPERTY(EditAnywhere, Category = "Targeting", meta = (DisplayName = "Include Abstract"))
	bool bIncludeAbstract = false;

	// =========================================================================
	// Tracking
	// =========================================================================

	/** Component structs whose fields become tunable columns. Empty = track
	 *  every eligible component found on the matched entities.
	 *
	 *  NOTE (Phase A): the picker is restricted to FSeinComponent descendants
	 *  via MetaStruct, which covers the native components. Designer-authored UDS
	 *  components (which don't reliably report IsChildOf — UE clears UDS
	 *  super-struct on every compile) and the SeinSubData exclusion are handled
	 *  by the eligibility-filtered struct viewer that lands with column
	 *  synthesis (Phase B) — see SeinComponentEligibility::IsEntityComponentStruct. */
	UPROPERTY(EditAnywhere, Category = "Tracking",
		meta = (DisplayName = "Tracked Components",
			MetaStruct = "/Script/SeinARTSCoreEntity.SeinComponent"))
	TArray<TObjectPtr<UScriptStruct>> TrackedComponents;

	// =========================================================================
	// Output
	// =========================================================================

	/** Folder the generated DataTable is written to. Leave empty to generate
	 *  alongside this profile asset. */
	UPROPERTY(EditAnywhere, Category = "Output",
		meta = (DisplayName = "Output Directory", ContentDir))
	FDirectoryPath OutputDir;

	/** The DataTable this profile generates. Filled on first Gather. */
	UPROPERTY(VisibleAnywhere, Category = "Output", meta = (DisplayName = "Generated Table"))
	TSoftObjectPtr<UDataTable> GeneratedTable;

#if WITH_EDITOR
	/** Resolve the concrete ASeinActor subclasses this profile targets: the
	 *  union of each Included Root's subtree, minus every Excluded class and its
	 *  subtree, minus abstract classes (unless bIncludeAbstract). Matched
	 *  Blueprint classes are loaded so their flags can be read; result is sorted
	 *  by class name. Editor-only — drives the Details-panel preview and (later)
	 *  the Gather/Push passes. */
	void ResolveTargetClasses(TArray<UClass*>& OutClasses) const;
#endif
};

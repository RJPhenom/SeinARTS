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
 *           ComponentData and pushes edits back.
 *
 *           The editor module owns target preview, destructive Gather,
 *           write-back Push, and source-drift checking.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"   // FDirectoryPath
#include "SeinBalanceProfile.generated.h"

class ASeinActor;
class USeinAbility;
class UDataTable;

/** What a balance profile targets — units (ASeinActor subclasses, tuning their components) or
 *  abilities (USeinAbility subclasses, tuning cost / cooldown / range). Both activation cost and
 *  production/build cost are ability `ResourceCost`s, so the ability table tunes them together. */
UENUM()
enum class ESeinBalanceTargetKind : uint8
{
	Entities    UMETA(DisplayName = "Entities (units / buildings)"),
	Abilities   UMETA(DisplayName = "Abilities (cost / cooldown / range)"),
};

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

	/** What this profile tunes — entity components, or ability cost/cooldown/range. The relevant root
	 *  fields below show/hide to match. */
	UPROPERTY(EditAnywhere, Category = "Targeting", meta = (DisplayName = "Target Kind"))
	ESeinBalanceTargetKind TargetKind = ESeinBalanceTargetKind::Entities;

	/** (Entities) Root classes to include. Every concrete ASeinActor subclass under each root
	 *  (loaded or not) is matched — opt in a parent, its children appear. Native or BP roots both work. */
	UPROPERTY(EditAnywhere, Category = "Targeting", meta = (DisplayName = "Included Roots",
		EditCondition = "TargetKind == ESeinBalanceTargetKind::Entities", EditConditionHides))
	TArray<TSoftClassPtr<ASeinActor>> IncludedRoots;

	/** (Entities) Classes to exclude — removes that class AND its whole subtree from the matched set. */
	UPROPERTY(EditAnywhere, Category = "Targeting", meta = (DisplayName = "Excluded Classes",
		EditCondition = "TargetKind == ESeinBalanceTargetKind::Entities", EditConditionHides))
	TArray<TSoftClassPtr<ASeinActor>> ExcludedClasses;

	/** (Abilities) Root ability classes — every concrete USeinAbility subclass under each root is
	 *  matched (one row per ability). */
	UPROPERTY(EditAnywhere, Category = "Targeting", meta = (DisplayName = "Ability Roots",
		EditCondition = "TargetKind == ESeinBalanceTargetKind::Abilities", EditConditionHides))
	TArray<TSoftClassPtr<USeinAbility>> AbilityRoots;

	/** (Abilities) Ability classes to exclude (class + subtree). */
	UPROPERTY(EditAnywhere, Category = "Targeting", meta = (DisplayName = "Excluded Abilities",
		EditCondition = "TargetKind == ESeinBalanceTargetKind::Abilities", EditConditionHides))
	TArray<TSoftClassPtr<USeinAbility>> ExcludedAbilities;

	/** Include classes flagged Abstract. Off by default — abstract bases carry no shippable tuning. */
	UPROPERTY(EditAnywhere, Category = "Targeting", meta = (DisplayName = "Include Abstract"))
	bool bIncludeAbstract = false;

	// =========================================================================
	// Tracking
	// =========================================================================

	/** Component structs whose fields become tunable columns. Empty = track
	 *  every eligible component found on the matched entities. The details-panel
	 *  picker accepts both native FSeinComponent descendants and eligible
	 *  designer-authored UDS components found on those entities. Per-class
	 *  sub-data remains excluded. */
	UPROPERTY(EditAnywhere, Category = "Tracking",
		meta = (DisplayName = "Tracked Components",
			MetaStruct = "/Script/SeinARTSCoreEntity.SeinComponent",
			EditCondition = "TargetKind == ESeinBalanceTargetKind::Entities", EditConditionHides))
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
	/** Resolve the concrete subclasses this profile targets — ASeinActor subclasses for
	 *  TargetKind::Entities, USeinAbility subclasses for TargetKind::Abilities: the union of each
	 *  root's subtree, minus every excluded class + subtree, minus abstract (unless bIncludeAbstract).
	 *  Matched Blueprint classes are loaded so their flags can be read; sorted by name. Editor-only. */
	void ResolveTargetClasses(TArray<UClass*>& OutClasses) const;
#endif
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBalanceColumn.h
 * @brief   The Phase-B column-provider spine for the balance-table generator.
 *
 *          A column source provider describes a set of table columns (one per tunable
 *          field) and reads each cell off a target entity's authored ComponentData.
 *          The `ESeinBalanceColumnKind` discriminator is the seam that lets later phases
 *          (Nested sub-data, Ability-derived cost) add providers without reshaping the
 *          Gather/Push engine. Editor-only.
 */

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"   // FEdGraphPinType
#include "GameplayTagContainer.h" // FGameplayTag (AbilityCost columns)

class USeinBalanceProfile;

/** Where a balance column reads its value from. Phase B ships Component + Identity. */
enum class ESeinBalanceColumnKind : uint8
{
	Component,         // a deterministic field on a tracked entity component
	Identity,          // DisplayName / IdentityTag — informational row labels
	NestedComponent,   // a field inside a component's FInstancedStruct sub-data (e.g. MovementClassData)
	AbilityField,      // a deterministic field on a targeted USeinAbility class CDO (cooldown, range, ...)
	AbilityCost,       // one resource of a USeinAbility's ResourceCost map, flattened to a column
};

/**
 * One column of the balance table. The persistent half (DisplayName / PinType / SourceKey)
 * defines the row-UDS field; the transient half (ComponentStruct / SourceProp) is filled in
 * DescribeColumns and consumed by ReadInto within the SAME Gather pass (no GC between).
 */
struct FSeinBalanceColumn
{
	/** Namespaced label — becomes the UDS field's friendly name AND the table column header. */
	FString DisplayName;

	/** Pin type cloned from the source FProperty (so FFixedPoint stays FFixedPoint, etc.). */
	FEdGraphPinType PinType;

	/** Stable identity, stamped in the UDS field metadata, used for rename-safe re-sync. */
	FString SourceKey;

	ESeinBalanceColumnKind Kind = ESeinBalanceColumnKind::Component;

	/** Component the source field lives on (or, for NestedComponent, the component that owns the
	 *  FInstancedStruct sub-data). */
	TWeakObjectPtr<UScriptStruct> ComponentStruct;

	/** NestedComponent only: the component's FInstancedStruct field (e.g. MovementClassData) and the
	 *  inner sub-data type this column reads from. SourceProp is then the field on InnerStruct.
	 *  Transient — valid only for the lifetime of one Gather/Push call. */
	const FProperty* NestedContainerProp = nullptr;
	TWeakObjectPtr<UScriptStruct> InnerStruct;

	/** AbilityCost only: the resource whose amount this column reads/writes in the ability's
	 *  ResourceCost map. */
	FGameplayTag ResourceTag;

	/** The source field. Transient — valid only for the lifetime of one Gather/Push call. */
	const FProperty* SourceProp = nullptr;

	/** True if the source field is an FFixedPoint surfaced as a plain float column for readability
	 *  (the grid renders a raw FFixedPoint as `{ "Value": <int64> }`). Read converts via ToFloat;
	 *  Push converts back via FromFloat — the same editor-only conversion FSeinFixedPointDetails uses. */
	bool bConvertFixedToFloat = false;
};

/** Result of one Push write-back cell. */
enum class ESeinBalanceWriteResult : uint8
{
	Wrote,      // a changed value was written back
	Unchanged,  // cell already equals the authored value — skipped, no perturbation
	Skipped,    // couldn't write: component not on this unit, type mismatch, or display-only column
};

/** A source of balance columns. Phase B ships Component + Identity; Phase E adds more. */
class ISeinBalanceColumnProvider
{
public:
	virtual ~ISeinBalanceColumnProvider() = default;

	/** Append the columns this provider contributes for the matched targets. */
	virtual void DescribeColumns(const TArray<UClass*>& Targets, const USeinBalanceProfile& Profile,
		TArray<FSeinBalanceColumn>& OutColumns) const = 0;

	/** Copy the cell value for (Target, Column) into DestPtr (a row-UDS field of Column.PinType).
	 *  Returns false if the target lacks the source — the caller leaves the cell at default. */
	virtual bool ReadInto(const UClass* Target, const FSeinBalanceColumn& Column,
		const FProperty* DestProp, void* DestPtr) const = 0;

	/** Write a row cell (CellProp/CellPtr, a row-UDS field) BACK into the target's authored
	 *  ComponentData on the BP CDO. Returns true iff a CHANGED value was written (unchanged cells
	 *  are skipped so untouched fixed-point values aren't perturbed by the float round-trip).
	 *  Display-only providers (Identity) return false. Editor-only; Phase C write-back. */
	virtual ESeinBalanceWriteResult WriteFrom(const UClass* Target, const FSeinBalanceColumn& Column,
		const FProperty* CellProp, const void* CellPtr) const = 0;
};

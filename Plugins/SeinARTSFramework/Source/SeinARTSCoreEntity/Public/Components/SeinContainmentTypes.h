/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinContainmentTypes.h
 * @brief   Shared enums + small structs for the Relationships primitive
 *          (DESIGN §14). Kept in its own header so component headers don't
 *          need to include each other.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Types/Vector.h"
#include "SeinContainmentTypes.generated.h"

/**
 * How contained occupants interact with the sim while inside a container.
 * DESIGN §14 "Visibility modes".
 */
UENUM(BlueprintType)
enum class ESeinContainmentVisibility : uint8
{
	/** Occupants excluded from spatial queries + vision stamping. Classic
	 *  garrison / transport-in-transit. Abilities still tick if the designer
	 *  wants them to (the component storage is still live). */
	Hidden,
	/** Occupants tick + appear in spatial queries at container + slot offset.
	 *  Classic mounted-crew / hero-attached-to-regiment. */
	PositionedRelative,
	/** Hidden for targeting but exposed for specific ability-triggered fire
	 *  (garrisoned infantry returning fire). Framework treats as Hidden for
	 *  spatial queries; designer handles the exposed firing via abilities. */
	Partial,
};

/**
 * One named attachment slot on a container (DESIGN §14).
 * Slot tags drive designer-scripted behavior (Slot.Driver enables Move,
 * Slot.Gunner operates the vehicle's weapon, etc.).
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinAttachmentSlotDef
{
	GENERATED_BODY()

	/** Slot identity tag (e.g., SeinARTS.Slot.Driver). Designers extend the
	 *  vocabulary under their own root. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Attachment")
	FGameplayTag SlotTag;

	/** Position relative to the container's transform (rotates with facing). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Attachment")
	FFixedVector LocalOffset;

	/** Tag query an entity must satisfy to occupy this slot. Empty = any entity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Attachment")
	FGameplayTagQuery AcceptedEntityQuery;
};

/**
 * One entry in a flattened containment tree (DESIGN §15). UHT does not allow
 * a USTRUCT to contain `TArray<SelfType>` as a UPROPERTY, so we serialize the
 * hierarchy as a depth-first pre-ordered list of entries, each carrying its
 * depth + parent index. Consumers reconstruct the tree BP-side by walking
 * `Entries` sequentially and grouping children under the most recent shallower
 * parent.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinContainmentTreeEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Containment")
	FSeinEntityHandle Entity;

	/** 0 = the root passed to BuildContainmentTree; 1 = its immediate occupants, etc. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Containment")
	int32 Depth = 0;

	/** Index into the same Entries array of this node's parent, or -1 for the root. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Containment")
	int32 ParentIndex = INDEX_NONE;
};

/**
 * Flattened containment tree view. Returned by
 * `USeinContainmentBPFL::SeinGetContainmentTree` for drill-down UI.
 * Flat alternatives (`SeinGetOccupants`, `SeinGetAllNestedOccupants`) remain
 * as-is for callers that don't need the hierarchy.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinContainmentTree
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Containment")
	TArray<FSeinContainmentTreeEntry> Entries;
};

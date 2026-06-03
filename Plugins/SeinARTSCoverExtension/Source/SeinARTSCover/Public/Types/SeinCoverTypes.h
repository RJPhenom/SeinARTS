/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverTypes.h
 * @brief   Wire-format types for the cover system (DESIGN §future "Cover").
 *
 *          Cover qualities are gameplay tags so designers can extend freely.
 *          Convention (not enforced — pick any tag root that suits the project):
 *            SeinARTS.Cover.Heavy     — sandbags, walls, building corners
 *            SeinARTS.Cover.Light     — fences, low fences, craters
 *            SeinARTS.Cover.Negative  — roads, exposed lanes (takes MORE damage)
 *
 *          Quality lives ONCE per provider (`FSeinCoverComponent::QualityTag`)
 *          and applies uniformly to all of that provider's slots + area volume.
 *          Designers wanting mixed qualities on one actor compose multiple cover
 *          provider components, one per tier.
 *
 *          A unit may simultaneously match multiple cover contexts — e.g. behind
 *          a sandbag (directional heavy) INSIDE a foxhole (omnidirectional heavy).
 *          QueryCoverAt() returns the full list so combat scripts can evaluate
 *          each independently against the incoming shot direction.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinCoverTypes.generated.h"

/** Shape of an area-based cover volume. None = provider is slot-only (no area
 *  cover). Phase 2a supports Box and Sphere; Polygon + Cylinder are future
 *  work when designers ask for non-axis-aligned cover regions. */
UENUM(BlueprintType)
enum class ESeinCoverAreaShape : uint8
{
	/** No area volume — provider is slot-based only (edge cover like sandbags). */
	None,
	/** Axis-aligned box in the provider's local space. LocalExtents = half-size. */
	Box,
	/** Sphere centered on the provider. LocalExtents.X is the radius (Y/Z ignored). */
	Sphere,
};

/** How the slot generator distributes slots when the designer clicks
 *  "Generate Slots" in the cover details panel. Both modes read the
 *  `FSeinCoverComponent::Area` field to derive their geometry — the
 *  generator has no other geometric source. */
UENUM(BlueprintType)
enum class ESeinCoverGenerateMode : uint8
{
	/** Walk the PERIMETER of `Area`'s box — sandbag walls, building edges,
	 *  low fences. N slots distributed evenly around the full perimeter,
	 *  inset OUTWARD by `GenerateSlotInsetUU` so units stand behind the
	 *  wall body. Requires `Area.Shape == Box`. */
	Edge UMETA(DisplayName = "Edge — slots around perimeter"),

	/** Fill the INTERIOR of `Area` (Box or Sphere) with concentric inset
	 *  rings from outside to center — foxholes, craters, room interiors.
	 *  Each ring inset by `GenerateSlotInsetUU` from the prior; slots-per-
	 *  ring proportional to perimeter so density stays roughly uniform. */
	Area UMETA(DisplayName = "Area — slots filling interior"),
};

// Slots are pure positions — `FSeinCoverComponent::Slots` is a
// `TArray<FFixedVector>` (each entry is a local-space slot position relative
// to the provider's actor transform). No per-slot direction, facing, or
// quality data — directionality is a provider-level property (see
// `FSeinCoverComponent::bIsDirectional`) computed at runtime via
// `USeinCoverBPFL::SeinGetCoverDirection`; quality is a single provider-
// level tag (`FSeinCoverComponent::QualityTag`). The flat-vector array
// keeps the details panel clean (each slot is one editable FFixedVector,
// not a nested struct with redundant scalar fields).

/**
 * Area cover volume — omnidirectional cover applied to any unit inside the
 * volume. Used for foxholes, craters, ditches (positive), and exposed lanes
 * like roads (negative). Decoupled from slot list — a provider can be slots-
 * only (edge cover), area-only (foxhole), or both (a foxhole next to a
 * sandbag wall).
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOVER_API FSeinCoverArea
{
	GENERATED_BODY()

	/** Volume shape. None = provider has no area cover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Cover|Area")
	ESeinCoverAreaShape Shape = ESeinCoverAreaShape::None;

	/** For Box: half-extents in the provider's local space (full box is
	 *  2 × LocalExtents on each axis). For Sphere: LocalExtents.X is the
	 *  radius, Y/Z ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Cover|Area",
		meta = (EditCondition = "Shape != ESeinCoverAreaShape::None"))
	FFixedVector LocalExtents = FFixedVector(FFixedPoint::FromInt(200), FFixedPoint::FromInt(200), FFixedPoint::FromInt(100));
};

/**
 * One cover source active at a queried point. Returned by `USeinCoverSystem::
 * QueryCoverAt`; combat scripts iterate the returned array and evaluate each
 * context against the incoming shot direction to compute total damage modifier
 * (a unit may legitimately be in multiple cover sources at once — sandbag +
 * foxhole layer cleanly under this model).
 *
 * Transient — not stored anywhere, just a query result. Not marked
 * `SeinDeterministic` because instances live for the duration of a query call
 * only, never enter sim storage or the state hash.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOVER_API FSeinCoverContext
{
	GENERATED_BODY()

	/** Cover quality tag at this source. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Cover")
	FGameplayTag QualityTag;

	/** Entity handle of the cover provider supplying this context. Combat
	 *  scripts can read further state from the provider entity if needed
	 *  (e.g. destruction state, owner tags, ability cooldowns) and pass
	 *  the handle to `USeinCoverBPFL::SeinGetCoverDirection` when
	 *  `bIsDirectional == true` to compute the per-shot cover vector. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Cover")
	FSeinEntityHandle ProviderHandle;

	/** True if this cover is directional (sandbag wall, low fence). Combat
	 *  damage formulas should call `SeinGetCoverDirection(EntityPos,
	 *  ProviderHandle)` to get the runtime cover vector and dot it against
	 *  the shot's incoming-from direction: +1 = fully covered, -1 = fully
	 *  flanked, smooth blend between.
	 *
	 *  False for area cover (foxhole, crater, negative-cover road) —
	 *  protection is omnidirectional and the direction helper isn't needed.
	 *  Set from `FSeinCoverComponent::bIsDirectional`. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Cover")
	bool bIsDirectional = false;
};

/**
 * One slot returned by `USeinCoverSystem::FindNearbySlots`. Carries the slot's
 * resolved world position (computed via the provider's transform) plus the
 * quality + identifying handles for downstream consumers — cover-aware broker
 * resolvers use these to snap eligible squad members onto cover when issuing
 * move commands.
 *
 * Distinct from FSeinCoverContext because the use case is different: cover
 * contexts answer "what cover is at this point" (consumer already knows the
 * world point); slot candidates answer "what cover slots are near this point"
 * (the world position is part of the answer).
 */
USTRUCT(BlueprintType)
struct SEINARTSCOVER_API FSeinCoverSlotCandidate
{
	GENERATED_BODY()

	/** World position of the slot, transformed by the provider's actor pose. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Cover")
	FFixedVector WorldPosition;

	/** World-space "outward from cover body" direction at this slot, used
	 *  by the cover-aware snap resolver's wrong-side bias. Computed at
	 *  `FindNearbySlots` time:
	 *    - For directional providers (sandbag walls), derived from the
	 *      provider actor's `SeinExtents` body — points from the nearest
	 *      body surface to the slot. Robust regardless of provider pivot
	 *      placement.
	 *    - For non-directional providers (foxholes), set to zero — the
	 *      resolver skips the wrong-side check entirely, since there's no
	 *      meaningful "preferred side" for omni cover.
	 *
	 *  Independent of combat damage math: combat uses
	 *  `USeinCoverBPFL::SeinGetCoverDirection(EntityPos, ProviderHandle)`
	 *  computed per shot, not this snap-time precomputed vector. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Cover")
	FFixedVector WorldProtectedFromDirection;

	/** Cover quality tag of the provider this slot belongs to. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Cover")
	FGameplayTag QualityTag;

	/** Cover provider entity that owns this slot. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Cover")
	FSeinEntityHandle ProviderHandle;

	/** Slot index inside the provider's `Slots` array, for direct lookup
	 *  back into the authored slot data (LocalFacing, LocalProtectedFromDirection). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Cover")
	int32 SlotIndex = INDEX_NONE;

	/** Footprint radius of this slot (from the provider's `SlotRadius`). The
	 *  resolution that produced this candidate already used it for extents-reject,
	 *  quality-by-area, and slot dedup; carried through so downstream consumers
	 *  (preview decals, snap) know the unit's standing footprint at the slot. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Cover")
	FFixedPoint Radius = FFixedPoint::Zero;
};

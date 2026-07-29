/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverComponent.h
 * @brief   Sim-side cover component. Lives in deterministic entity storage
 *          after the host actor spawns; the cover system queries it on cover
 *          lookups.
 *
 *          Authored on the entity bridge's ComponentData array. Renamed from
 *          FSeinCoverProviderComponent (2026-05-19) — every cover entity is a
 *          provider by definition, the "Provider" qualifier was noise.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SeinComponent.h"
#include "Types/SeinCoverTypes.h"
#include "SeinCoverComponent.generated.h"

struct FSeinExtentsShape;

/**
 * Sim component for cover-providing entities.
 *
 * Carries the slot list + area volume that the cover system queries against.
 * One provider entity can contribute BOTH edge cover (via Slots) AND area
 * cover (via Area) — e.g. a sandbag wall behind a foxhole authored as one
 * provider with three sandbag slots + a sphere area for the foxhole.
 *
 * The provider's world transform comes from the host entity's transform —
 * slot and area data is in the provider's local space so a movable provider
 * (engineer-deployed sandbag, vehicle wreck) updates its cover footprint as
 * the entity moves.
 *
 * One uniform `QualityTag` per provider — every slot AND the optional area
 * volume report the same cover quality at query time. Designers wanting mixed
 * qualities (e.g. heavy sandbag corners + light fence middle along one wall)
 * compose multiple cover providers on the same actor, one per quality tier.
 * Cuts authoring noise (no per-slot quality stamping) and prevents the
 * "I changed the quality on the component but the slots still have the old
 * tag" footgun the per-slot model created.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOVER_API FSeinCoverComponent : public FSeinComponent
{
	GENERATED_BODY()

	/** Cover quality tag reported by every cover context this provider
	 *  contributes — typically SeinARTS.Cover.Heavy / Light / Negative or a
	 *  designer-defined extension. One tag per provider; compose multiple
	 *  providers if you need mixed qualities on one actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Cover",
		meta = (Categories = "SeinARTS.Cover"))
	FGameplayTag QualityTag;

	/** Whether this provider's cover is direction-dependent (sandbag wall,
	 *  low fence — true) or omnidirectional (foxhole, crater — false).
	 *
	 *  Combat damage formulas check this flag on the returned
	 *  `FSeinCoverContext`: when true, they should call
	 *  `USeinCoverBPFL::SeinGetCoverDirection(EntityPos, ProviderHandle)`
	 *  and dot the result against the shot's incoming-from direction to
	 *  compute a [-1, +1] cover-effectiveness score. When false, they apply
	 *  the quality modifier unconditionally (full omni protection).
	 *
	 *  Set automatically by the slot generator: Edge mode → true, Area mode
	 *  → false. Designer can flip the bool for hand-authored slot layouts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Cover")
	bool bIsDirectional = false;

	/** Discrete cover slots — formation snap targets. Each entry is a
	 *  local-space position relative to the provider's actor transform;
	 *  the cover-aware broker resolver picks the nearest slot for each
	 *  cover-eligible squad member when issuing a move order.
	 *
	 *  Pure positions — no per-slot direction, facing, or quality data.
	 *  Cover PROTECTION is volume-based (granted by being inside `Area`,
	 *  not by standing on a slot), so walking between slots preserves
	 *  cover. Directionality is a provider-level property
	 *  (`bIsDirectional`), and combat math computes the runtime cover
	 *  vector via `USeinCoverBPFL::SeinGetCoverDirection` against the
	 *  actor's `SeinExtents` body — no stale per-slot direction to
	 *  maintain. Empty = no snap targets (area-only provider). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Cover")
	TArray<FFixedVector> Slots;

	/** Cover-zone volume — units inside this volume receive a cover context
	 *  from this provider at query time. Distinct from the actor's
	 *  `SeinExtents` component (which describes the actor's physical body
	 *  for nav / fog / debug); the cover area is the authored cover
	 *  FOOTPRINT, which can extend beyond the physical body (a sandbag
	 *  wall's cover area sits behind the wall on the protected side, not
	 *  on the wall itself).
	 *
	 *  Shape = None disables this provider entirely (no cover contribution).
	 *  Quality + directionality come from the provider's `QualityTag` and
	 *  `bIsDirectional`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Cover")
	FSeinCoverArea Area;

	// ========================================================================
	// Slot generation — editor-time authoring helpers
	// ========================================================================
	//
	// Configure the parameters below and click "Generate Slots" in the details
	// panel (injected by `FSeinCoverComponentDetails` in the SeinARTSCoverEditor
	// module) to fill the `Slots` array procedurally. Replaces hand-placement
	// for long sandbag walls / foxhole interiors / etc. Designers can still
	// edit individual slots afterward — this is a starting-point convenience.
	//
	// Two geometric sources, picked by GenerateMode:
	//   - Edge: wraps the sibling FSeinExtentsComponent's first Box shape
	//     (the wall body). Slots sit OUTSIDE that body by `GenerateSlotInsetUU`
	//     — on the protected side, inside the cover Area but outside the wall.
	//     Designer needs an Extents entry with a Box shape authored.
	//   - Area: fills `Area`'s interior with concentric inset rings. Reads
	//     `Area.Shape` + `Area.LocalExtents` directly; no sibling needed.

#if WITH_EDITORONLY_DATA
	/** Distribution mode — Edge (around `Area`'s perimeter) or Area (interior). */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Cover|Generate")
	ESeinCoverGenerateMode GenerateMode = ESeinCoverGenerateMode::Edge;

	/** Number of slots to generate. Total count distributed around the
	 *  perimeter (Edge mode) or across the interior rings (Area mode). */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Cover|Generate",
		meta = (ClampMin = "1", UIMin = "1", UIMax = "64"))
	int32 GenerateSlotCount = 8;
#endif

	/** Footprint radius of a unit standing on a slot, in world units. This is the
	 *  circle used at RUNTIME (in the cover system's slot resolution) to: (a) reject
	 *  a slot whose circle overlaps a wall's solid Extents, (b) tag a slot with the
	 *  best cover quality whose Area its circle overlaps, and (c) dedup slots whose
	 *  circles overlap each other (best quality wins). Default ~infantry footprint.
	 *  (Lives here, above the inset, so authors tune slot size + spacing together.) */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Cover|Generate",
		meta = (ClampMin = "0.0"))
	FFixedPoint SlotRadius = FFixedPoint::FromInt(50);

#if WITH_EDITORONLY_DATA
	/** Distance from the wall body edge (Edge mode) or area edge (Area mode)
	 *  to the slot's CENTER, in world units.
	 *
	 *  Edge mode: slot centers are placed this far OUTSIDE the sibling
	 *  Extents body — units stand on the protected side behind the wall, with
	 *  this gap between their pivot and the wall surface. Default 60cm gives
	 *  visible slot rings clear of the wall mesh.
	 *
	 *  Area mode: distance between concentric inset rings INSIDE the area,
	 *  AND the inset of the outermost ring from the area edge. Same value
	 *  reused so designers have one knob to tune density. In scatter mode,
	 *  doubles as the minimum slot-to-slot center distance — controls how
	 *  loosely the random points are packed. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Cover|Generate",
		meta = (ClampMin = "0.0"))
	float GenerateSlotInsetUU = 60.f;

	/** Randomize slot positions instead of evenly distributing them.
	 *
	 *  Edge mode: slots stay ON the outer perimeter at body-extents +
	 *  GenerateSlotInsetUU, but their spacing along the perimeter is
	 *  jittered (some pairs closer, some farther) — gives a less
	 *  regimented look on long sandbag walls / wreckage piles.
	 *
	 *  Area mode: slots are placed at random positions INSIDE the area
	 *  with a minimum slot-to-slot center distance of `GenerateSlotInsetUU`
	 *  (no overlaps) and kept that far from the area edge as well — gives
	 *  scattered foxhole / crater interiors instead of concentric rings.
	 *
	 *  Off by default — the regular ring/perimeter layout is the
	 *  predictable baseline. Each click produces a fresh random layout
	 *  (non-seeded), so designers can re-roll until they're happy. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Cover|Generate")
	bool bScatterSlots = false;

	/** Procedurally regenerate `Slots` (and stamp `bIsDirectional`) from the
	 *  `GenerateMode` + geometric source. Replaces the array wholesale on
	 *  each call. Driven by the "Generate Slots" button injected into the
	 *  details panel by `FSeinCoverComponentDetails` (SeinARTSCoverEditor).
	 *
	 *  Behavior:
	 *    - Edge mode (`bIsDirectional = true`): walks the perimeter of
	 *      `OptionalEdgeShape` (a sibling FSeinExtentsComponent's Box shape)
	 *      at body-half-extents + GenerateSlotInsetUU outward. Slots end up
	 *      inside the cover Area but outside the wall body. Bails with a
	 *      warning when no shape is passed or the shape isn't a Box.
	 *    - Area mode (`bIsDirectional = false`): fills `Area`'s interior
	 *      with concentric inset rings (Box) or concentric circles (Sphere).
	 *      Bails with a warning when `Area.Shape == None`. `OptionalEdgeShape`
	 *      is ignored.
	 *
	 *  Pure — no actor lookup, no sim-state touch. Safe to call off the sim
	 *  tick. The caller (cover details panel) resolves the sibling Extents
	 *  shape from the owning USeinEntityComponent's ComponentData array. */
	void GenerateSlots(const FSeinExtentsShape* OptionalEdgeShape = nullptr);
#endif
};

FORCEINLINE uint32 GetTypeHash(const FSeinCoverComponent& Data)
{
	uint32 Hash = GetTypeHash(static_cast<uint8>(Data.Area.Shape));
	Hash = HashCombine(Hash, GetTypeHash(Data.Slots.Num()));
	Hash = HashCombine(Hash, GetTypeHash(Data.QualityTag));
	Hash = HashCombine(Hash, GetTypeHash(Data.SlotRadius));
	return Hash;
}

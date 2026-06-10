/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLevelLoS.h
 * @brief   Deterministic line-of-sight / occlusion query over baked level data (CP1.1, D14).
 *
 *          The combat-facing environment-collision service — projectile-vs-terrain,
 *          AoE-vs-wall, targeting line-of-sight — as its OWN thin interface, so combat
 *          does NOT depend on Fog of War (planning/Decisions.md D14, resolved Q3). The
 *          default impl reads the shared height field + the FoW layer's occluder channel
 *          (the same deterministic data FoW vision already uses), all in fixed-point
 *          (integer-Bresenham march). Consumers hold a `USeinLevelLoS*` resolved via the
 *          level-data subsystem; they never reference FoW or the substrate directly.
 *
 *          NOTE (per user, 2026-06-07): this exposes/shares a deterministic sim-side
 *          query the framework already runs (FoW LOS / SeinExtents / BPFLs) — it is NOT
 *          a UE float raycast.
 *
 *          THREADING CONTRACT (planning/Roadmap_Multithreading.md, step 1): HasLineOfSight /
 *          IsOccluded MAY be called concurrently — impls must be reentrant. Single-threaded
 *          today; this only reserves the contract.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinLevelLoS.generated.h"

UCLASS(Abstract, BlueprintType, meta = (DisplayName = "Sein Level LoS"))
class SEINARTSLEVELDATA_API USeinLevelLoS : public UObject
{
	GENERATED_BODY()

public:

	/** True if an unobstructed straight line exists from `From` to `To` — terrain
	 *  (the shared height field) and static occluders block it. Deterministic
	 *  fixed-point march over the baked grid. Default: true (no data → no occlusion). */
	virtual bool HasLineOfSight(const FFixedVector& From, const FFixedVector& To) const { return true; }

	/** True if `WorldPos` is occluded from `EyePos`. Convenience negation of
	 *  HasLineOfSight, exposed for combat readability. */
	virtual bool IsOccluded(const FFixedVector& EyePos, const FFixedVector& WorldPos) const
	{
		return !HasLineOfSight(EyePos, WorldPos);
	}
};

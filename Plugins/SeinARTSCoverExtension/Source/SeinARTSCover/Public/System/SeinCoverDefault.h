/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverDefault.h
 * @brief   Minimal reference impl of USeinCoverSystem.
 *
 *          Maintains a flat list of provider entity handles. QueryCoverAt
 *          walks the list, transforms each provider's slots + area into world
 *          space via the host entity's transform, and runs point-in-slot
 *          (radius check) + point-in-area (shape check) tests per provider.
 *
 *          O(P × (S + 1)) per query where P = providers, S = slots per
 *          provider. Acceptable for phase 2a (cover-rich maps cap out around
 *          50-100 providers, each with <10 slots). Upgrade to a spatial index
 *          if profiling demands it — flagged as a future optimization.
 */

#pragma once

#include "CoreMinimal.h"
#include "System/SeinCoverSystem.h"
#include "Types/FixedPoint.h"
#include "SeinCoverDefault.generated.h"

UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Sein Cover Default"))
class SEINARTSCOVER_API USeinCoverDefault : public USeinCoverSystem
{
	GENERATED_BODY()

public:
	virtual void OnCoverSystemDeinitialized() override;

	virtual void RegisterProvider(FSeinEntityHandle ProviderHandle) override;
	virtual void UnregisterProvider(FSeinEntityHandle ProviderHandle) override;

	virtual TArray<FSeinCoverContext> QueryCoverAt(FFixedVector WorldPoint,
		FSeinPlayerID Observer = FSeinPlayerID()) const override;

	/** Override of the default "first-context-wins" pick: applies the canonical
	 *  framework priority Heavy > Light > Negative > <any other tag> when
	 *  multiple cover sources overlap at a query point. Used by the formation
	 *  preview to pick a single decal tint per cell. Subclasses can further
	 *  override to use designer-configured priority tables. */
	virtual FGameplayTag QueryBestCoverQualityAt(FFixedVector WorldPoint,
		FSeinPlayerID Observer = FSeinPlayerID()) const override;

	virtual TArray<FSeinCoverSlotCandidate> FindNearbySlots(FFixedVector Origin, FFixedPoint Radius,
		FSeinPlayerID Observer = FSeinPlayerID()) const override;

	// Tunables
	// ====================================================================================================

	/** A unit's position must be within this radius (in world units) of a
	 *  slot's world position to qualify as "in" that slot. 75 cm matches
	 *  typical infantry footprint scale; tune up for larger units. Per-impl
	 *  rather than per-slot — slots are designer-authored as exact positions,
	 *  and the tolerance is uniform across the system. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Cover")
	FFixedPoint SlotMatchRadius = FFixedPoint::FromInt(75);

protected:
	/** Registered cover-provider entity handles. Phase 2a uses a flat list;
	 *  upgrade to a spatial index (cell hash, BVH, etc.) when provider counts
	 *  + query rates make the linear scan a profile hit. */
	UPROPERTY(Transient)
	TArray<FSeinEntityHandle> RegisteredProviders;

	/** Per-provider cached "reach" — conservative upper bound on the distance
	 *  from a provider's actor center to any of its slots or any point in
	 *  its cover area. Parallel array to `RegisteredProviders` (same length,
	 *  same index). Computed once at registration time (cover area
	 *  dimensions are immutable after authoring); used by `QueryCoverAt`
	 *  and `FindNearbySlots` as a coarse per-provider distance gate before
	 *  paying the per-slot / per-shape work.
	 *
	 *  Without this gate, every cover query on a 50-provider map iterated
	 *  every provider — the scaling factor that made the formation preview
	 *  expensive when many cover providers were placed. Not UPROPERTY
	 *  because FFixedPoint is a USTRUCT not exposed for reflection and we
	 *  don't need it serialized (rebuilt on RegisterProvider). */
	TArray<FFixedPoint> RegisteredProviderReaches;
};

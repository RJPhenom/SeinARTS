/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDefaultCommandBrokerResolver.h
 * @brief   Default resolver — capability-map-filtered dispatch with
 *          anchor-aligned uniform-spacing positions (DESIGN §5).
 *
 *          Per-member dispatch rule:
 *            1. If the member can service the order's ContextTag, dispatch
 *               that ability against the order target.
 *            2. Otherwise, fall back to SeinARTS.Ability.Move toward the
 *               formation position for that member (non-combatants tagging
 *               along with an attack order, etc.).
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "SeinDefaultCommandBrokerResolver.generated.h"

class USeinFormation;

/**
 * Decides, for each unit in a selection, what a single order actually makes that unit do, and
 * where each one stands when it gets there. This is the resolver picked out of the box, and it is
 * the base the Squad and Cover extensions build on.
 *
 * Dispatch is capability-driven, per member. If a member can service the order's context (its own
 * commands table maps the click context to an ability), that ability is dispatched against the
 * order target. Otherwise the member falls back to a "tag along" move toward its own slot in the
 * formation — so non-combatants swept up in an attack order, or units with an unmapped click type,
 * still travel with the group instead of standing idle. The tag-along ability is configurable and
 * can be turned off (leaving those members idle for the order).
 *
 * Positions come from a formation. The order gesture (a right-click-drag, an alt-drag, etc.) can
 * stamp a formation tag that this resolver looks up in Formations By Tag; if nothing matches it
 * uses the Default Formation Class, and if that is also unset it falls back to a blob where every
 * member shares the single destination. Point the default at a spreading formation (grid, wedge,
 * ring, etc.) to change the no-gesture layout.
 *
 * Two opt-out slot RE-MATCH passes keep a moving formation from making units cross paths: the
 * lateral pass re-ranks members left-to-right, and with the depth pass added the grid gets a full
 * 2-D nearest-slot assignment so a deep block also converges without units swapping front-to-back.
 * Both default on; clearing them falls back to raw index order. The re-match is deterministic —
 * every sort carries a handle tie-break for a total, bit-identical order across clients — so it is
 * safe for lockstep.
 */
UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Default Command Broker Resolver"))
class SEINARTSCOREENTITY_API USeinDefaultCommandBrokerResolver : public USeinCommandBrokerResolver
{
	GENERATED_BODY()

public:
	USeinDefaultCommandBrokerResolver();

	/** Default formation for this resolver's moves when the order nominates no
	 *  FormationTag (or the tag isn't in FormationsByTag). Null → the resolver's
	 *  ResolvePositions fallback (a blob — every member shares the destination),
	 *  which is the framework default. Point at USeinGridFormation, a custom
	 *  formation, etc. to change the no-gesture layout. (Replaced the removed
	 *  bFormationSpreadEnabled bool: "spread" is now "pick a spreading formation".) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Broker|Formation",
		meta = (DisplayName = "Default Formation Class"))
	TSoftClassPtr<USeinFormation> DefaultFormationClass;

	/** Optional map of gesture-nominated formation tag → formation class. The order
	 *  gesture (e.g. a right-click-drag) stamps an FSeinOrderTarget::FormationTag;
	 *  the resolver looks it up here, falling back to DefaultFormationClass. Lets a
	 *  project bind "drag = line", "alt-drag = column", etc. without code. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Broker|Formation",
		meta = (DisplayName = "Formations By Tag"))
	TMap<FGameplayTag, TSoftClassPtr<USeinFormation>> FormationsByTag;

	/** Formation-level slot RE-MATCH on the LATERAL (left/right) axis. OPT-OUT, default true.
	 *  When a non-squad selection moves, members are re-matched to the grid slots by left/right rank
	 *  so a rotating formation doesn't make everyone cross to their old index slot. Clear to fall back
	 *  to raw index order. (Squads carry their OWN per-squad opt-IN flags on FSeinSquadComponent.) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Broker|Formation",
		meta = (DisplayName = "Reassign Slots Lateral"))
	bool bReassignSlotsLateral = true;

	/** Formation-level slot RE-MATCH on the DEPTH (front/back) axis. OPT-OUT, default true.
	 *  With Lateral (the default), the grid gets a full 2-D nearest-slot assignment so a deep block
	 *  converges without crossing front-to-back as well as left-to-right. Clear to drop back to a 1-D
	 *  (lateral-only) match, or clear both for raw index order. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Broker|Formation",
		meta = (DisplayName = "Reassign Slots Depth"))
	bool bReassignSlotsDepth = true;

	/** Ability tag dispatched for members whose own DefaultCommands table doesn't
	 *  resolve the click context (ResolveMemberAbility returned invalid). Targets
	 *  the formation slot, not the order target — "tag along with the group" for
	 *  non-combatants on an attack order, unmapped click types, etc. Set to an
	 *  invalid tag to disable the tag-along behavior entirely (those members
	 *  stay idle for the order). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|Broker|Dispatch",
		meta = (Categories = "SeinARTS.Ability"))
	FGameplayTag TagAlongAbility;

	virtual FSeinBrokerDispatchPlan ResolveDispatch_Implementation(
		USeinWorldSubsystem* World,
		FSeinEntityHandle BrokerHandle,
		const FSeinBrokerOrderInput& Order) override;

	virtual TArray<FFixedVector> ResolvePositions_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		FFixedVector Anchor,
		FFixedQuaternion Facing) override;

	virtual FSeinFormationLayout ResolveFormationLayout_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target,
		bool bReassignLateral,
		bool bReassignDepth) override;

	/** Re-match members to formation slots so a rotating/translating formation doesn't make units
	 *  cross paths. Per-axis, driven by the two designer flags:
	 *    - bLateral only → 1-D rank match on the formation RIGHT axis (preserve left/right order;
	 *                      front/back untouched — the wedge/arrow behavior).
	 *    - bDepth only   → 1-D rank match on the formation FORWARD axis (preserve front/back order).
	 *    - both          → 2-D nearest-slot assignment (greedy min-distance in centroid-aligned local
	 *                      space; non-crossing for deep grids — the line/block behavior).
	 *    - neither       → no-op (keep ResolvePositions' index order / authored slots as-is).
	 *  Reads each member's CURRENT position and permutes `Positions` in place so `Positions[i]` becomes
	 *  member i's slot — path-independent, so consecutive moves don't oscillate.
	 *
	 *  DETERMINISTIC: every sort carries a handle/slot-index tie-break (TOTAL order → bit-identical
	 *  across clients). A magnitude-only sort is unstable on ties → order-dependent → lockstep desync.
	 *  The 2-D path aligns the member/slot clouds by their own centroids before squaring distances, so
	 *  the bulk move translation cancels (a unit half a map from its slot can't overflow 32.32). */
	static void ReassignSlots(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		TArray<FFixedVector>& Positions,
		FFixedQuaternion FormationFacing,
		bool bLateral,
		bool bDepth);
	// ReassignSlots is PUBLIC (a pure deterministic static utility): consumed by the
	// dispatch layout pass above AND the broker tick's idle re-seek pairing.

protected:
	/** Resolve the USeinFormation that lays out this order. Looks up `FormationTag`
	 *  in FormationsByTag, falls back to DefaultFormationClass, and returns the class
	 *  CDO (formations are stateless / pure compute — invoked on the CDO, never
	 *  instanced). Null when neither resolves → the caller uses the ResolvePositions
	 *  blob fallback. Loads the soft class synchronously (loader-cached). */
	USeinFormation* ResolveFormation(FGameplayTag FormationTag) const;
};

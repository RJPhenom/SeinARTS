/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionResolver.h
 * @brief   Abstract base class for pluggable collision-resolution implementations.
 *
 *          USeinCollisionResolver owns one tick's full collider separation +
 *          overlap-event emission for one world. It is the ONLY thing the
 *          PostTick collision-resolution system (FSeinCollisionResolutionSystem)
 *          talks to: that system is a thin delegator that calls Resolve() once
 *          per tick.
 *
 *          Separation is pure extent-vs-extent: a resolver consults the collision
 *          model (FSeinExtentsComponent + the channel registry) and the
 *          deterministic MTV narrowphase only. The one nav touch-point is the
 *          hard-barrier gate (the world subsystem's pluggable PassableResolver /
 *          AuthoritativeDestinationResolver delegates), which a resolver queries
 *          through the world subsystem so the collision floor stays
 *          nav-impl-agnostic. FRAMING: this is NOT collision asking navigation for
 *          pathing permission — it is collision treating the nav-blocked region as
 *          STATIC, infinite-mass geometry (walls a body can't be shoved through),
 *          which is squarely collision's own job. The nav grid is just the cheap
 *          authoritative source of "where the static barriers are" in a grid RTS
 *          (there are no per-cell wall colliders). So collision stays nav-BLIND in
 *          principle: it never softens or changes an overlap decision to serve pathing.
 *
 *          Configured via plugin settings
 *          (`USeinARTSCoreSettings::CollisionResolverClass`). The framework ships
 *          `USeinCollisionResolverDefault` as the default: the Gauss-Seidel
 *          relaxation pass set + overlap diff. An optional
 *          `USeinCollisionResolverParallel` (default-off) ships alongside.
 *          Game teams can subclass or replace it entirely (impulse-based,
 *          position-based-dynamics, etc.) without touching any other framework
 *          code. Mirrors the pluggable Navigation / Fog-of-War pattern (abstract
 *          base + shipped default, class chosen in settings).
 *
 *          SHARED HELPERS LIVE HERE. The narrowphase shape build (FCollisionShape2D
 *          / BuildShape2D / BuildShapes2D / NarrowphasePair / ComputeDeepestContact),
 *          the channel/collider/mass helpers (BuildChannelDefaults / IsCollider /
 *          ResolveColliderMass), the hard-barrier gate (CanOccupy), the overlap diff
 *          (MakePairKey / DetectOverlapsAndEmit + the ActiveOverlaps scratch) are
 *          PROTECTED members of this base so every concrete resolver reuses the SAME
 *          verbatim implementation. Subclasses supply only the separation strategy
 *          (Gauss-Seidel relaxation vs. parallel Jacobi vs. impulse/PBD/…) on top of
 *          this shared floor. ResolvePairResponse is a free function in
 *          SeinCollisionTypes.h — both resolvers call it directly.
 *
 *          Determinism: a resolver is a deterministic UObject — fixed-point only
 *          (no float / FMath / rand), GC-rooted by the world subsystem's
 *          UPROPERTY. Any per-tick scratch it keeps (last-tick overlap set, etc.)
 *          is a transient render signal, NOT part of the hashed sim state.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Math/CollisionQueries.h"
#include "Collision/SeinCollisionTypes.h"
#include "Components/SeinExtentsComponent.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "Types/Transform.h"
#include "Types/Vector.h"
#include "SeinCollisionResolver.generated.h"

class USeinWorldSubsystem;
class UWorld;

UCLASS(Abstract, BlueprintType, meta = (DisplayName = "Sein Collision Resolver"))
class SEINARTSCOREENTITY_API USeinCollisionResolver : public UObject
{
	GENERATED_BODY()

public:

	// ----------------------------------------------------------------------
	// Lifecycle — called by USeinWorldSubsystem
	// ----------------------------------------------------------------------

	/** Called once when the world subsystem instantiates this resolver.
	 *  Default: no-op. Mirrors USeinNavigation::OnNavigationInitialized. */
	virtual void OnInitialized(UWorld* /*World*/) {}

	// ----------------------------------------------------------------------
	// Resolution — the per-tick surface
	// ----------------------------------------------------------------------

	/** Run one simulation tick's FULL collision resolution: the relaxation /
	 *  separation passes that push overlapping Block colliders apart along their
	 *  minimum-translation axis, followed by the overlap-event diff that emits
	 *  CollisionOverlapBegin / CollisionOverlapEnd visual events for Overlap pairs.
	 *  Called once per PostTick by FSeinCollisionResolutionSystem on the SETTLED
	 *  positions snapshot. Default: no-op (a resolver that does nothing leaves
	 *  colliders untouched). Subclasses override. */
	virtual void Resolve(USeinWorldSubsystem& /*World*/) {}

protected:

	// ======================================================================
	// SHARED RESOLUTION HELPERS — used verbatim by every concrete resolver
	// (Gauss-Seidel default, parallel Jacobi, …). Lifted up from the Default
	// resolver so a second strategy reuses them with NO logic change.
	// ======================================================================

	/** Overlap-responding pairs that were overlapping LAST tick, keyed by the
	 *  canonical pair key (minIndex<<32 | maxIndex), value = (A,B) handles with
	 *  A.Index < B.Index. Diffed against this tick's set to emit begin/end
	 *  events. Lives on the resolver, not the hashed sim state — a snapshot
	 *  restore simply re-derives it within a tick (a transient render signal). */
	TMap<uint64, TPair<FSeinEntityHandle, FSeinEntityHandle>> ActiveOverlaps;

	/** A single Extents shape resolved into a planar (XY) collision primitive,
	 *  in world space, plus its vertical [ZMin, ZMax] span for the early-out. */
	struct FCollisionShape2D
	{
		bool         bIsBox = false;
		FFixedVector Center = FFixedVector::ZeroVector;
		FFixedPoint  Radius = FFixedPoint::Zero;            // disc
		FFixedVector AxisX  = FFixedVector::ForwardVector;  // box (unit, planar)
		FFixedVector AxisY  = FFixedVector::RightVector;    // box (unit, planar)
		FFixedPoint  HalfX  = FFixedPoint::Zero;            // box
		FFixedPoint  HalfY  = FFixedPoint::Zero;            // box
		FFixedPoint  ZMin   = FFixedPoint::Zero;
		FFixedPoint  ZMax   = FFixedPoint::Zero;
	};

	/** Build the world-space planar primitive for one Extents shape on an entity.
	 *  Capsule → disc (its vertical axis is a point from above); Box → oriented
	 *  rectangle whose axes come from the entity's planar facing rotated by the
	 *  shape's YawOffset. The shape's LocalOffset is composed via the entity
	 *  transform; its Height defines the Z span. */
	static FCollisionShape2D BuildShape2D(const FFixedTransform& Xf, const FSeinExtentsShape& Shape);

	/** Narrow-phase for one shape pair, returning the contact (Normal points
	 *  A → B). Runs the vertical early-out first, then dispatches by shape type.
	 *  For Box(A)-vs-Disc(B) it calls DiscVsOBB with the disc as A and flips the
	 *  normal back to this call's A → B convention. */
	static SeinCollision::FSeinContact2D NarrowphasePair(const FCollisionShape2D& A, const FCollisionShape2D& B);

	/** Build every Extents shape of one collider into world-space planar
	 *  primitives. Done ONCE per "self" per pass and reused across the whole
	 *  neighbour loop, so a self's shapes are not rebuilt per pair. */
	static void BuildShapes2D(const FSeinExtentsComponent& Ext, const FFixedTransform& Xf, TArray<FCollisionShape2D>& Out);

	/** Deepest contact between a collider's PRE-BUILT self shapes and another
	 *  collider's Extents. The deepest (max-penetration) contact drives the
	 *  separation: resolve the worst overlap first; relaxation passes clean up
	 *  shallower residuals. Iteration order (self-outer, other-inner) and the
	 *  strict > tie-break are identical to the pre-optimization version. */
	static bool ComputeDeepestContact(
		const TArray<FCollisionShape2D>& SelfShapes,
		const FSeinExtentsComponent& OtherExt, const FFixedTransform& OtherXf,
		FFixedVector& OutNormal, FFixedPoint& OutDepth);

	/** Snapshot the enabled channels' default responses by name. */
	static void BuildChannelDefaults(TMap<FName, ESeinCollisionResponse>& Out);

	/** Effective response for a self↔other collider pair: look up each side's
	 *  per-channel response against the OTHER's object-type channel (falling back
	 *  to that channel's default from `ChannelDefaults`), then resolve to the
	 *  weaker of the two via ResolvePairResponse (Block needs both). The single
	 *  shared form of the four-lookup expression every resolver's per-pair loop
	 *  runs — same channel lookup order and tie-break, so the resolved value is
	 *  byte-identical across the Gauss-Seidel, parallel Jacobi, and overlap-diff
	 *  call sites. */
	static FORCEINLINE ESeinCollisionResponse ResolvePairFor(
		const FSeinExtentsComponent& SelfExt, const FSeinExtentsComponent& OtherExt,
		const TMap<FName, ESeinCollisionResponse>& ChannelDefaults)
	{
		const ESeinCollisionResponse DefSelfToOther = ChannelDefaults.FindRef(OtherExt.ObjectType.Channel);
		const ESeinCollisionResponse DefOtherToSelf = ChannelDefaults.FindRef(SelfExt.ObjectType.Channel);
		const ESeinCollisionResponse RespSelfToOther = SelfExt.CollisionResponses.GetResponseForChannel(OtherExt.ObjectType.Channel, DefSelfToOther);
		const ESeinCollisionResponse RespOtherToSelf = OtherExt.CollisionResponses.GetResponseForChannel(SelfExt.ObjectType.Channel, DefOtherToSelf);
		return ResolvePairResponse(RespSelfToOther, RespOtherToSelf);
	}

	/** True iff the entity is a live collider eligible for resolution (enabled,
	 *  has a body, has an object type). */
	static bool IsCollider(const FSeinExtentsComponent* Ext);

	/** A collider's authored push mass, floored to a small positive so the ratio
	 *  test and the mass-weighted split never divide by zero. Pure collision data —
	 *  never derived from footprint, nav, or movement. */
	static FFixedPoint ResolveColliderMass(const FSeinExtentsComponent& Ext);

	/** Hard-barrier gate: true iff a collider of bounding radius `Radius` may
	 *  occupy world position `P` — i.e. the move would NOT put its FOOTPRINT onto
	 *  a non-walkable cell (a baked nav wall, or off the grid edge). Samples the
	 *  center plus an 8-direction unit ring at the collider radius (the body, not
	 *  just the center) through the world subsystem's pluggable PassableResolver,
	 *  so the collision floor stays nav-impl-agnostic — a one-way "walkable?"
	 *  query, no hard nav dependency. Cover slots (AuthoritativeDestinationResolver)
	 *  are exempt: a unit may be delivered onto a bake-blocked slot. Unbound
	 *  (nav-less / tests) → always true (ungated), identical to the prior behavior.
	 *  Shared by every resolver so the "never through a wall" rule is one
	 *  implementation. */
	static bool CanOccupy(USeinWorldSubsystem& World, const FFixedVector& P, FFixedPoint Radius);

	/** Build a canonical pair key (lower entity index in the high 32 bits). */
	static FORCEINLINE uint64 MakePairKey(int32 IndexA, int32 IndexB)
	{
		const int32 Lo = (IndexA < IndexB) ? IndexA : IndexB;
		const int32 Hi = (IndexA < IndexB) ? IndexB : IndexA;
		return (static_cast<uint64>(static_cast<uint32>(Lo)) << 32) | static_cast<uint32>(Hi);
	}

	/**
	 * Sweep the settled positions for Overlap-responding pairs that geometrically
	 * overlap, then diff against last tick's set to emit CollisionOverlapBegin /
	 * CollisionOverlapEnd visual events. Block pairs are already separated by the
	 * resolution passes, so they don't appear here; Ignore pairs are skipped.
	 * Event emission order is made deterministic by sorting the pair keys (TMap
	 * iteration order is not stable, but the sorted uint64 keys are).
	 */
	void DetectOverlapsAndEmit(USeinWorldSubsystem& World, const TMap<FName, ESeinCollisionResponse>& ChannelDefaults);
};

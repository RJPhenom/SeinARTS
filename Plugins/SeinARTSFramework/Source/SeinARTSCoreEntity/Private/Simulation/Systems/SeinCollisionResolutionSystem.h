/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionResolutionSystem.h
 * @brief   PostTick system that separates overlapping COLLIDERS along their
 *          minimum-translation axis, so two Blocking colliders never end a tick
 *          inside each other — and a unit can never be shoved THROUGH a solid
 *          collider (a wall especially).
 *
 *          Separation is pure extent-vs-extent: the OVERLAP test consults ONLY
 *          the collision model (FSeinExtentsComponent's collision section + the
 *          channel registry) and the deterministic MTV narrowphase. The one nav
 *          touch-point is the HARD-BARRIER gate: a separation move that would land
 *          a unit on a non-walkable cell (a baked nav wall, or off the grid edge)
 *          is REFUSED — the unit holds at the barrier instead of being shoved
 *          across it. That walkability test goes through the world subsystem's
 *          pluggable PassableResolver delegate (cover slots exempt via
 *          AuthoritativeDestinationResolver), so the floor stays nav-impl-agnostic:
 *          it asks "walkable?" through a seam, it doesn't know any nav. Net result:
 *          sein-extents colliders block by the MTV separation; baked nav walls and
 *          the grid edge block by this gate; both are never-crossable.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Math/MathLib.h"
#include "Math/CollisionQueries.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Collision/SeinCollisionSpatialHash.h"
#include "Components/SeinExtentsComponent.h"
#include "Components/SeinExtentsHelpers.h"
#include "Settings/PluginSettings.h"
#include "Events/SeinVisualEvent.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Transform.h"
#include "Types/Vector.h"

/**
 * System: Collision Resolution
 * Phase: PostTick | Priority: 10
 *
 * Runs after movement (PostTick) and before StateHash (priority 100), so its
 * separations are part of the deterministic state snapshot. Uses the collision
 * broadphase (rebuilt PreTick) to find candidate neighbours in O(K).
 *
 * Per tick: build the channel default-response table once, then run a fixed
 * number of relaxation passes. Each pass iterates only MOVABLE colliders as
 * "self" (statics never move, so they're only ever the queried neighbour),
 * and for each Blocking pair pushes apart along the deepest shape-pair contact.
 * Static neighbours are infinite-mass (the movable takes the entire push), which
 * is what makes a wall un-passable.
 */
class FSeinCollisionResolutionSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		// Channel default responses, resolved once per tick (defaults don't
		// change mid-tick). Per-pair lookups are then O(1) map gets.
		TMap<FName, ESeinCollisionResponse> ChannelDefaults;
		BuildChannelDefaults(ChannelDefaults);
		if (ChannelDefaults.Num() == 0) return; // no enabled channels → nothing to resolve

		// Fixed relaxation passes: each pass fully separates any pair it touches;
		// repeating settles clusters (units packed against a wall) without the
		// cost or nondeterminism risk of an open-ended converge loop.
		// Mass-ratio cutoff (Project Settings > Plugins > SeinARTS > Collision).
		// Integer ratio → exact fixed-point; read once per tick, constant across peers.
		const USeinARTSCoreSettings* MassSettings = GetDefault<USeinARTSCoreSettings>();
		const int32 RawCutoff = MassSettings ? MassSettings->CollisionMassRatioCutoff : 8;
		const FFixedPoint MassRatioCutoff = FFixedPoint::FromInt(RawCutoff > 1 ? RawCutoff : 1);

		constexpr int32 NumPasses = 4;
		for (int32 Pass = 0; Pass < NumPasses; ++Pass)
		{
			ResolvePass(World, ChannelDefaults, MassRatioCutoff);
		}

		// Overlap events run on the SETTLED positions (after Block separation),
		// so Overlap-responding pairs report their final overlap state this tick.
		DetectOverlapsAndEmit(World, ChannelDefaults);
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PostTick; }
	virtual int32 GetPriority() const override { return 10; }
	virtual FName GetSystemName() const override { return TEXT("CollisionResolution"); }

private:
	/** Overlap-responding pairs that were overlapping LAST tick, keyed by the
	 *  canonical pair key (minIndex<<32 | maxIndex), value = (A,B) handles with
	 *  A.Index < B.Index. Diffed against this tick's set to emit begin/end
	 *  events. Lives on the system, not the hashed sim state — a snapshot
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
	static FCollisionShape2D BuildShape2D(const FFixedTransform& Xf, const FSeinExtentsShape& Shape)
	{
		FCollisionShape2D Out;
		Out.bIsBox = (Shape.Shape == ESeinExtentsShape::Box);

		const FFixedVector Center = Xf.GetLocation() + Xf.TransformVector(Shape.LocalOffset);
		Out.Center = Center;
		Out.ZMin = Center.Z;
		Out.ZMax = Center.Z + Shape.Height;

		if (!Out.bIsBox)
		{
			Out.Radius = Shape.Radius;
			return Out;
		}

		Out.HalfX = Shape.HalfExtentX;
		Out.HalfY = Shape.HalfExtentY;

		// Planar facing from the entity rotation (drop Z, renormalize) so slope
		// pitch/roll don't tilt the footprint axes.
		FFixedVector Forward = Xf.GetQuaternionRotation().GetForwardVector();
		Forward.Z = FFixedPoint::Zero;
		Forward = FFixedVector::GetSafeNormal(Forward);
		if (Forward.IsZero())
		{
			Forward = FFixedVector::ForwardVector;
		}

		// Apply the shape's yaw offset (degrees) about Z, if any.
		if (Shape.YawOffsetDegrees != FFixedPoint::Zero)
		{
			const FFixedPoint Rad = Shape.YawOffsetDegrees * FFixedPoint::Pi / FFixedPoint::FromInt(180);
			const FFixedPoint CosA = SeinMath::Cos(Rad);
			const FFixedPoint SinA = SeinMath::Sin(Rad);
			const FFixedPoint NX = Forward.X * CosA - Forward.Y * SinA;
			const FFixedPoint NY = Forward.X * SinA + Forward.Y * CosA;
			Forward = FFixedVector(NX, NY, FFixedPoint::Zero);
		}

		Out.AxisX = Forward;
		// Planar perpendicular. The box footprint is symmetric (±HalfX, ±HalfY),
		// so the sign of the right axis is irrelevant to the SAT result.
		Out.AxisY = FFixedVector(Forward.Y, -Forward.X, FFixedPoint::Zero);
		return Out;
	}

	/** Narrow-phase for one shape pair, returning the contact (Normal points
	 *  A → B). Runs the vertical early-out first, then dispatches by shape type.
	 *  For Box(A)-vs-Disc(B) it calls DiscVsOBB with the disc as A and flips the
	 *  normal back to this call's A → B convention. */
	static SeinCollision::FSeinContact2D NarrowphasePair(const FCollisionShape2D& A, const FCollisionShape2D& B)
	{
		SeinCollision::FSeinContact2D Contact;
		if (!SeinCollision::RangesOverlap(A.ZMin, A.ZMax, B.ZMin, B.ZMax))
		{
			return Contact; // vertical spans don't overlap → not colliding
		}

		if (!A.bIsBox && !B.bIsBox)
		{
			return SeinCollision::DiscVsDisc(A.Center, A.Radius, B.Center, B.Radius);
		}
		if (!A.bIsBox && B.bIsBox)
		{
			return SeinCollision::DiscVsOBB(A.Center, A.Radius, B.Center, B.AxisX, B.AxisY, B.HalfX, B.HalfY);
		}
		if (A.bIsBox && !B.bIsBox)
		{
			Contact = SeinCollision::DiscVsOBB(B.Center, B.Radius, A.Center, A.AxisX, A.AxisY, A.HalfX, A.HalfY);
			Contact.Normal = -Contact.Normal; // disc(B)→box(A) is B→A; flip to A→B
			return Contact;
		}
		return SeinCollision::OBBVsOBB(
			A.Center, A.AxisX, A.AxisY, A.HalfX, A.HalfY,
			B.Center, B.AxisX, B.AxisY, B.HalfX, B.HalfY);
	}

	/** Build every Extents shape of one collider into world-space planar
	 *  primitives. Done ONCE per "self" per pass and reused across the whole
	 *  neighbour loop, so a self's shapes are not rebuilt per pair. */
	static void BuildShapes2D(const FSeinExtentsComponent& Ext, const FFixedTransform& Xf, TArray<FCollisionShape2D>& Out)
	{
		Out.Reset(Ext.Shapes.Num());
		for (const FSeinExtentsShape& Shape : Ext.Shapes)
		{
			Out.Add(BuildShape2D(Xf, Shape));
		}
	}

	/** Deepest contact between a collider's PRE-BUILT self shapes and another
	 *  collider's Extents. The deepest (max-penetration) contact drives the
	 *  separation: resolve the worst overlap first; relaxation passes clean up
	 *  shallower residuals. Iteration order (self-outer, other-inner) and the
	 *  strict > tie-break are identical to the pre-optimization version. */
	static bool ComputeDeepestContact(
		const TArray<FCollisionShape2D>& SelfShapes,
		const FSeinExtentsComponent& OtherExt, const FFixedTransform& OtherXf,
		FFixedVector& OutNormal, FFixedPoint& OutDepth)
	{
		bool bAny = false;
		FFixedPoint BestDepth = FFixedPoint::Zero;
		FFixedVector BestNormal = FFixedVector::ZeroVector;

		for (const FCollisionShape2D& A : SelfShapes)
		{
			for (const FSeinExtentsShape& ShapeB : OtherExt.Shapes)
			{
				const FCollisionShape2D B = BuildShape2D(OtherXf, ShapeB);
				const SeinCollision::FSeinContact2D Contact = NarrowphasePair(A, B);
				if (Contact.bHit && Contact.Depth > BestDepth)
				{
					BestDepth = Contact.Depth;
					BestNormal = Contact.Normal;
					bAny = true;
				}
			}
		}

		if (bAny)
		{
			OutNormal = BestNormal;
			OutDepth = BestDepth;
		}
		return bAny;
	}

	/** Snapshot the enabled channels' default responses by name. */
	static void BuildChannelDefaults(TMap<FName, ESeinCollisionResponse>& Out)
	{
		Out.Reset();
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		if (!Settings) return;
		for (const FSeinCollisionChannelDefinition& Channel : Settings->GetAllCollisionChannels())
		{
			if (!Channel.Name.IsNone())
			{
				Out.Add(Channel.Name, Channel.DefaultResponse);
			}
		}
	}

	/** True iff the entity is a live collider eligible for resolution (enabled,
	 *  has a body, has an object type). */
	static bool IsCollider(const FSeinExtentsComponent* Ext)
	{
		return Ext && Ext->bCollisionEnabled && Ext->Shapes.Num() > 0 && !Ext->ObjectType.Channel.IsNone();
	}

	/** A collider's authored push mass, floored to a small positive so the ratio
	 *  test and the mass-weighted split never divide by zero. Pure collision data —
	 *  never derived from footprint, nav, or movement. */
	static FFixedPoint ResolveColliderMass(const FSeinExtentsComponent& Ext)
	{
		return (Ext.Mass > FFixedPoint::Epsilon) ? Ext.Mass : FFixedPoint::Epsilon;
	}

	static void ResolvePass(USeinWorldSubsystem& World, const TMap<FName, ESeinCollisionResponse>& ChannelDefaults, const FFixedPoint MassRatioCutoff)
	{
		const FSeinCollisionSpatialHash& Hash = World.GetCollisionSpatialHash();
		const FFixedPoint CellSize = Hash.GetCellSize();
		TArray<FSeinEntityHandle> Neighbors;

		// Hoist the Extents storage once per pass: GetComponent<T>() does a
		// hashmap lookup by UScriptStruct* per call; resolving the storage once
		// makes every per-self / per-neighbour fetch an O(1) indexed get.
		ISeinComponentStorage* ExtentsStorage = World.GetComponentStorageRaw(FSeinExtentsComponent::StaticStruct());
		// Reused scratch for the self collider's pre-built shapes (see below).
		TArray<FCollisionShape2D> SelfShapes;

		// Hard-barrier gate: the push must never move a unit's FOOTPRINT onto a
		// non-walkable cell — a baked nav wall, or off the grid edge, is a
		// never-crossable barrier (the body holds clear of the face instead of
		// being shoved across it). Footprint-aware to MATCH the movement step's
		// ResolveNavCollision, which keeps the whole body off walls; a center-only
		// gate would let the push shove a body half-into a wall while its center
		// cell stayed passable (the "units in the wall" symptom). Queried through
		// the world subsystem's pluggable passability delegate so the collision
		// floor stays nav-impl-agnostic — a one-way "walkable?" query, no hard nav
		// dependency. Cover slots (authoritative destinations) are exempt: a unit
		// may be delivered onto a bake-blocked slot. Unbound (nav-less / tests) →
		// ungated, identical to the prior behavior.
		const bool bBarrierGate = World.PassableResolver.IsBound();
		const bool bAuthExempt  = World.AuthoritativeDestinationResolver.IsBound();
		// 8 unit-ring directions (45° spacing), sampled at the collider radius — the
		// body footprint, not just the center.
		const FFixedPoint RingDiag = FFixedPoint::FromInt(7071) / FFixedPoint::FromInt(10000); // ≈ cos 45°
		const FFixedVector BarrierRing[8] = {
			FFixedVector( FFixedPoint::One,   FFixedPoint::Zero, FFixedPoint::Zero),
			FFixedVector( RingDiag,           RingDiag,          FFixedPoint::Zero),
			FFixedVector( FFixedPoint::Zero,  FFixedPoint::One,  FFixedPoint::Zero),
			FFixedVector(-RingDiag,           RingDiag,          FFixedPoint::Zero),
			FFixedVector(-FFixedPoint::One,   FFixedPoint::Zero, FFixedPoint::Zero),
			FFixedVector(-RingDiag,          -RingDiag,          FFixedPoint::Zero),
			FFixedVector( FFixedPoint::Zero, -FFixedPoint::One,  FFixedPoint::Zero),
			FFixedVector( RingDiag,          -RingDiag,          FFixedPoint::Zero),
		};
		auto CanOccupy = [&World, bBarrierGate, bAuthExempt, &BarrierRing](const FFixedVector& P, FFixedPoint Radius) -> bool
		{
			if (!bBarrierGate) return true;
			// Cover slot (authoritative): the whole body may sit on a bake-blocked cell.
			if (bAuthExempt && World.AuthoritativeDestinationResolver.Execute(P)) return true;
			// Center first, then the footprint ring — the body must clear walls too.
			if (!World.PassableResolver.Execute(P)) return false;
			if (Radius > FFixedPoint::Zero)
			{
				for (int32 i = 0; i < 8; ++i)
				{
					const FFixedVector S(P.X + BarrierRing[i].X * Radius, P.Y + BarrierRing[i].Y * Radius, P.Z);
					if (!World.PassableResolver.Execute(S)) return false;
				}
			}
			return true;
		};

		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle SelfHandle, FSeinEntity& SelfEntity)
		{
			const FSeinExtentsComponent* SelfExt = ExtentsStorage
				? static_cast<const FSeinExtentsComponent*>(ExtentsStorage->GetComponentRaw(SelfHandle))
				: nullptr;
			if (!IsCollider(SelfExt)) return;
			// Non-movable colliders (Static + Stationary) never initiate a push —
			// they're only resolved as the queried neighbour of a movable, so skip
			// them as "self".
			if (SelfExt->Mobility != ESeinCollisionMobility::Movable) return;

			const FFixedPoint SelfRadius = SeinExtentsHelpers::GetColliderBoundingRadius(*SelfExt);
			if (SelfRadius <= FFixedPoint::Zero) return;
			const FFixedPoint MassSelf = ResolveColliderMass(*SelfExt);

			const FFixedVector SelfQueryPos = SelfEntity.Transform.GetLocation();
			// Footprint-stamped broadphase means a query radius covering self's
			// own footprint finds any overlapping collider; +1 cell of slack
			// absorbs mid-pass drift.
			const FFixedPoint QueryRadius = SelfRadius + CellSize;

			Neighbors.Reset();
			Hash.QueryRadius(SelfQueryPos, QueryRadius, Neighbors, SelfHandle);

			for (const FSeinEntityHandle& OtherHandle : Neighbors)
			{
				const FSeinExtentsComponent* OtherExt = ExtentsStorage
					? static_cast<const FSeinExtentsComponent*>(ExtentsStorage->GetComponentRaw(OtherHandle))
					: nullptr;
				if (!IsCollider(OtherExt)) continue;

				const bool bOtherImmovable = (OtherExt->Mobility != ESeinCollisionMobility::Movable);

				// Process each movable-movable pair once (from the lower index).
				// Immovable (Static/Stationary) neighbours are only ever seen here, so never skip them.
				if (!bOtherImmovable && OtherHandle.Index <= SelfHandle.Index) continue;

				// Effective response = weaker of the two sides (Block needs both).
				const ESeinCollisionResponse DefSelfToOther = ChannelDefaults.FindRef(OtherExt->ObjectType.Channel);
				const ESeinCollisionResponse DefOtherToSelf = ChannelDefaults.FindRef(SelfExt->ObjectType.Channel);
				const ESeinCollisionResponse RespSelfToOther = SelfExt->CollisionResponses.GetResponseForChannel(OtherExt->ObjectType.Channel, DefSelfToOther);
				const ESeinCollisionResponse RespOtherToSelf = OtherExt->CollisionResponses.GetResponseForChannel(SelfExt->ObjectType.Channel, DefOtherToSelf);
				const ESeinCollisionResponse Effective = ResolvePairResponse(RespSelfToOther, RespOtherToSelf);

				// Only Block pushes. Ignore → nothing; Overlap → no push (overlap
				// begin/end events are emitted by the overlap system, not here).
				if (Effective != ESeinCollisionResponse::Block) continue;

				FSeinEntity* OtherEntity = World.GetEntityPool().Get(OtherHandle);
				if (!OtherEntity) continue;

				FFixedVector Normal;
				FFixedPoint  Depth;
				// Rebuild self's shapes from its CURRENT transform: self is pushed
				// (below) as it resolves earlier neighbours this pass, so later pairs
				// must see the moved position (matches the pre-opt per-pair rebuild).
				BuildShapes2D(*SelfExt, SelfEntity.Transform, SelfShapes);
				if (!ComputeDeepestContact(SelfShapes, *OtherExt, OtherEntity->Transform, Normal, Depth)) continue;
				if (Depth <= FFixedPoint::Zero) continue;

				// Mass-weighted split. Immovable other (Static/Stationary) = infinite mass → the movable
				// self absorbs the entire separation (can't shove a wall or stationary platform).
				FFixedPoint SelfShare;
				FFixedPoint OtherShare;
				if (bOtherImmovable)
				{
					SelfShare = FFixedPoint::One;
					OtherShare = FFixedPoint::Zero;
				}
				else
				{
					const FFixedPoint MassOther = ResolveColliderMass(*OtherExt);
					// Mass-ratio cutoff: a collider at least Cutoff× the other's mass is
					// immovable for THIS pair — the lighter body absorbs the entire
					// separation (a mob of infantry can't shove a tank). Cross-multiply
					// so the ratio test needs no division.
					if (MassSelf >= MassOther * MassRatioCutoff)
					{
						SelfShare = FFixedPoint::Zero;  // self much heavier → unpushable here
						OtherShare = FFixedPoint::One;
					}
					else if (MassOther >= MassSelf * MassRatioCutoff)
					{
						SelfShare = FFixedPoint::One;   // other much heavier → unpushable here
						OtherShare = FFixedPoint::Zero;
					}
					else
					{
						const FFixedPoint MassSum = MassSelf + MassOther;
						SelfShare = (MassSum > FFixedPoint::Epsilon) ? (MassOther / MassSum) : FFixedPoint::Half;
						OtherShare = FFixedPoint::One - SelfShare;
					}
				}

				// Self moves along -Normal (away from other); preserve Z. HOLDS at
				// the barrier if the move would put its FOOTPRINT across a
				// non-walkable cell — the body never crosses a wall / the grid edge
				// (cover exempt), matching the movement step's footprint clamp.
				const FFixedVector SelfPosNow = SelfEntity.Transform.GetLocation();
				FFixedVector SelfNew = SelfPosNow - Normal * (Depth * SelfShare);
				SelfNew.Z = SelfPosNow.Z;
				if (CanOccupy(SelfNew, SelfRadius))
				{
					SelfEntity.Transform.SetLocation(SelfNew);
				}

				// Other moves along +Normal, unless it's an immovable static —
				// same footprint-barrier hold rule, using its own collider radius.
				if (!bOtherImmovable)
				{
					const FFixedVector OtherPosNow = OtherEntity->Transform.GetLocation();
					FFixedVector OtherNew = OtherPosNow + Normal * (Depth * OtherShare);
					OtherNew.Z = OtherPosNow.Z;
					const FFixedPoint OtherRadius = SeinExtentsHelpers::GetColliderBoundingRadius(*OtherExt);
					if (CanOccupy(OtherNew, OtherRadius))
					{
						OtherEntity->Transform.SetLocation(OtherNew);
					}
				}
			}
		});
	}

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
	void DetectOverlapsAndEmit(USeinWorldSubsystem& World, const TMap<FName, ESeinCollisionResponse>& ChannelDefaults)
	{
		const FSeinCollisionSpatialHash& Hash = World.GetCollisionSpatialHash();
		const FFixedPoint CellSize = Hash.GetCellSize();
		TArray<FSeinEntityHandle> Neighbors;

		TMap<uint64, TPair<FSeinEntityHandle, FSeinEntityHandle>> Current;

		// Hoist the Extents storage once (see ResolvePass) + reusable self-shape scratch.
		ISeinComponentStorage* ExtentsStorage = World.GetComponentStorageRaw(FSeinExtentsComponent::StaticStruct());
		TArray<FCollisionShape2D> SelfShapes;

		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle SelfHandle, FSeinEntity& SelfEntity)
		{
			const FSeinExtentsComponent* SelfExt = ExtentsStorage
				? static_cast<const FSeinExtentsComponent*>(ExtentsStorage->GetComponentRaw(SelfHandle))
				: nullptr;
			if (!IsCollider(SelfExt)) return;
			if (SelfExt->Mobility != ESeinCollisionMobility::Movable) return;

			const FFixedPoint SelfRadius = SeinExtentsHelpers::GetColliderBoundingRadius(*SelfExt);
			if (SelfRadius <= FFixedPoint::Zero) return;

			const FFixedVector SelfPos = SelfEntity.Transform.GetLocation();
			const FFixedPoint QueryRadius = SelfRadius + CellSize;

			Neighbors.Reset();
			Hash.QueryRadius(SelfPos, QueryRadius, Neighbors, SelfHandle);

			// Build self's collision shapes ONCE, reused across all neighbours.
			BuildShapes2D(*SelfExt, SelfEntity.Transform, SelfShapes);

			for (const FSeinEntityHandle& OtherHandle : Neighbors)
			{
				const FSeinExtentsComponent* OtherExt = ExtentsStorage
					? static_cast<const FSeinExtentsComponent*>(ExtentsStorage->GetComponentRaw(OtherHandle))
					: nullptr;
				if (!IsCollider(OtherExt)) continue;

				const bool bOtherImmovable = (OtherExt->Mobility != ESeinCollisionMobility::Movable);
				if (!bOtherImmovable && OtherHandle.Index <= SelfHandle.Index) continue;

				const ESeinCollisionResponse DefSelfToOther = ChannelDefaults.FindRef(OtherExt->ObjectType.Channel);
				const ESeinCollisionResponse DefOtherToSelf = ChannelDefaults.FindRef(SelfExt->ObjectType.Channel);
				const ESeinCollisionResponse RespSelfToOther = SelfExt->CollisionResponses.GetResponseForChannel(OtherExt->ObjectType.Channel, DefSelfToOther);
				const ESeinCollisionResponse RespOtherToSelf = OtherExt->CollisionResponses.GetResponseForChannel(SelfExt->ObjectType.Channel, DefOtherToSelf);
				if (ResolvePairResponse(RespSelfToOther, RespOtherToSelf) != ESeinCollisionResponse::Overlap) continue;

				FSeinEntity* OtherEntity = World.GetEntityPool().Get(OtherHandle);
				if (!OtherEntity) continue;

				FFixedVector Normal;
				FFixedPoint  Depth;
				if (!ComputeDeepestContact(SelfShapes, *OtherExt, OtherEntity->Transform, Normal, Depth)) continue;

				const bool bSelfLower = (SelfHandle.Index < OtherHandle.Index);
				const FSeinEntityHandle A = bSelfLower ? SelfHandle : OtherHandle;
				const FSeinEntityHandle B = bSelfLower ? OtherHandle : SelfHandle;
				Current.Add(MakePairKey(SelfHandle.Index, OtherHandle.Index),
					TPair<FSeinEntityHandle, FSeinEntityHandle>(A, B));
			}
		});

		// Diff against last tick. Collect begin/end keys, sort for deterministic
		// event order, then emit.
		TArray<uint64> BeginKeys;
		TArray<uint64> EndKeys;
		for (const TPair<uint64, TPair<FSeinEntityHandle, FSeinEntityHandle>>& KV : Current)
		{
			if (!ActiveOverlaps.Contains(KV.Key)) BeginKeys.Add(KV.Key);
		}
		for (const TPair<uint64, TPair<FSeinEntityHandle, FSeinEntityHandle>>& KV : ActiveOverlaps)
		{
			if (!Current.Contains(KV.Key)) EndKeys.Add(KV.Key);
		}
		BeginKeys.Sort();
		EndKeys.Sort();

		for (const uint64 Key : BeginKeys)
		{
			const TPair<FSeinEntityHandle, FSeinEntityHandle>& Pair = Current[Key];
			World.EnqueueVisualEvent(FSeinVisualEvent::MakeCollisionOverlapBeginEvent(Pair.Key, Pair.Value));
		}
		for (const uint64 Key : EndKeys)
		{
			const TPair<FSeinEntityHandle, FSeinEntityHandle>& Pair = ActiveOverlaps[Key];
			World.EnqueueVisualEvent(FSeinVisualEvent::MakeCollisionOverlapEndEvent(Pair.Key, Pair.Value));
		}

		ActiveOverlaps = MoveTemp(Current);
	}
};

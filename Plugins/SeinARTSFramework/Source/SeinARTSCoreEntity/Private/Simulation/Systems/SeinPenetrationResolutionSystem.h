/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPenetrationResolutionSystem.h
 * @brief   PostTick system that pushes overlapping entities apart along their
 *          separation axis. Universal safety net beneath per-movement
 *          anticipation — even if Layer 1 didn't fully avoid a collision, no
 *          two entities ever end a tick visibly inside each other.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Math/MathLib.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinSpatialHash.h"
#include "Components/SeinExtentsComponent.h"
#include "Components/SeinExtentsHelpers.h"  // SeinExtentsHelpers::BoundingRadius
#include "Components/SeinNavigationComponent.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"

/**
 * System: Penetration Resolution
 * Phase: PostTick | Priority: 10
 *
 * Runs after movement has finished moving everyone (PostTick) and before
 * StateHash (priority 100), so it contributes to the deterministic state
 * snapshot. Uses the spatial hash from PreTick to find candidate pairs in
 * O(K) per entity instead of O(N²).
 *
 * Algorithm:
 *   1. ForEachEntity (sorted by handle index — pool insertion order).
 *   2. For each entity with FootprintRadius > 0, query the spatial hash for
 *      neighbors within `FootprintRadius * 2` (max possible overlap distance
 *      between two entities of similar size; covers the worst case).
 *   3. For each neighbor whose handle index is GREATER THAN self's index,
 *      compute separation. Each pair is processed exactly once because
 *      the index-greater filter is symmetric.
 *   4. Mass-weighted symmetric push-apart along the separation axis.
 *      Mass = FootprintRadius² (heavier units displace lighter ones more).
 *   5. Run the full sweep TWO TIMES per tick — cheap relaxation that
 *      converges most cluster configurations without the cost of true PBD.
 *
 * Z is intentionally untouched — movements own ground snap, and pushing
 * units around vertically would defeat that.
 */
class FSeinPenetrationResolutionSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		// Two relaxation passes. The second catches residual overlaps in
		// dense clusters that the first pass couldn't fully separate.
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			ResolvePass(World);
		}
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PostTick; }
	virtual int32 GetPriority() const override { return 10; }
	virtual FName GetSystemName() const override { return TEXT("PenetrationResolution"); }

private:
	/** Derive the effective collision radius from an entity's Extents shapes
	 *  via the shared `SeinExtentsHelpers::BoundingRadius` helper. Capsule
	 *  → Shape.Radius; Box → diagonal `sqrt(HX² + HY²)` (the smallest circle
	 *  that fully contains the box — center-to-corner reach). Max across all
	 *  shapes for compound entities. Returns 0 when the entity has no
	 *  Extents — degrades to center-point-only passability. */
	static FFixedPoint GetExtentsRadius(USeinWorldSubsystem& World, FSeinEntityHandle Handle)
	{
		const FSeinExtentsComponent* Extents = World.GetComponent<FSeinExtentsComponent>(Handle);
		if (!Extents) return FFixedPoint::Zero;
		FFixedPoint MaxRadius = FFixedPoint::Zero;
		for (const FSeinExtentsShape& Shape : Extents->Shapes)
		{
			const FFixedPoint R = SeinExtentsHelpers::BoundingRadius(Shape);
			if (R > MaxRadius) MaxRadius = R;
		}
		return MaxRadius;
	}

	/** Same Extents → NavComp.FallbackFootprintRadius → 0 cascade used by
	 *  `USeinMovement::ResolveCollisionRadius` so penetration resolution
	 *  agrees with both collision (footprint sampling) and pathfinding
	 *  (path-request clearance) on body size. Without this, tanks with
	 *  Extents got pushed apart as if they were tiny (NavComp fallback
	 *  radius) instead of at their real body size. */
	static FFixedPoint ResolveCollisionRadius(
		USeinWorldSubsystem& World,
		FSeinEntityHandle Handle,
		const FSeinNavigationComponent* Nav)
	{
		FFixedPoint R = GetExtentsRadius(World, Handle);
		if (R <= FFixedPoint::Zero && Nav)
		{
			R = Nav->FallbackFootprintRadius;
		}
		return R;
	}

	static void ResolvePass(USeinWorldSubsystem& World)
	{
		const FSeinSpatialHash& Hash = World.GetSpatialHash();
		TArray<FSeinEntityHandle> Neighbors;

		// Precompute 8 unit-direction ring offsets (45° spacing) once per
		// pass. Scaled by each entity's collision radius below. Same
		// geometry as USeinMovement::CacheFootprintFromContext.
		FFixedPoint RingDirX[8];
		FFixedPoint RingDirY[8];
		for (int32 i = 0; i < 8; ++i)
		{
			const FFixedPoint Angle = (FFixedPoint::Pi * FFixedPoint::FromInt(i)) / FFixedPoint::FromInt(4);
			RingDirX[i] = SeinMath::Cos(Angle);
			RingDirY[i] = SeinMath::Sin(Angle);
		}

		// Footprint-aware passability check. Tests center + 8 ring
		// samples at the entity's Extents-derived collision radius.
		// When Radius <= 0 (no Extents), degrades to center-only.
		const auto IsFootprintPassable = [&World, &RingDirX, &RingDirY](const FFixedVector& Pos, FFixedPoint Radius) -> bool
		{
			if (!World.PassableResolver.IsBound()) return true;
			if (!World.PassableResolver.Execute(Pos)) return false;
			if (Radius <= FFixedPoint::Zero) return true;
			for (int32 i = 0; i < 8; ++i)
			{
				const FFixedVector SamplePos(
					Pos.X + RingDirX[i] * Radius,
					Pos.Y + RingDirY[i] * Radius,
					Pos.Z);
				if (!World.PassableResolver.Execute(SamplePos)) return false;
			}
			return true;
		};

		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle SelfHandle, FSeinEntity& SelfEntity)
		{
			// Penetration resolution requires a NavComp (gating only) plus an
			// effective collision radius via the cascade Extents → NavComp.
			// Entities with neither (no body) skip resolution entirely —
			// matches the prior "no nav data = no resolution" behaviour.
			const FSeinNavigationComponent* SelfNav = World.GetComponent<FSeinNavigationComponent>(SelfHandle);
			if (!SelfNav) return;
			const FFixedPoint SelfRadius = ResolveCollisionRadius(World, SelfHandle, SelfNav);
			if (SelfRadius <= FFixedPoint::Zero) return;

			const FFixedVector SelfPos = SelfEntity.Transform.GetLocation();
			// Query radius = self + max-other footprint. Using 2× self is the
			// tight upper bound when all units share roughly the same size; if
			// a much larger unit exists nearby, IT will see us during ITS
			// own iteration (with its larger query radius). Both directions
			// of the pair check still execute correctly because the index
			// filter only fires for one of them.
			const FFixedPoint QueryRadius = SelfRadius * FFixedPoint::Two;

			Neighbors.Reset();
			Hash.QueryRadius(SelfPos, QueryRadius, Neighbors, SelfHandle);

			for (const FSeinEntityHandle& OtherHandle : Neighbors)
			{
				// Index-greater filter: process each pair exactly once.
				// Pool iteration order is ascending, so by the time we get
				// here, anyone with a smaller index already had their turn
				// and we'd be doing redundant work.
				if (OtherHandle.Index <= SelfHandle.Index) continue;

				const FSeinNavigationComponent* OtherNav = World.GetComponent<FSeinNavigationComponent>(OtherHandle);
				if (!OtherNav) continue;
				const FFixedPoint OtherRadius = ResolveCollisionRadius(World, OtherHandle, OtherNav);
				if (OtherRadius <= FFixedPoint::Zero) continue;

				FSeinEntity* OtherEntity = World.GetEntityPool().Get(OtherHandle);
				if (!OtherEntity) continue;

				const FFixedVector OtherPos = OtherEntity->Transform.GetLocation();

				FFixedVector Delta = OtherPos - SelfPos;
				Delta.Z = FFixedPoint::Zero;
				const FFixedPoint DistSq = Delta.SizeSquared();
				const FFixedPoint MinDist = SelfRadius + OtherRadius;
				const FFixedPoint MinDistSq = MinDist * MinDist;
				if (DistSq >= MinDistSq) continue; // not overlapping

				// Resolve. Distance might be zero (perfect overlap), in
				// which case Delta direction is undefined — bias to +X to
				// keep the resolution deterministic. Won't happen often;
				// real motion produces non-zero deltas.
				FFixedVector PushDir;
				FFixedPoint Distance;
				if (DistSq <= FFixedPoint::Epsilon)
				{
					PushDir = FFixedVector(FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::Zero);
					Distance = FFixedPoint::Zero;
				}
				else
				{
					Distance = SeinMath::Sqrt(DistSq);
					PushDir = Delta / Distance; // unit vector self → other
				}

				const FFixedPoint Overlap = MinDist - Distance;

				// Mass-weighted split. MassA = SelfRadius², MassB = OtherRadius².
				// A's share of the push (in absolute terms) = MassB / (MassA + MassB)
				// — heavier "other" pushes "self" more. Symmetric.
				const FFixedPoint MassSelf = SelfRadius * SelfRadius;
				const FFixedPoint MassOther = OtherRadius * OtherRadius;
				const FFixedPoint MassSum = MassSelf + MassOther;
				const FFixedPoint SelfShare = (MassSum > FFixedPoint::Epsilon)
					? (MassOther / MassSum) : FFixedPoint::Half;
				const FFixedPoint OtherShare = FFixedPoint::One - SelfShare;

				// Self moves AWAY from other (along -PushDir), other moves
				// AWAY from self (along +PushDir).
				FFixedVector SelfNewPos = SelfPos - PushDir * (Overlap * SelfShare);
				FFixedVector OtherNewPos = OtherPos + PushDir * (Overlap * OtherShare);
				SelfNewPos.Z = SelfPos.Z;
				OtherNewPos.Z = OtherPos.Z;

				// Footprint-aware nav-passability + step-height gate. Tests
				// center + 8 ring samples at the entity's Extents-derived
				// collision radius, then checks that the ground height at the
				// proposed position is within MaxStepHeight of the entity's
				// current Z — prevents pushes onto wall-top cells that are
				// passable but vertically inaccessible from the side.
				//
				// Collision radius comes from FSeinExtentsComponent (the
				// entity's physical shape), NOT FootprintRadius (a nav
				// tuning knob). Entities without Extents degrade to center-
				// point-only checks (legacy behaviour).
				//
				// Axis-slide fallback: full push → X-only → Y-only → hold.
				// Resolver unbound (tests / nav-less games) → permit.
				const FFixedPoint SelfCollision = GetExtentsRadius(World, SelfHandle);
				const FFixedPoint OtherCollision = GetExtentsRadius(World, OtherHandle);

				// Max traversable height difference. Matches the movement
				// system's CachedMaxStepHeight default.
				constexpr int32 MaxStepHeightInt = 75;
				const FFixedPoint MaxStepHeight = FFixedPoint::FromInt(MaxStepHeightInt);

				const auto ProjectToPassable = [&IsFootprintPassable, &World, MaxStepHeight](
					const FFixedVector& OldPos, const FFixedVector& NewPos,
					FFixedPoint CollisionRadius) -> FFixedVector
				{
					auto IsValidStep = [&](const FFixedVector& Candidate) -> bool
					{
						if (!IsFootprintPassable(Candidate, CollisionRadius)) return false;
						// Step-height gate: if a height resolver is available,
						// reject positions whose ground Z differs too much from
						// the entity's current Z.
						if (World.HeightResolver.IsBound())
						{
							FFixedPoint GroundZ;
							if (World.HeightResolver.Execute(Candidate, GroundZ))
							{
								FFixedPoint Diff = GroundZ - OldPos.Z;
								if (Diff < FFixedPoint::Zero) Diff = -Diff;
								if (Diff > MaxStepHeight) return false;
							}
						}
						return true;
					};
					if (IsValidStep(NewPos)) return NewPos;
					// Try X-only slide.
					const FFixedVector XOnly(NewPos.X, OldPos.Y, NewPos.Z);
					if (IsValidStep(XOnly)) return XOnly;
					// Try Y-only slide.
					const FFixedVector YOnly(OldPos.X, NewPos.Y, NewPos.Z);
					if (IsValidStep(YOnly)) return YOnly;
					// Fully blocked — refuse the push, hold position.
					return OldPos;
				};
				SelfNewPos = ProjectToPassable(SelfPos, SelfNewPos, SelfCollision);
				OtherNewPos = ProjectToPassable(OtherPos, OtherNewPos, OtherCollision);

				SelfEntity.Transform.SetLocation(SelfNewPos);
				OtherEntity->Transform.SetLocation(OtherNewPos);
			}
		});
	}
};

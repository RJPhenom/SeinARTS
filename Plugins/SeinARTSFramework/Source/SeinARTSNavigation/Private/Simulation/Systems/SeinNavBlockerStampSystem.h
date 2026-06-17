/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavBlockerStampSystem.h
 * @brief   PreTick system that walks entities carrying FSeinExtentsComponent with
 *          bBlocksNav = true, expands each entity's Shapes into
 *          FSeinDynamicBlocker entries, and pushes the flat list to the
 *          active USeinNavigation. Pathfinding inside this same tick sees
 *          the fresh stamps.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Core/SeinSystemPriority.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinExtentsComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Stamping/SeinStampShape.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "SeinNavigation.h"
#include "Logging/LogMacros.h"

// LogSeinNavBlockerStamp (declared in SeinARTSNavigationLog.h): diagnostic log for
// the nav-blocker stamping pipeline. Off by default — `log LogSeinNavBlockerStamp
// Verbose` reports how many blockers each tick produces + which entities
// contributed. If the count is 0 no entity is supplying blocker data; if it's >0
// the issue is downstream (CollectDebugBlockerCells / DrawDynamicBlockersDebug).
#include "SeinARTSNavigationLog.h"

/**
 * System: Nav Blocker Stamp
 * Phase: PreTick | Priority: 7
 *
 * Runs after SpatialHash (priority 5) and before pathfinding (which happens
 * in AbilityExecution phase via MoveToAction's TickAction). Walks the entity
 * pool in handle-index order (deterministic — same as the spatial hash
 * rebuild), filters to entities whose FSeinExtentsComponent has `bBlocksNav` set,
 * expands each entity's Shapes into FSeinDynamicBlocker entries, and pushes
 * the flat list to the nav.
 *
 * Each blocker carries the owning entity's `BlockedNavLayerMask`; the
 * pathfinding overlay-rebuild gates per-agent via mask intersection, so
 * water blockers (mask = Default) ignore amphibious agents (mask = N0).
 *
 * **Dirty-bit short-circuit.** A per-entity snapshot cache tracks each
 * blocker entity's position bucket (25cm grid), forward-vector bucket
 * (~14° per step), shape count, and layer mask. Each tick walks entities
 * once, builds the new blocker list AND updates the cache in the same
 * pass. If NO entity's snapshot changed (and no entity was added/removed)
 * the call to `SetDynamicBlockers` is skipped entirely — the nav keeps
 * last tick's list, its OnNavigationMutated broadcast never fires, and
 * the per-FindPath overlay (Phase 1 bounded-Memzero) keeps using the
 * cached dirty rect. On a stationary scene this collapses the system's
 * tick to a cache-validation pass.
 *
 * Bucket sizes deliberately coarse — a 25cm position bucket and 14°
 * rotation bucket are well below "smallest meaningful nav change" for
 * any reasonable cell size, so false positives (saying dirty when
 * unchanged) are bounded and false negatives (saying clean when changed)
 * are zero.
 */
class FSeinNavBlockerStampSystem final : public ISeinSystem
{
public:
	explicit FSeinNavBlockerStampSystem(USeinNavigation* InNav)
		: Nav(InNav) {}

	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		USeinNavigation* NavPtr = Nav.Get();
		if (!NavPtr) return;

		++SysGen;
		bool bDirty = false;
		Blockers.Reset();

		// Hoist component-storage lookups out of the per-entity loop.
		// `GetComponent<T>()` does a hashmap lookup by UScriptStruct* per
		// call; resolving the storage once turns the per-entity cost into
		// a single indexed access. Cheap on its own, compounding with the
		// dirty-bit work below.
		ISeinComponentStorage* ExtentsStorage =
			World.GetComponentStorageRaw(FSeinExtentsComponent::StaticStruct());
		// Synthetic-radial fallback (no Extents authored) reads
		// FallbackFootprintRadius from the navigation component. The nav
		// component is the authoritative source for footprint info when
		// Extents is absent — matches the runtime cascade used by
		// USeinMovement::ResolveCollisionRadius (Extents → NavComp → 0).
		ISeinComponentStorage* NavStorage =
			World.GetComponentStorageRaw(FSeinNavigationComponent::StaticStruct());

		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			const FFixedVector EntityPos = Entity.Transform.GetLocation();
			const FFixedQuaternion EntityRot = Entity.Transform.Rotation;

			const FSeinExtentsComponent* Extents = ExtentsStorage
				? static_cast<const FSeinExtentsComponent*>(ExtentsStorage->GetComponentRaw(Handle))
				: nullptr;

			// Determine eligibility + snapshot signature parameters first;
			// build blockers only after, so non-eligible entities skip the
			// list-append work too.
			bool bEligibleExtents = false;
			int32 ShapeCount = 0;
			uint8 LayerMask = 0;

			if (Extents)
			{
				if (!Extents->bBlocksNav) return;
				if (Extents->Shapes.Num() == 0) return;
				if (Extents->BlockedNavLayerMask == 0) return;
				bEligibleExtents = true;
				ShapeCount = Extents->Shapes.Num();
				LayerMask = Extents->BlockedNavLayerMask;
			}

			const FSeinNavigationComponent* NavData = nullptr;
			if (!bEligibleExtents)
			{
				NavData = NavStorage
					? static_cast<const FSeinNavigationComponent*>(NavStorage->GetComponentRaw(Handle))
					: nullptr;
				if (!NavData || NavData->FallbackFootprintRadius <= FFixedPoint::Zero) return;
				ShapeCount = 1;        // synthetic radial shape
				LayerMask = 0x01;      // Default layer
			}

			// Compute snapshot for dirty detection. All fixed-point math —
			// `FFixedPoint::ToInt()` is deterministic (arithmetic shift,
			// floor toward -inf on x64 / arm).
			//
			// Position bucket: 25cm granularity. Smaller than any reasonable
			// cell size (typically 100cm, never less than ~50cm for an RTS),
			// so any meaningful cell change clears the bucket boundary
			// guaranteed-fast. False positives (bucket cross with no cell
			// cross) are bounded by 25cm; false negatives are impossible.
			//
			// Forward-vector bucket: x*4 / y*4. Forward components are in
			// [-1, 1]; ×4 then truncate gives 9 buckets per axis (~14° per
			// step). Captures rotation changes that matter for rect
			// footprints without false-positive-ing on micro-rotations.
			const int32 PosXBucket = EntityPos.X.ToInt() / 25;
			const int32 PosYBucket = EntityPos.Y.ToInt() / 25;
			const FFixedVector Forward = EntityRot.RotateVector(FFixedVector::ForwardVector);
			const int32 FwdXBucket = (Forward.X * FFixedPoint::FromInt(4)).ToInt();
			const int32 FwdYBucket = (Forward.Y * FFixedPoint::FromInt(4)).ToInt();

			FBlockerSnapshot& Cached = SnapshotCache.FindOrAdd(Handle.Index);
			const bool bExistedLastTick = (Cached.LastSeenSysGen + 1 == SysGen);
			const bool bSlotReuse = (Cached.EntityGeneration != Handle.Generation);
			if (!bExistedLastTick
				|| bSlotReuse
				|| Cached.PosXBucket != PosXBucket
				|| Cached.PosYBucket != PosYBucket
				|| Cached.FwdXBucket != FwdXBucket
				|| Cached.FwdYBucket != FwdYBucket
				|| Cached.ShapeCount != ShapeCount
				|| Cached.LayerMask != LayerMask)
			{
				bDirty = true;
			}
			Cached.EntityGeneration = Handle.Generation;
			Cached.PosXBucket = PosXBucket;
			Cached.PosYBucket = PosYBucket;
			Cached.FwdXBucket = FwdXBucket;
			Cached.FwdYBucket = FwdYBucket;
			Cached.ShapeCount = ShapeCount;
			Cached.LayerMask = LayerMask;
			Cached.LastSeenSysGen = SysGen;

			// Build the new blocker list (existing logic). Always rebuilt
			// even when clean — cheap struct copies — so when dirty we
			// already have it ready to push without a second walk.
			if (bEligibleExtents)
			{
				for (const FSeinExtentsShape& ExtShape : Extents->Shapes)
				{
					FSeinDynamicBlocker B;
					B.Shape = ExtShape.AsStampShape();
					B.EntityCenter = EntityPos;
					B.EntityRotation = EntityRot;
					B.Owner = Handle;
					B.BlockedNavLayerMask = Extents->BlockedNavLayerMask;
					Blockers.Add(B);
				}
			}
			else
			{
				FSeinStampShape Synthetic;
				Synthetic.Shape = ESeinStampShape::Radial;
				Synthetic.bEnabled = true;
				Synthetic.Radius = NavData->FallbackFootprintRadius;

				FSeinDynamicBlocker B;
				B.Shape = Synthetic;
				B.EntityCenter = EntityPos;
				B.EntityRotation = EntityRot;
				B.Owner = Handle;
				B.BlockedNavLayerMask = LayerMask;
				Blockers.Add(B);
			}
		});

		// Sweep stale cache entries (entity destroyed, or toggled bBlocksNav
		// off mid-game so the eligibility check above returned early).
		// LastSeenSysGen wasn't updated this tick → drop the entry and
		// mark dirty so the nav drops that blocker too.
		for (auto It = SnapshotCache.CreateIterator(); It; ++It)
		{
			if (It.Value().LastSeenSysGen != SysGen)
			{
				bDirty = true;
				It.RemoveCurrent();
			}
		}

		if (bDirty)
		{
			NavPtr->SetDynamicBlockers(Blockers);
			UE_LOG(LogSeinNavBlockerStamp, Verbose,
				TEXT("Stamped %d nav blocker(s) into nav (dirty: rebuilt)"),
				Blockers.Num());
		}
		else
		{
			UE_LOG(LogSeinNavBlockerStamp, VeryVerbose,
				TEXT("Nav blocker stamp skipped (clean): %d entities cached, no movement past bucket threshold"),
				SnapshotCache.Num());
		}
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PreTick; }
	virtual int32 GetPriority() const override { return SeinSystemPriority::NavBlockerStamp; }
	virtual FName GetSystemName() const override { return TEXT("NavBlockerStamp"); }

private:
	TWeakObjectPtr<USeinNavigation> Nav;
	TArray<FSeinDynamicBlocker> Blockers;

	/** Per-entity snapshot used by the dirty-bit short-circuit. Captures
	 *  the entity-state signature that affects the emitted blocker — pose
	 *  (bucketed), shape count, layer mask — plus the entity-handle
	 *  generation so a destroyed-and-respawned slot doesn't false-hit a
	 *  stale cache entry, plus the system tick generation so removed
	 *  entities are detected as cache entries that fell behind. */
	struct FBlockerSnapshot
	{
		int32 EntityGeneration = -1;
		int32 PosXBucket = 0;
		int32 PosYBucket = 0;
		int32 FwdXBucket = 0;
		int32 FwdYBucket = 0;
		int32 ShapeCount = 0;
		uint8 LayerMask = 0;
		uint64 LastSeenSysGen = 0;
	};

	/** Keyed by `FSeinEntityHandle::Index`. Generation mismatch on lookup
	 *  is handled inside the dirty check (slot reuse → treat as new). */
	TMap<int32, FBlockerSnapshot> SnapshotCache;

	/** Per-tick generation counter. Incremented each Tick; any cache entry
	 *  whose LastSeenSysGen falls behind got dropped from the entity walk
	 *  (destroyed or toggled-off) and is swept post-walk. */
	uint64 SysGen = 0;
};

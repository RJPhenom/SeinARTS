/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionResolverParallel.cpp
 *
 * The Jacobi separation strategy: a snapshot-read, deferred-apply relaxation
 * whose per-mover compute fans across worker threads via SeinParallelFor. Shares
 * the entire collision floor (shape build, channel/collider/mass helpers, the
 * hard-barrier CanOccupy gate, the overlap-event diff) with the Gauss-Seidel
 * default through the USeinCollisionResolver base; only the relaxation schedule
 * lives here.
 *
 * DETERMINISM (the heart). Each pass:
 *   1. gathers every movable collider into a flat array (serial);
 *   2. PARALLEL: for mover i, reads its FROZEN snapshot position and every
 *      neighbour's FROZEN snapshot position (never a mid-pass move), walks
 *      neighbours in the handle-sorted order QueryRadius returns, accumulates a
 *      barrier-gated running position from ONLY its own share of each overlap,
 *      and writes the result to NewPos[i] (a disjoint slot);
 *   3. SERIAL: writes NewPos[i] back to each mover's transform.
 * The compute is a pure function of immutable state (snapshot transforms + the
 * PreTick-built broadphase + Extents storage) writing a disjoint slot, the
 * per-neighbour loop is sequential in handle order, and all math is fixed-point —
 * so the result is independent of thread count, provable on the Sein.Sim.Parallel
 * 0-vs-1 canonical-root gate. The serial apply is the only mutation and it is disjoint
 * per self; deferring it out of the compute is what keeps the parallel reads on a
 * consistent snapshot.
 */

#include "Collision/SeinCollisionResolverParallel.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Collision/SeinCollisionSpatialHash.h"
#include "Components/SeinExtentsHelpers.h"
#include "Settings/PluginSettings.h"
#include "Core/SeinParallel.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

void USeinCollisionResolverParallel::Resolve(USeinWorldSubsystem& World)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Collision_Parallel_Resolve);
	// Shared floor: channel default responses resolved once per tick. Identical
	// to the Gauss-Seidel resolver.
	TMap<FName, ESeinCollisionResponse> ChannelDefaults;
	BuildChannelDefaults(ChannelDefaults);
	if (ChannelDefaults.Num() == 0) return; // no enabled channels → nothing to resolve

	// Mass-ratio cutoff — the SAME setting and read the default uses, so the
	// mass behaviour is identical pair-for-pair.
	const USeinARTSCoreSettings* MassSettings = GetDefault<USeinARTSCoreSettings>();
	const int32 RawCutoff = MassSettings ? MassSettings->CollisionMassRatioCutoff : 8;
	const FFixedPoint MassRatioCutoff = FFixedPoint::FromInt(RawCutoff > 1 ? RawCutoff : 1);

	// Fixed Jacobi passes (default 8; Jacobi converges slower than GS's 4).
	const int32 Passes = (NumPasses > 0) ? NumPasses : 1;
	for (int32 Pass = 0; Pass < Passes; ++Pass)
	{
		if (!JacobiPass(World, ChannelDefaults, MassRatioCutoff))
		{
			// This pass made no exact fixed-point transform changes. Repeating
			// the same deterministic calculation from the same state cannot
			// create one, so the remaining relaxation passes are redundant.
			break;
		}
	}

	// Overlap events run on the SETTLED positions (after Block separation),
	// exactly like the default.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Collision_OverlapEvents);
		DetectOverlapsAndEmit(World, ChannelDefaults);
	}
}

bool USeinCollisionResolverParallel::JacobiPass(USeinWorldSubsystem& World, const TMap<FName, ESeinCollisionResponse>& ChannelDefaults, const FFixedPoint MassRatioCutoff)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Collision_JacobiPass);
	const FSeinCollisionSpatialHash& Hash = World.GetCollisionSpatialHash();
	const FFixedPoint CellSize = Hash.GetCellSize();

	// Hoist the Extents storage once per pass (see the default's ResolvePass):
	// GetComponent<T>() is a hashmap lookup by UScriptStruct* per call; resolving
	// it once makes every per-self / per-neighbour fetch an O(1) indexed get.
	const ISeinComponentStorage* ExtentsStorage =
		World.GetComponentStorageRaw(
			FSeinExtentsComponent::StaticStruct());
	if (!ExtentsStorage) return false;

	// ------------------------------------------------------------------
	// 1) Gather every MOVABLE collider into a flat, indexable array (serial).
	//    Same collider test as the default's ResolvePass: IsCollider + Movable
	//    mobility + positive radius. Cache the Extents pointer, bounding radius,
	//    and mass so the parallel compute below does no extra lookups. The
	//    Extents storage does not reallocate during a pass (no AddComponent /
	//    DestroyEntity here), so the cached pointers stay valid for the pass.
	// ------------------------------------------------------------------
	struct FMover
	{
		FSeinEntityHandle            Handle;
		const FSeinExtentsComponent* Ext;
		FFixedPoint                  Radius;
		FFixedPoint                  Mass;
	};
	TArray<FMover> Movers;
	Movers.Reserve(World.GetEntityPool().GetActiveCount());
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Collision_GatherMovers);
		World.GetEntityPool().ForEachEntity([&](
			FSeinEntityHandle SelfHandle,
			const FSeinEntity& /*SelfEntity*/)
		{
			const FSeinExtentsComponent* SelfExt =
				static_cast<const FSeinExtentsComponent*>(ExtentsStorage->GetComponentRaw(SelfHandle));
			if (!IsCollider(SelfExt)) return;
			// Non-movable colliders (Static + Stationary) never initiate a push — they
			// are only ever the queried neighbour of a movable, so skip them as "self".
			if (SelfExt->Mobility != ESeinCollisionMobility::Movable) return;

			const FFixedPoint SelfRadius = SeinExtentsHelpers::GetColliderBoundingRadius(*SelfExt);
			if (SelfRadius <= FFixedPoint::Zero) return;

			Movers.Add(FMover{ SelfHandle, SelfExt, SelfRadius, ResolveColliderMass(*SelfExt) });
		});
	}

	if (Movers.Num() == 0) return false;

	// Output slot per mover — the disjoint write target for the parallel compute.
	TArray<FFixedVector> NewPos;
	NewPos.SetNumUninitialized(Movers.Num());

	// ------------------------------------------------------------------
	// 2) PARALLEL compute. Each mover reads ONLY immutable state (its frozen
	//    snapshot transform + neighbours' frozen snapshot transforms + the
	//    PreTick broadphase + Extents storage) and writes ONLY NewPos[i].
	//    bForceSerial when an authoritative-destination provider is live: provider
	//    callback thread-safety is not part of the registry contract, so the
	//    CanOccupy executes only on the main thread — exactly the nav-containment
	//    pattern. `Sein.Sim.Parallel 0` forces serial too; the result is
	//    bit-identical either way.
	// ------------------------------------------------------------------
	const bool bForceSerial =
		World.HasAuthoritativeDestinationProviders();

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Collision_JacobiCompute);
		SeinParallelFor(Movers.Num(), [&](int32 i)
		{
		const FMover& Self = Movers[i];
		const FSeinEntity* SelfEntityPtr = World.GetEntityPool().Get(Self.Handle);
		if (!SelfEntityPtr)
		{
			// Defensive: a gathered handle should still be live this pass. Keep
			// the slot consistent so the apply is a no-op.
			NewPos[i] = FFixedVector::ZeroVector;
			return;
		}

		// SNAPSHOT position — read once, NEVER written in this phase. Both the
		// running accumulation and every narrowphase read derive from frozen
		// snapshots, so no mover sees another mover's mid-pass move (the Jacobi
		// difference from Gauss-Seidel, and what makes the compute parallel-safe).
		const FFixedVector SelfPos0 = SelfEntityPtr->Transform.GetLocation();

		// Per-body neighbour scratch + self-shape scratch — MUST be locals (one
		// per body invocation) so concurrent QueryRadius / BuildShapes2D calls
		// never share a buffer.
		TArray<FSeinEntityHandle> Neighbors;
		TArray<FCollisionShape2D> SelfShapes;

		// Footprint-stamped broadphase: a query radius covering self's footprint
		// finds any overlapping collider; +1 cell of slack absorbs drift.
		const FFixedPoint QueryRadius = Self.Radius + CellSize;
		Hash.QueryRadius(SelfPos0, QueryRadius, Neighbors, Self.Handle);
		if (Neighbors.Num() == 0)
		{
			NewPos[i] = SelfPos0;
			return;
		}

		// Build the self shapes only when the broadphase found a possible pair.
		// Settled, isolated bodies therefore avoid all narrowphase setup.
		BuildShapes2D(*Self.Ext, SelfEntityPtr->Transform, SelfShapes);

		// Barrier-gated running position. Starts at the snapshot and only ever
		// accepts a push that CanOccupy allows (never crosses a wall / the grid
		// edge); a rejected push is simply skipped. Each accepted push moves the
		// running position, so successive overlaps separate from where the unit
		// has tentatively reached this pass — but ALL reads stay on the frozen
		// snapshot, so the result is independent of neighbour iteration order
		// beyond the handle-sorted sequence QueryRadius guarantees.
		FFixedVector Running = SelfPos0;

		// Walk neighbours in the handle-sorted order QueryRadius returns. NO
		// pair-once gate (no OtherIndex <= SelfIndex skip): each mover computes
		// ONLY ITS OWN share, so both sides of a pair resolve independently in
		// their own compute.
		for (const FSeinEntityHandle& OtherHandle : Neighbors)
		{
			const FSeinExtentsComponent* OtherExt =
				static_cast<const FSeinExtentsComponent*>(ExtentsStorage->GetComponentRaw(OtherHandle));
			if (!IsCollider(OtherExt)) continue;

			const bool bOtherImmovable = (OtherExt->Mobility != ESeinCollisionMobility::Movable);

			// Effective response = weaker of the two sides (Block needs both) —
			// the same gating as the default's ResolvePass.
			if (ResolvePairFor(*Self.Ext, *OtherExt, ChannelDefaults) != ESeinCollisionResponse::Block) continue;

			const FSeinEntity* OtherEntityPtr = World.GetEntityPool().Get(OtherHandle);
			if (!OtherEntityPtr) continue;

			// Narrowphase on SNAPSHOT positions of BOTH self and other — reads
			// only; never the moved position. This is the Jacobi difference from
			// GS and is intentional: a consistent frozen snapshot is what makes
			// the parallel compute deterministic.
			FFixedVector Normal;
			FFixedPoint  Depth;
			if (!ComputeDeepestContact(SelfShapes, *OtherExt, OtherEntityPtr->Transform, Normal, Depth)) continue;
			if (Depth <= FFixedPoint::Zero) continue;

			// Self's share ONLY (same mass logic as the default, computed for just
			// this side). Immovable other = infinite mass → self absorbs the whole
			// separation. Else the mass-ratio cutoff, then the mass-weighted split.
			FFixedPoint SelfShare;
			if (bOtherImmovable)
			{
				SelfShare = FFixedPoint::One;
			}
			else
			{
				const FFixedPoint MassOther = ResolveColliderMass(*OtherExt);
				if (Self.Mass >= MassOther * MassRatioCutoff)
				{
					SelfShare = FFixedPoint::Zero;  // self much heavier → unpushable here
				}
				else if (MassOther >= Self.Mass * MassRatioCutoff)
				{
					SelfShare = FFixedPoint::One;   // other much heavier → self takes it all
				}
				else
				{
					const FFixedPoint MassSum = Self.Mass + MassOther;
					SelfShare = (MassSum > FFixedPoint::Epsilon) ? (MassOther / MassSum) : FFixedPoint::Half;
				}
			}
			if (SelfShare <= FFixedPoint::Zero) continue;

			// Self moves along -Normal (away from other), scaled by its share and
			// the relaxation factor; preserve the snapshot Z. Per-push barrier
			// gate: accept only if the candidate keeps the FOOTPRINT off walls /
			// the grid edge (cover exempt) — else skip this push, so the body
			// never crosses a barrier even mid-accumulation.
			const FFixedVector Push = -Normal * (Depth * SelfShare * Relaxation);
			if (Push == FFixedVector::ZeroVector) continue;
			FFixedVector Candidate = Running + Push;
			Candidate.Z = SelfPos0.Z;
			if (CanOccupy(
				World,
				Self.Handle,
				Candidate,
				Self.Radius,
				bForceSerial))
			{
				Running = Candidate;
			}
		}

			NewPos[i] = Running;
		}, bForceSerial);
	}

	// ------------------------------------------------------------------
	// 3) SERIAL apply. Disjoint per self (each writes only its own transform),
	//    deferred out of the compute so no mover read another's mid-pass move.
	// ------------------------------------------------------------------
	bool bAnyChanged = false;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Collision_Apply);
		for (int32 i = 0; i < Movers.Num(); ++i)
		{
			const FSeinEntity* CurrentEntity =
				World.GetEntity(Movers[i].Handle);
			if (!CurrentEntity
				|| CurrentEntity->Transform.GetLocation() == NewPos[i])
			{
				continue;
			}
			FSeinEntity* SelfEntityPtr =
				World.GetEntityMutable(Movers[i].Handle);
			if (!SelfEntityPtr) continue;
			SelfEntityPtr->Transform.SetLocation(NewPos[i]);
			bAnyChanged = true;
		}
	}
	return bAnyChanged;
}

namespace
{
	const UClass* FindNearestNativeCollisionClass(const UClass* Class)
	{
		while (Class && !Class->HasAnyClassFlags(CLASS_Native))
		{
			Class = Class->GetSuperClass();
		}
		return Class;
	}
}

bool USeinCollisionResolverParallel::ComputeStateCoverageClaim(
	FSeinCollisionResolverStateCoverageClaim& OutClaim,
	FString& OutError) const
{
	OutClaim = {};
	OutError.Reset();
	const UClass* NativeClass =
		FindNearestNativeCollisionClass(GetClass());
	if (NativeClass != USeinCollisionResolverParallel::StaticClass())
	{
		OutError = FString::Printf(
			TEXT("Native collision-resolver subclass '%s' must explicitly claim exact mutable-state coverage."),
			*GetClass()->GetPathName());
		return false;
	}
	OutClaim.StableImplementationId =
		TEXT("seinarts.collision.resolver.parallel");
	OutClaim.BehaviorRevision = 2;
	OutClaim.CoverageRevision = 1;
	OutClaim.StateCoverage =
		ESeinCollisionResolverStateCoverage::Stateless;
	return true;
}

bool USeinCollisionResolverParallel::ComputeResolutionConfigDigest(
	FGuid& OutDigest,
	FString& OutError) const
{
	OutDigest.Invalidate();
	OutError.Reset();
	const UClass* NativeClass =
		FindNearestNativeCollisionClass(GetClass());
	if (NativeClass != USeinCollisionResolverParallel::StaticClass())
	{
		OutError = FString::Printf(
			TEXT("Native collision-resolver subclass '%s' must override ComputeResolutionConfigDigest to cover its own resolution tuning."),
			*GetClass()->GetPathName());
		return false;
	}
	FSeinCanonicalDigestWriter Writer(
		TEXT("SeinARTS.Collision.Parallel.ResolutionConfig"), 1);
	if (!Writer.WriteString(GetClass()->GetPathName())
		|| !Writer.WriteInt32(NumPasses)
		|| !Writer.WriteInt64(Relaxation.Value))
	{
		OutError = Writer.GetError();
		return false;
	}
	return Writer.Finalize(OutDigest, OutError);
}

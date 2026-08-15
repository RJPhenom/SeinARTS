/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionResolverDefault.cpp
 *
 * The Gauss-Seidel separation strategy. The math, pass count, mass-weighting,
 * hard-barrier gate, overlap-event diff, and narrowphase are unchanged from when
 * they lived inline in FSeinCollisionResolutionSystem. The shared floor (shape
 * build, channel/collider/mass helpers, CanOccupy gate, overlap diff) lives on
 * the USeinCollisionResolver base; only the Gauss-Seidel pass + per-tick driver
 * stay here. ResolvePass now calls the base CanOccupy instead of the former
 * local `CanOccupy` lambda — same logic, same byte-for-byte separation.
 */

#include "Collision/SeinCollisionResolverDefault.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Collision/SeinCollisionSpatialHash.h"
#include "Components/SeinExtentsHelpers.h"
#include "Settings/PluginSettings.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

void USeinCollisionResolverDefault::Resolve(USeinWorldSubsystem& World)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Collision_Default_Resolve);
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
	const bool bMayUseAuthoritativeDestination =
		World.HasAuthoritativeDestinationProviders();

	constexpr int32 NumPasses = 4;
	for (int32 Pass = 0; Pass < NumPasses; ++Pass)
	{
		// A no-write pass is a fixed point: rerunning the same deterministic
		// contacts against identical transforms cannot produce a later push.
		// Dense moving clusters retain all four bounded relaxation passes; idle
		// or already-settled worlds stop paying for redundant scans.
		if (!ResolvePass(
			World,
			ChannelDefaults,
			MassRatioCutoff,
			bMayUseAuthoritativeDestination))
		{
			break;
		}
	}

	// Overlap events run on the SETTLED positions (after Block separation),
	// so Overlap-responding pairs report their final overlap state this tick.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Collision_OverlapEvents);
		DetectOverlapsAndEmit(World, ChannelDefaults);
	}
}

bool USeinCollisionResolverDefault::ResolvePass(
	USeinWorldSubsystem& World,
	const TMap<FName, ESeinCollisionResponse>& ChannelDefaults,
	FFixedPoint MassRatioCutoff,
	bool bMayUseAuthoritativeDestination)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Collision_GaussSeidelPass);
	const FSeinCollisionSpatialHash& Hash = World.GetCollisionSpatialHash();
	const FFixedPoint CellSize = Hash.GetCellSize();
	TArray<FSeinEntityHandle> Neighbors;

	// Hoist the Extents storage once per pass: GetComponent<T>() does a
	// hashmap lookup by UScriptStruct* per call; resolving the storage once
	// makes every per-self / per-neighbour fetch an O(1) indexed get.
	const ISeinComponentStorage* ExtentsStorage =
		World.GetComponentStorageRaw(
			FSeinExtentsComponent::StaticStruct());
	// Reused scratch for the self collider's pre-built shapes (see below).
	TArray<FCollisionShape2D> SelfShapes;

	FSeinEntityPool* MutablePool =
		World.GetEntityPoolMutable();
	if (!MutablePool) return false;
	bool bAnyTransformChanged = false;
	MutablePool->ForEachEntity([&](
		FSeinEntityHandle SelfHandle,
		FSeinEntity& SelfEntity)
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
		bool bSelfShapesDirty = true;

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
			const ESeinCollisionResponse Effective = ResolvePairFor(*SelfExt, *OtherExt, ChannelDefaults);

			// Only Block pushes. Ignore → nothing; Overlap → no push (overlap
			// begin/end events are emitted by the overlap system, not here).
			if (Effective != ESeinCollisionResponse::Block) continue;

			FSeinEntity* OtherEntity =
				MutablePool->Get(OtherHandle);
			if (!OtherEntity) continue;

			FFixedVector Normal;
			FFixedPoint  Depth;
			// Rebuild from the CURRENT transform only when an earlier accepted push
			// actually changed self. Most candidates do not overlap, so rebuilding
			// identical shape arrays for every broadphase neighbour was pure cost.
			if (bSelfShapesDirty)
			{
				BuildShapes2D(*SelfExt, SelfEntity.Transform, SelfShapes);
				bSelfShapesDirty = false;
			}
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
			if (CanOccupy(
				World,
				SelfHandle,
				SelfNew,
				SelfRadius,
				bMayUseAuthoritativeDestination))
			{
				if (SelfNew != SelfPosNow)
				{
					SelfEntity.Transform.SetLocation(SelfNew);
					bSelfShapesDirty = true;
					bAnyTransformChanged = true;
				}
			}

			// Other moves along +Normal, unless it's an immovable static —
			// same footprint-barrier hold rule, using its own collider radius.
			if (!bOtherImmovable)
			{
				const FFixedVector OtherPosNow = OtherEntity->Transform.GetLocation();
				FFixedVector OtherNew = OtherPosNow + Normal * (Depth * OtherShare);
				OtherNew.Z = OtherPosNow.Z;
				const FFixedPoint OtherRadius = SeinExtentsHelpers::GetColliderBoundingRadius(*OtherExt);
				if (OtherNew != OtherPosNow
					&& CanOccupy(
						World,
						OtherHandle,
						OtherNew,
						OtherRadius,
						bMayUseAuthoritativeDestination))
				{
					OtherEntity->Transform.SetLocation(OtherNew);
					bAnyTransformChanged = true;
				}
			}
		}
	});
	return bAnyTransformChanged;
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

bool USeinCollisionResolverDefault::ComputeStateCoverageClaim(
	FSeinCollisionResolverStateCoverageClaim& OutClaim,
	FString& OutError) const
{
	const UClass* NativeClass =
		FindNearestNativeCollisionClass(GetClass());
	if (NativeClass != USeinCollisionResolverDefault::StaticClass())
	{
		OutClaim = {};
		OutError = FString::Printf(
			TEXT("Native collision-resolver subclass '%s' must explicitly claim exact mutable-state coverage."),
			*GetClass()->GetPathName());
		return false;
	}
	return ComputeDefaultResolverStateCoverageClaim(OutClaim, OutError);
}

bool USeinCollisionResolverDefault::ComputeDefaultResolverStateCoverageClaim(
	FSeinCollisionResolverStateCoverageClaim& OutClaim,
	FString& OutError) const
{
	OutClaim = {};
	OutError.Reset();
	OutClaim.StableImplementationId =
		TEXT("seinarts.collision.resolver.default");
	OutClaim.BehaviorRevision = 2;
	OutClaim.CoverageRevision = 1;
	OutClaim.StateCoverage =
		ESeinCollisionResolverStateCoverage::Stateless;
	return true;
}

bool USeinCollisionResolverDefault::ComputeResolutionConfigDigest(
	FGuid& OutDigest,
	FString& OutError) const
{
	OutDigest.Invalidate();
	OutError.Reset();
	const UClass* NativeClass =
		FindNearestNativeCollisionClass(GetClass());
	if (NativeClass != USeinCollisionResolverDefault::StaticClass())
	{
		OutError = FString::Printf(
			TEXT("Native collision-resolver subclass '%s' must override ComputeResolutionConfigDigest to cover its own resolution tuning."),
			*GetClass()->GetPathName());
		return false;
	}
	FSeinCanonicalDigestWriter Writer(
		TEXT("SeinARTS.Collision.Default.ResolutionConfig"), 1);
	if (!Writer.WriteString(GetClass()->GetPathName()))
	{
		OutError = Writer.GetError();
		return false;
	}
	return Writer.Finalize(OutDigest, OutError);
}

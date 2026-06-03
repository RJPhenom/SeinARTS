/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverDefault.cpp
 */

#include "System/SeinCoverDefault.h"
#include "Components/SeinCoverComponent.h"
#include "Lib/SeinCoverGeometry.h"
#include "Tags/SeinCoverGameplayTags.h"

#include "SeinFogOfWar.h"
#include "SeinFogOfWarSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Types/Entity.h"
#include "Math/MathLib.h"

#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinCoverDefault, Log, All);

namespace SeinCoverDefaultLocal
{
	/** Look up the world's fog-of-war system, or nullptr if FoW is not
	 *  available (module not loaded, subsystem not initialized for this
	 *  world). Cover queries gracefully degrade to "no filtering" when
	 *  FoW is missing — useful for tests, fog-less game modes, and
	 *  combat scripts that pass invalid Observer. */
	static USeinFogOfWar* GetFog(USeinWorldSubsystem* WorldSub)
	{
		if (!WorldSub) return nullptr;
		UWorld* World = WorldSub->GetWorld();
		if (!World) return nullptr;
		USeinFogOfWarSubsystem* Sub = World->GetSubsystem<USeinFogOfWarSubsystem>();
		return Sub ? Sub->GetFogOfWar() : nullptr;
	}

	/** Whether `ProviderHandle` is visible to `Observer` per the FoW
	 *  visibility policy (AlwaysVisible / VisibleOnceExplored /
	 *  VisionLayersOnly + owner-sees-own). Returns true unconditionally
	 *  when Observer is invalid (caller didn't ask for filtering) or when
	 *  FoW is unavailable (no module / no subsystem). */
	static bool IsProviderVisibleToObserver(USeinWorldSubsystem* WorldSub,
		FSeinEntityHandle ProviderHandle, FSeinPlayerID Observer)
	{
		if (!Observer.IsValid() || !WorldSub) return true;
		USeinFogOfWar* Fog = GetFog(WorldSub);
		if (!Fog) return true;
		return Fog->IsEntityVisibleToObserver(Observer, *WorldSub, ProviderHandle);
	}

	/** Canonical cover-quality priority: Heavy > Light > <designer tag> > Negative.
	 *  Mirrors QueryBestCoverQualityAt's ordering so the slot resolver's
	 *  "best overlapping area" pick and dedup tie-break agree with point cover
	 *  queries. Higher = stronger protection; invalid tag = 0. */
	static int32 CoverQualityPriority(const FGameplayTag& Tag)
	{
		if (Tag == SeinCoverTags::Cover_Heavy)    return 4;
		if (Tag == SeinCoverTags::Cover_Light)    return 3;
		if (Tag == SeinCoverTags::Cover_Negative) return 1;
		return Tag.IsValid() ? 2 : 0;
	}

	/** Conservative upper bound on the distance from a provider's actor
	 *  center to any of its slot world positions or any point inside its
	 *  cover area. Used to prefilter providers in query loops — if the
	 *  query origin is farther than `Radius + Reach`, the provider can't
	 *  possibly contribute and we skip its per-slot / per-shape work
	 *  entirely.
	 *
	 *  Returns the area's local-space diagonal magnitude (sqrt of summed
	 *  squared extents for Box, radius for Sphere). Adds a 200cm margin
	 *  to cover edge-mode slots that sit just outside the area body —
	 *  prefilter must NEVER reject a provider that could legitimately
	 *  contribute, so we lean toward over-inclusion. */
	static FFixedPoint ComputeProviderReach(const FSeinCoverComponent* Data)
	{
		if (!Data) return FFixedPoint::Zero;
		FFixedPoint AreaReach = FFixedPoint::Zero;
		switch (Data->Area.Shape)
		{
			case ESeinCoverAreaShape::Box:
				// L₂ norm of half-extents (= half-diagonal).
				AreaReach = Data->Area.LocalExtents.Size();
				break;
			case ESeinCoverAreaShape::Sphere:
				AreaReach = Data->Area.LocalExtents.X;
				break;
			case ESeinCoverAreaShape::None:
			default:
				break;
		}
		// 200cm margin covers edge-mode slots that sit OUTSIDE the area body
		// (GenerateSlotsAlongEdge places them ~60cm out). Generous on purpose
		// — false negatives in the prefilter would silently break cover snap.
		return AreaReach + FFixedPoint::FromInt(200);
	}
}

void USeinCoverDefault::OnCoverSystemDeinitialized()
{
	RegisteredProviders.Reset();
	RegisteredProviderReaches.Reset();
	Super::OnCoverSystemDeinitialized();
}

void USeinCoverDefault::RegisterProvider(FSeinEntityHandle ProviderHandle)
{
	if (!ProviderHandle.IsValid()) return;
	// Dedup by handle. If already present, refresh the cached reach so a
	// re-register (rare; designer hot-edited the data) picks up the latest
	// area dimensions.
	const int32 ExistingIdx = RegisteredProviders.Find(ProviderHandle);

	// Compute reach from the provider's data — cache once at registration
	// (provider data is immutable after authoring for the common path).
	FFixedPoint Reach = FFixedPoint::Zero;
	if (USeinWorldSubsystem* WorldSub = World.Get())
	{
		if (const FSeinCoverComponent* Data = WorldSub->GetComponent<FSeinCoverComponent>(ProviderHandle))
		{
			Reach = SeinCoverDefaultLocal::ComputeProviderReach(Data);
		}
	}

	if (ExistingIdx != INDEX_NONE)
	{
		RegisteredProviderReaches[ExistingIdx] = Reach;
		return;
	}

	RegisteredProviders.Add(ProviderHandle);
	RegisteredProviderReaches.Add(Reach);
	UE_LOG(LogSeinCoverDefault, Verbose,
		TEXT("RegisterProvider: %s (reach=%.1f; now %d total)"),
		*ProviderHandle.ToString(), Reach.ToFloat(), RegisteredProviders.Num());
}

void USeinCoverDefault::UnregisterProvider(FSeinEntityHandle ProviderHandle)
{
	const int32 Idx = RegisteredProviders.Find(ProviderHandle);
	if (Idx == INDEX_NONE) return;
	RegisteredProviders.RemoveAt(Idx);
	RegisteredProviderReaches.RemoveAt(Idx);
	UE_LOG(LogSeinCoverDefault, Verbose,
		TEXT("UnregisterProvider: %s (now %d total)"),
		*ProviderHandle.ToString(), RegisteredProviders.Num());
}

TArray<FSeinCoverContext> USeinCoverDefault::QueryCoverAt(FFixedVector WorldPoint,
	FSeinPlayerID Observer) const
{
	TArray<FSeinCoverContext> Result;

	USeinWorldSubsystem* WorldSub = World.Get();
	if (!WorldSub || RegisteredProviders.Num() == 0) return Result;

	// One unified containment test per provider: is the query point inside
	// the provider's `Area` volume? Slots no longer contribute cover contexts
	// directly — they're pure formation snap targets (see FSeinCoverSlot
	// docstring). This means a unit moving between adjacent slots inside the
	// area stays in cover continuously, rather than dropping out of cover
	// briefly while transiting the gap between slot SlotMatchRadius circles.
	//
	// Per-provider distance prefilter: skip providers whose actor center is
	// farther than `Reach` from the query point. Reach is cached per provider
	// at RegisterProvider time (= area diagonal + small slot margin). For
	// cover-rich maps this drops the iteration from "all providers" to "the
	// few providers near the query point".
	for (int32 ProviderIdx = 0; ProviderIdx < RegisteredProviders.Num(); ++ProviderIdx)
	{
		const FSeinEntityHandle& ProviderHandle = RegisteredProviders[ProviderIdx];
		const FFixedPoint Reach = RegisteredProviderReaches.IsValidIndex(ProviderIdx)
			? RegisteredProviderReaches[ProviderIdx] : FFixedPoint::Zero;

		const FSeinEntity* Entity = WorldSub->GetEntity(ProviderHandle);
		if (!Entity) continue;
		const FFixedVector ProviderLocation = Entity->Transform.GetLocation();

		// Coarse distance gate — if WorldPoint is outside the provider's
		// `Reach` bubble, it can't be inside the area. SquareCompare avoids
		// a sqrt; Reach itself was precomputed once at registration.
		if (Reach > FFixedPoint::Zero)
		{
			const FFixedPoint ReachSq = Reach * Reach;
			if (FFixedVector::DistSquared(ProviderLocation, WorldPoint) > ReachSq) continue;
		}

		// Per-observer fog-visibility filter. Cover the observer can't see
		// shouldn't contribute to their preview / snap / minimap queries.
		// Skipped when Observer is invalid (combat scripts, etc.) — see the
		// docstring on the API for the semantics.
		if (!SeinCoverDefaultLocal::IsProviderVisibleToObserver(WorldSub, ProviderHandle, Observer))
		{
			continue;
		}

		const FSeinCoverComponent* Data = WorldSub->GetComponent<FSeinCoverComponent>(ProviderHandle);
		if (!Data) continue;
		if (Data->Area.Shape == ESeinCoverAreaShape::None) continue;

		const FFixedQuaternion ProviderRotation = Entity->Transform.GetQuaternionRotation();

		// Bring the query point into the provider's local space so the
		// inclusion check runs in axis-aligned terms regardless of provider
		// rotation. This is what lets a designer rotate a sandbag wall in
		// the level and have its cover area rotate with it.
		const FFixedVector LocalPoint =
			ProviderRotation.Inverse().RotateVector(WorldPoint - ProviderLocation);

		bool bInside = false;
		switch (Data->Area.Shape)
		{
			case ESeinCoverAreaShape::Box:
			{
				const FFixedPoint AbsX = SeinMath::Abs(LocalPoint.X);
				const FFixedPoint AbsY = SeinMath::Abs(LocalPoint.Y);
				const FFixedPoint AbsZ = SeinMath::Abs(LocalPoint.Z);
				bInside = (AbsX <= Data->Area.LocalExtents.X)
					   && (AbsY <= Data->Area.LocalExtents.Y)
					   && (AbsZ <= Data->Area.LocalExtents.Z);
				break;
			}
			case ESeinCoverAreaShape::Sphere:
			{
				const FFixedPoint RadiusSq = Data->Area.LocalExtents.X * Data->Area.LocalExtents.X;
				bInside = (LocalPoint.SizeSquared() <= RadiusSq);
				break;
			}
			default:
				break;
		}

		if (bInside)
		{
			FSeinCoverContext Ctx;
			Ctx.QualityTag     = Data->QualityTag;
			Ctx.ProviderHandle = ProviderHandle;
			// Directionality is a provider-level property now — combat code
			// reads this flag and calls `SeinGetCoverDirection` only when
			// true. For omni cover (foxholes etc.) it applies the quality
			// modifier unconditionally.
			Ctx.bIsDirectional = Data->bIsDirectional;
			Result.Add(Ctx);
		}
	}

	return Result;
}

FGameplayTag USeinCoverDefault::QueryBestCoverQualityAt(FFixedVector WorldPoint,
	FSeinPlayerID Observer) const
{
	// Canonical priority: Heavy > Light > Negative > <any other tag>.
	// We pick the strongest PROTECTION tag first (Heavy → Light) so a unit
	// standing in heavy cover layered on top of a negative-cover patch (e.g.
	// sandbags on a road) still gets the heavy chevron — matches CoH-style
	// "best protection" UX. Falls back to negative only when no positive
	// cover is present, so the negative is the lone signal at that point.
	const TArray<FSeinCoverContext> Contexts = QueryCoverAt(WorldPoint, Observer);
	if (Contexts.Num() == 0) return FGameplayTag();

	FGameplayTag BestNonCanonical;     // first non-Heavy/Light/Negative tag we see, fallback when nothing canonical matches
	bool bSawLight = false;
	bool bSawNegative = false;

	for (const FSeinCoverContext& Ctx : Contexts)
	{
		if (!Ctx.QualityTag.IsValid()) continue;
		if (Ctx.QualityTag == SeinCoverTags::Cover_Heavy)
		{
			// Heavy always wins — return immediately to skip rest of the walk.
			return SeinCoverTags::Cover_Heavy;
		}
		if (Ctx.QualityTag == SeinCoverTags::Cover_Light)    { bSawLight = true; continue; }
		if (Ctx.QualityTag == SeinCoverTags::Cover_Negative) { bSawNegative = true; continue; }
		// Designer-defined tag — remember the first one as a generic fallback.
		if (!BestNonCanonical.IsValid()) BestNonCanonical = Ctx.QualityTag;
	}

	if (bSawLight)         return SeinCoverTags::Cover_Light;
	if (BestNonCanonical.IsValid()) return BestNonCanonical;
	if (bSawNegative)      return SeinCoverTags::Cover_Negative;
	return FGameplayTag();
}

TArray<FSeinCoverSlotCandidate> USeinCoverDefault::FindNearbySlots(FFixedVector Origin,
	FFixedPoint Radius, FSeinPlayerID Observer) const
{
	TArray<FSeinCoverSlotCandidate> Result;

	USeinWorldSubsystem* WorldSub = World.Get();
	if (!WorldSub)
	{
		UE_LOG(LogSeinCoverDefault, Verbose,
			TEXT("[FindNearbySlots] no WorldSubsystem (cover system never initialized?)"));
		return Result;
	}
	if (RegisteredProviders.Num() == 0)
	{
		UE_LOG(LogSeinCoverDefault, Verbose,
			TEXT("[FindNearbySlots] RegisteredProviders is EMPTY — cover providers never registered with the cover system. "
			     "Check that the provider actor was spawned through the sim pipeline (USeinWorldSubsystem::SpawnEntity) "
			     "and that an FSeinCoverComponent entry is authored in the bridge's ComponentData array."));
		return Result;
	}
	if (Radius <= FFixedPoint::Zero) return Result;

	UE_LOG(LogSeinCoverDefault, Verbose,
		TEXT("[FindNearbySlots] Origin=(%.1f, %.1f, %.1f) Radius=%.1f, %d providers registered"),
		Origin.X.ToFloat(), Origin.Y.ToFloat(), Origin.Z.ToFloat(),
		Radius.ToFloat(), RegisteredProviders.Num());

	const FFixedPoint RadiusSq = Radius * Radius;

	// ======================================================================
	// Pass 1 — gather the near-cursor, observer-visible providers the resolution
	// runs over: each one's transform + solid body (Extents, for the overlap
	// reject) + cover data (Area / quality / Slots / SlotRadius). The query radius
	// + each provider's cached reach prefilter the set to the few around the cursor.
	// ======================================================================
	struct FGatheredProvider
	{
		FSeinEntityHandle            Handle;
		int32                        Index;
		FFixedVector                 Location;
		FFixedQuaternion             Rotation;
		const FSeinExtentsComponent* Extents;
		const FSeinCoverComponent*   Cover;
	};
	TArray<FGatheredProvider> Providers;
	Providers.Reserve(RegisteredProviders.Num());
	for (int32 ProviderIdx = 0; ProviderIdx < RegisteredProviders.Num(); ++ProviderIdx)
	{
		const FSeinEntityHandle& ProviderHandle = RegisteredProviders[ProviderIdx];
		const FSeinEntity* Entity = WorldSub->GetEntity(ProviderHandle);
		if (!Entity) continue;

		const FFixedVector ProviderLocation = Entity->Transform.GetLocation();
		const FFixedPoint Reach = RegisteredProviderReaches.IsValidIndex(ProviderIdx)
			? RegisteredProviderReaches[ProviderIdx] : FFixedPoint::Zero;
		const FFixedPoint TotalReach = Radius + Reach;
		if (FFixedVector::DistSquared(ProviderLocation, Origin) > TotalReach * TotalReach) continue;

		// Per-observer fog filter — snap callers pass the ordering player so cover
		// they haven't scouted doesn't influence the result; combat / AI pass an
		// invalid Observer to disable filtering.
		if (!SeinCoverDefaultLocal::IsProviderVisibleToObserver(WorldSub, ProviderHandle, Observer)) continue;

		const FSeinCoverComponent* Cover = WorldSub->GetComponent<FSeinCoverComponent>(ProviderHandle);
		if (!Cover) continue;

		FGatheredProvider GP;
		GP.Handle   = ProviderHandle;
		GP.Index    = ProviderIdx;
		GP.Location = ProviderLocation;
		GP.Rotation = Entity->Transform.GetQuaternionRotation();
		GP.Extents  = WorldSub->GetComponent<FSeinExtentsComponent>(ProviderHandle);
		GP.Cover    = Cover;
		Providers.Add(GP);
	}
	if (Providers.Num() == 0) return Result;

	// ======================================================================
	// Pass 2 — generate every provider's slots and resolve each:
	//   (a) EXTENTS REJECT: drop a slot whose footprint circle overlaps ANY
	//       gathered provider's solid body (cross-walls / mutual-reject case).
	//       Runs before dedup, so two slots that both overlap the opposing wall
	//       simply vanish — no tie-break needed.
	//   (b) QUALITY-BY-AREA: a slot's quality is the BEST cover Area its circle
	//       overlaps across all providers (a light-wall slot inside a heavy area
	//       reads heavy), falling back to its own provider's tag.
	// All overlap math is the core SeinGeometry circle/box/sphere intersection set;
	// the cover layer only poses the shapes.
	// ======================================================================
	struct FResolvedSlot
	{
		FFixedVector      WorldPos;
		FFixedPoint       R;
		FGameplayTag      Quality;
		FFixedVector      ProtectedDir;
		FSeinEntityHandle Provider;
		int32             ProviderIndex;
		int32             SlotIndex;
		int32             Priority;
	};
	TArray<FResolvedSlot> Slots;
	for (const FGatheredProvider& P : Providers)
	{
		const FFixedPoint SlotR = FFixedPoint::FromFloat(P.Cover->SlotRadius);
		for (int32 SlotIdx = 0; SlotIdx < P.Cover->Slots.Num(); ++SlotIdx)
		{
			const FFixedVector WorldPos = P.Location + P.Rotation.RotateVector(P.Cover->Slots[SlotIdx]);

			// (a) Extents reject — slot circle overlaps any provider's solid body.
			bool bRejected = false;
			for (const FGatheredProvider& Q : Providers)
			{
				if (Q.Extents && SeinCoverGeometry::CircleOverlapsExtents(
						Q.Extents, Q.Location, Q.Rotation, WorldPos, SlotR))
				{
					bRejected = true;
					break;
				}
			}
			if (bRejected) continue;

			// (b) Quality = best cover area the slot's circle overlaps.
			FGameplayTag Quality = P.Cover->QualityTag;
			int32 BestPriority = SeinCoverDefaultLocal::CoverQualityPriority(Quality);
			for (const FGatheredProvider& Q : Providers)
			{
				if (Q.Cover->Area.Shape == ESeinCoverAreaShape::None) continue;
				const int32 QPriority = SeinCoverDefaultLocal::CoverQualityPriority(Q.Cover->QualityTag);
				if (QPriority <= BestPriority) continue;   // can't beat current best
				if (SeinCoverGeometry::CircleOverlapsCoverArea(
						Q.Cover->Area, Q.Location, Q.Rotation, WorldPos, SlotR))
				{
					Quality = Q.Cover->QualityTag;
					BestPriority = QPriority;
				}
			}

			FResolvedSlot RS;
			RS.WorldPos      = WorldPos;
			RS.R             = SlotR;
			RS.Quality       = Quality;
			RS.ProtectedDir  = (P.Cover->bIsDirectional && P.Extents)
				? SeinCoverGeometry::OutwardFromExtentsCached(P.Extents, P.Location, P.Rotation, WorldPos)
				: FFixedVector::ZeroVector;
			RS.Provider      = P.Handle;
			RS.ProviderIndex = P.Index;
			RS.SlotIndex     = SlotIdx;
			RS.Priority      = BestPriority;
			Slots.Add(RS);
		}
	}

	// ======================================================================
	// Pass 3 — dedup overlapping slots, best quality wins. Sort best-first
	// (quality priority desc, then provider + slot index asc for a lockstep-
	// deterministic tie-break), then greedily accept any slot whose circle
	// doesn't overlap an already-accepted one. Best-first greedy guarantees the
	// winner of each overlapping cluster is its highest-quality member.
	// ======================================================================
	TArray<int32> Order;
	Order.Reserve(Slots.Num());
	for (int32 i = 0; i < Slots.Num(); ++i) Order.Add(i);
	Order.Sort([&Slots](int32 A, int32 B)
	{
		if (Slots[A].Priority != Slots[B].Priority)           return Slots[A].Priority > Slots[B].Priority;
		if (Slots[A].ProviderIndex != Slots[B].ProviderIndex) return Slots[A].ProviderIndex < Slots[B].ProviderIndex;
		return Slots[A].SlotIndex < Slots[B].SlotIndex;
	});

	TArray<int32> Accepted;
	Accepted.Reserve(Slots.Num());
	for (int32 OrderIdx : Order)
	{
		const FResolvedSlot& S = Slots[OrderIdx];
		bool bOverlaps = false;
		for (int32 AcceptedIdx : Accepted)
		{
			const FResolvedSlot& A = Slots[AcceptedIdx];
			if (SeinGeometry::SphereIntersectsSphere(
					FFixedSphere(S.WorldPos, S.R), FFixedSphere(A.WorldPos, A.R)))
			{
				bOverlaps = true;
				break;
			}
		}
		if (!bOverlaps) Accepted.Add(OrderIdx);
	}

	// ======================================================================
	// Pass 4 — emit accepted slots within the cursor radius as candidates. Keep
	// the lenient nav-point backstop (slot CENTRE on a blocked cell) for blockers
	// that aren't cover providers — center-only, so it never over-rejects a slot
	// the unit can actually stand on (clearance to the slot is the path layer's job).
	// ======================================================================
	TArray<FFixedPoint> DistSqByCandidate;
	DistSqByCandidate.Reserve(Accepted.Num());
	for (int32 AcceptedIdx : Accepted)
	{
		const FResolvedSlot& S = Slots[AcceptedIdx];
		const FFixedPoint DistSq = FFixedVector::DistSquared(Origin, S.WorldPos);
		if (DistSq > RadiusSq) continue;
		if (WorldSub->DynamicPassableResolver.IsBound() &&
			!WorldSub->DynamicPassableResolver.Execute(S.WorldPos)) continue;

		FSeinCoverSlotCandidate Candidate;
		Candidate.WorldPosition               = S.WorldPos;
		Candidate.WorldProtectedFromDirection = S.ProtectedDir;
		Candidate.QualityTag                  = S.Quality;
		Candidate.ProviderHandle              = S.Provider;
		Candidate.SlotIndex                   = S.SlotIndex;
		Candidate.Radius                      = S.R;
		Result.Add(MoveTemp(Candidate));
		DistSqByCandidate.Add(DistSq);
	}

	// Sort by ascending DistSq using the cached values rather than recomputing
	// two DistSquared calls per compare. We sort indices then physically
	// reorder the candidates — for N candidates that's N moves + O(N log N)
	// scalar compares.
	const int32 NumCandidates = Result.Num();
	if (NumCandidates > 1)
	{
		TArray<int32> Indices;
		Indices.Reserve(NumCandidates);
		for (int32 i = 0; i < NumCandidates; ++i) Indices.Add(i);
		Indices.Sort([&DistSqByCandidate](int32 A, int32 B)
		{
			return DistSqByCandidate[A] < DistSqByCandidate[B];
		});

		TArray<FSeinCoverSlotCandidate> Sorted;
		Sorted.Reserve(NumCandidates);
		for (int32 i : Indices) Sorted.Add(MoveTemp(Result[i]));
		Result = MoveTemp(Sorted);
	}

	return Result;
}

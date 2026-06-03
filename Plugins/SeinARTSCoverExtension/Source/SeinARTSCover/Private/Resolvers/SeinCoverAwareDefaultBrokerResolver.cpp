/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverAwareDefaultBrokerResolver.cpp
 */

#include "Resolvers/SeinCoverAwareDefaultBrokerResolver.h"

#include "Lib/SeinCoverGeometry.h"
#include "System/SeinCoverSubsystem.h"
#include "System/SeinCoverSystem.h"
#include "Tags/SeinCoverGameplayTags.h"
#include "Types/SeinCoverTypes.h"

#include "Settings/SeinARTSCoverSettings.h"
#include "Simulation/SeinWorldSubsystem.h"

#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinCoverResolver, Log, All);

namespace SeinCoverSnapLocal
{
	/** Resolve the cover subsystem from a sim world subsystem ref. */
	static USeinCoverSystem* GetCoverSystem(USeinWorldSubsystem* WorldSub)
	{
		if (!WorldSub) return nullptr;
		UWorld* World = WorldSub->GetWorld();
		if (!World) return nullptr;
		USeinCoverSubsystem* CoverSub = World->GetSubsystem<USeinCoverSubsystem>();
		return CoverSub ? CoverSub->GetCoverSystem() : nullptr;
	}

	/** Tag-based eligibility check — uses cover iff the entity carries the
	 *  `SeinARTS.Cover.UsesCover` tag (typically authored on the entity
	 *  bridge's BaseTags and propagated through the spawn pipeline). O(1)
	 *  via the world subsystem's tag-state map. */
	static bool EntityUsesCover(USeinWorldSubsystem* WorldSub, FSeinEntityHandle Handle)
	{
		return WorldSub && WorldSub->HasTag(Handle, SeinCoverTags::Cover_UsesCover);
	}

	/** One-pass greedy nearest allocator over an INDEX-FILTERED slot list.
	 *  Walks `MemberOrder` (member indices remaining to allocate) and for
	 *  each member, finds the cheapest unallocated slot in `SlotIndices`
	 *  within `SnapRadiusSq`. Allocated slots are inserted into
	 *  `InOutAllocatedSlotIndices` so a subsequent pass can skip them.
	 *  Members that successfully snap are removed from `MemberOrder` (so
	 *  the next pass only sees still-unallocated members).
	 *
	 *  Cost is plain squared distance — no side penalty. The side decision
	 *  is made up front by `PartitionSlotsByCursorSide`; within a pass,
	 *  greedy-nearest is the right policy. */
	static int32 GreedyAllocatePass(
		const TArray<FSeinCoverSlotCandidate>& Slots,
		const TArray<int32>& SlotIndices,
		TArray<FFixedVector>& InOutPositions,
		TArray<int32>& MemberOrder,
		TSet<int32>& InOutAllocatedSlotIndices,
		FFixedPoint SnapRadiusSq)
	{
		int32 NumSnapped = 0;
		for (int32 MemberCursor = 0; MemberCursor < MemberOrder.Num(); )
		{
			const int32 MemberIdx = MemberOrder[MemberCursor];

			int32 BestSlotIdx = INDEX_NONE;
			FFixedPoint BestDistSq = FFixedPoint::MaxValue;
			for (int32 SlotIdx : SlotIndices)
			{
				if (InOutAllocatedSlotIndices.Contains(SlotIdx)) continue;
				const FFixedPoint DistSq = FFixedVector::DistSquared(InOutPositions[MemberIdx], Slots[SlotIdx].WorldPosition);
				if (DistSq > SnapRadiusSq) continue;
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					BestSlotIdx = SlotIdx;
				}
			}

			if (BestSlotIdx != INDEX_NONE)
			{
				InOutPositions[MemberIdx] = Slots[BestSlotIdx].WorldPosition;
				InOutAllocatedSlotIndices.Add(BestSlotIdx);
				MemberOrder.RemoveAt(MemberCursor);
				++NumSnapped;
			}
			else
			{
				++MemberCursor;
			}
		}
		return NumSnapped;
	}

	/** Two-pass cover-snap allocator implementing the cursor-side global
	 *  filter described in DESIGN option D:
	 *    Pass 1 — greedy nearest over PREFERRED-side slots (slots on the
	 *             same side of the cover body as the cursor, or slots
	 *             from non-directional providers).
	 *    Pass 2 — for any members still unallocated, greedy nearest over
	 *             WRONG-side slots. Wrap-around fallback so a squad larger
	 *             than the preferred-side capacity still snaps cleanly.
	 *
	 *  Walks Members in index order (matches the broker resolver's per-
	 *  member iteration). Lower slot indices get first pick — typically
	 *  the leader / front row, which is the natural priority. */
	static void SnapMembersToSlots(
		USeinWorldSubsystem* WorldSub,
		const TArray<FSeinEntityHandle>& Members,
		TArray<FFixedVector>& InOutPositions,
		TArray<FSeinCoverSlotCandidate>& Slots,
		FFixedVector TargetLocation,
		FFixedPoint SnapRadius)
	{
		const FFixedPoint SnapRadiusSq = SnapRadius * SnapRadius;

		// Partition slot candidates by cursor side. One outward-from-extents
		// call per provider (cached internally); slots with zero WPFD
		// (non-directional providers) go into preferred.
		TArray<int32> PreferredIndices;
		TArray<int32> WrongSideIndices;
		SeinCoverGeometry::PartitionSlotsByCursorSide(WorldSub, Slots, TargetLocation,
			PreferredIndices, WrongSideIndices);

		// Build the working member list: only cover-eligible members with a
		// valid InOutPositions index. Order preserves the input order so the
		// leader / front row gets first pick.
		TArray<int32> RemainingMembers;
		RemainingMembers.Reserve(Members.Num());
		for (int32 i = 0; i < Members.Num(); ++i)
		{
			if (i >= InOutPositions.Num()) break;
			if (!EntityUsesCover(WorldSub, Members[i])) continue;
			RemainingMembers.Add(i);
		}
		const int32 TotalEligible = RemainingMembers.Num();

		TSet<int32> AllocatedSlots;
		AllocatedSlots.Reserve(Slots.Num());

		const int32 SnappedPreferred = GreedyAllocatePass(
			Slots, PreferredIndices, InOutPositions, RemainingMembers, AllocatedSlots, SnapRadiusSq);

		const int32 SnappedWrongSide = GreedyAllocatePass(
			Slots, WrongSideIndices, InOutPositions, RemainingMembers, AllocatedSlots, SnapRadiusSq);

		UE_LOG(LogSeinCoverResolver, Verbose,
			TEXT("Cover-snap: %d/%d members snapped (preferred=%d, wrong-side fallback=%d; %d preferred slots, %d wrong-side slots in candidate set)"),
			SnappedPreferred + SnappedWrongSide, TotalEligible,
			SnappedPreferred, SnappedWrongSide,
			PreferredIndices.Num(), WrongSideIndices.Num());
	}
}

void USeinCoverAwareDefaultBrokerResolver::PostProcessPositions(
	USeinWorldSubsystem* WorldSub,
	const TArray<FSeinEntityHandle>& Members,
	TArray<FFixedVector>& InOutPositions,
	FFixedVector TargetLocation) const
{
	// Tuning sourced from plugin settings — single config surface for both
	// cover-aware resolvers. Live edits take effect on the next move
	// command (no editor restart). FromFloat is safe at call time because
	// this resolver path is render-side only (the sim never sees this
	// radius — it only gates which slot candidates the resolver considers,
	// and the snap output is already deterministic via sim-side slot /
	// member positions).
	const USeinARTSCoverSettings* Settings = GetDefault<USeinARTSCoverSettings>();
	const FFixedPoint CoverSnapRadius =
		FFixedPoint::FromFloat(Settings ? Settings->CoverSnapRadius : 500.f);

	UE_LOG(LogSeinCoverResolver, Verbose,
		TEXT("[DefaultCoverAware::PostProcessPositions] called; Members=%d, Positions=%d, Target=(%.1f, %.1f, %.1f), Radius=%.1f"),
		Members.Num(), InOutPositions.Num(),
		TargetLocation.X.ToFloat(), TargetLocation.Y.ToFloat(), TargetLocation.Z.ToFloat(),
		CoverSnapRadius.ToFloat());

	if (Members.Num() == 0 || InOutPositions.Num() == 0) return;

	USeinCoverSystem* Cover = SeinCoverSnapLocal::GetCoverSystem(WorldSub);
	if (!Cover)
	{
		UE_LOG(LogSeinCoverResolver, Warning,
			TEXT("[DefaultCoverAware::PostProcessPositions] no cover system available"));
		return;
	}

	// Per-observer fog-visibility filter — only consider cover the ordering
	// player can see. We derive the observer from the first member's owner;
	// all squad members share the issuing player so any member's owner is
	// correct. Deterministic across clients because every peer evaluates the
	// same player's fog state for the same command.
	const FSeinPlayerID Observer = (Members.Num() > 0)
		? WorldSub->GetEntityOwner(Members[0])
		: FSeinPlayerID();

	TArray<FSeinCoverSlotCandidate> NearbySlots = Cover->FindNearbySlots(TargetLocation, CoverSnapRadius, Observer);
	UE_LOG(LogSeinCoverResolver, Verbose,
		TEXT("[DefaultCoverAware::PostProcessPositions] %d nearby slots within radius"), NearbySlots.Num());

	if (NearbySlots.Num() == 0) return;

	// NOTE: cover slots are deliberately NOT nav-projected. They are authoritative
	// destinations that OVERRULE the coarse nav bake — a "red"/blocked cell under a
	// slot is a low-resolution false-negative, not a reason to relocate the slot.
	// (An earlier version projected here; it fed slots into the 30-cell ring-scan
	// and sent destinations dozens of cells away. See root CLAUDE.md invariant #6.)
	// Reachability is handled by the authoritative-destination path: the unit is
	// delivered to the exact slot, and the preview shows the exact slot.

	// Tag check before allocation so we can see who's eligible.
	for (int32 i = 0; i < Members.Num(); ++i)
	{
		const bool bUses = SeinCoverSnapLocal::EntityUsesCover(WorldSub, Members[i]);
		UE_LOG(LogSeinCoverResolver, Verbose,
			TEXT("  Member[%d] %s: UsesCover=%s"), i, *Members[i].ToString(), bUses ? TEXT("true") : TEXT("false"));
	}

	SeinCoverSnapLocal::SnapMembersToSlots(WorldSub, Members, InOutPositions, NearbySlots, TargetLocation, CoverSnapRadius);
}

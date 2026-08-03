/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverAwareSquadDispatchResolver.cpp
 *
 *          Cover-snap implementation parallel to the default-resolver variant
 *          — both apply the same cursor-side global filter + two-pass greedy
 *          allocation. We duplicate the body here rather than share a static
 *          helper across plugin modules to keep each resolver class
 *          self-contained and avoid cross-module private-header coupling; the
 *          logic is small + identical enough that drift risk is minimal.
 */

#include "SeinCoverAwareSquadDispatchResolver.h"

#include "Lib/SeinCoverGeometry.h"
#include "System/SeinCoverSubsystem.h"
#include "System/SeinCoverSystem.h"
#include "Tags/SeinCoverGameplayTags.h"
#include "Types/SeinCoverTypes.h"

#include "Settings/SeinARTSCoverSettings.h"
#include "Simulation/SeinWorldSubsystem.h"

#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinCoverSquadResolver, Log, All);

namespace SeinCoverSquadSnapLocal
{
	static USeinCoverSystem* GetCoverSystem(USeinWorldSubsystem* WorldSub)
	{
		if (!WorldSub) return nullptr;
		UWorld* World = WorldSub->GetWorld();
		if (!World) return nullptr;
		USeinCoverSubsystem* CoverSub = World->GetSubsystem<USeinCoverSubsystem>();
		return CoverSub ? CoverSub->GetCoverSystem() : nullptr;
	}

	static bool EntityUsesCover(USeinWorldSubsystem* WorldSub, FSeinEntityHandle Handle)
	{
		return WorldSub && WorldSub->HasTag(Handle, SeinCoverTags::Cover_UsesCover);
	}

	/** Greedy nearest pass over an index-filtered slot list. Returns the
	 *  number of members snapped this pass; members that successfully
	 *  snapped are removed from `MemberOrder` so subsequent passes only see
	 *  remaining-unallocated members. Mirror of the helper in
	 *  SeinCoverAwareDefaultBrokerResolver.cpp. */
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
}

void USeinCoverAwareSquadDispatchResolver::PostProcessPositions_Implementation(
	USeinWorldSubsystem* WorldSub,
	const TArray<FSeinEntityHandle>& Members,
	TArray<FFixedVector>& InOutPositions,
	FFixedVector TargetLocation)
{
	// Tuning comes from the single Cover settings surface shared with the
	// default broker resolver. Cursor-side preference is deterministic policy;
	// CoverSnapRadius is the only distance gate.
	const USeinARTSCoverSettings* Settings = GetDefault<USeinARTSCoverSettings>();
	const FFixedPoint CoverSnapRadius =
		(Settings ? Settings->CoverSnapRadius : FFixedPoint::FromInt(500));

	UE_LOG(LogSeinCoverSquadResolver, Verbose,
		TEXT("[SquadCoverAware::PostProcessPositions] called; Members=%d, Positions=%d, Target=(%.1f, %.1f, %.1f), Radius=%.1f"),
		Members.Num(), InOutPositions.Num(),
		TargetLocation.X.ToFloat(), TargetLocation.Y.ToFloat(), TargetLocation.Z.ToFloat(),
		CoverSnapRadius.ToFloat());

	if (Members.Num() == 0 || InOutPositions.Num() == 0) return;

	USeinCoverSystem* Cover = SeinCoverSquadSnapLocal::GetCoverSystem(WorldSub);
	if (!Cover)
	{
		UE_LOG(LogSeinCoverSquadResolver, Warning,
			TEXT("[SquadCoverAware::PostProcessPositions] no cover system available"));
		return;
	}

	// Per-observer fog filter — only consider cover the ordering player can
	// see. First member's owner = ordering player (squad members share
	// owner). Deterministic across clients (same fog state evaluated for
	// the same command). See SeinCoverAwareDefaultBrokerResolver.cpp for
	// the full rationale.
	const FSeinPlayerID Observer = (Members.Num() > 0)
		? WorldSub->GetEntityOwner(Members[0])
		: FSeinPlayerID();

	TArray<FSeinCoverSlotCandidate> NearbySlots = Cover->FindNearbySlots(TargetLocation, CoverSnapRadius, Observer);
	UE_LOG(LogSeinCoverSquadResolver, Verbose,
		TEXT("[SquadCoverAware::PostProcessPositions] %d nearby slots within radius"), NearbySlots.Num());

	if (NearbySlots.Num() == 0) return;

	// NOTE: cover slots are deliberately NOT nav-projected. They are authoritative
	// destinations that OVERRULE the coarse nav bake — a "red"/blocked cell under a
	// slot is a low-resolution false-negative, not a reason to relocate the slot.
	// (An earlier version projected here; it fed slots into the 30-cell ring-scan
	// and sent destinations dozens of cells away. See root CLAUDE.md invariant #6.)
	// Reachability is handled by the authoritative-destination path: the unit is
	// delivered to the exact slot, and the preview shows the exact slot.

	// Eligibility audit before allocation.
	for (int32 i = 0; i < Members.Num(); ++i)
	{
		const bool bUses = SeinCoverSquadSnapLocal::EntityUsesCover(WorldSub, Members[i]);
		UE_LOG(LogSeinCoverSquadResolver, Verbose,
			TEXT("  Member[%d] %s: UsesCover=%s"), i, *Members[i].ToString(), bUses ? TEXT("true") : TEXT("false"));
	}

	const FFixedPoint SnapRadiusSq = CoverSnapRadius * CoverSnapRadius;

	// Partition slot candidates by cursor side — one outward call per
	// provider (cached). Slots from non-directional providers (foxholes,
	// craters) go into preferred unconditionally; their WPFD is zero so
	// no side decision is needed.
	TArray<int32> PreferredIndices;
	TArray<int32> WrongSideIndices;
	SeinCoverGeometry::PartitionSlotsByCursorSide(WorldSub, NearbySlots, TargetLocation,
		PreferredIndices, WrongSideIndices);

	// Build the working member list: only cover-eligible members with a
	// valid InOutPositions index. Order preserves input order (squad's
	// effective dispatch order — low slot index gets first pick, natural
	// priority for squads with authored slot layouts).
	TArray<int32> RemainingMembers;
	RemainingMembers.Reserve(Members.Num());
	for (int32 i = 0; i < Members.Num(); ++i)
	{
		if (i >= InOutPositions.Num()) break;
		if (!SeinCoverSquadSnapLocal::EntityUsesCover(WorldSub, Members[i])) continue;
		RemainingMembers.Add(i);
	}
	const int32 TotalEligible = RemainingMembers.Num();

	TSet<int32> AllocatedSlots;
	AllocatedSlots.Reserve(NearbySlots.Num());

	// Pass 1: greedy nearest over preferred-side slots only.
	const int32 SnappedPreferred = SeinCoverSquadSnapLocal::GreedyAllocatePass(
		NearbySlots, PreferredIndices, InOutPositions, RemainingMembers, AllocatedSlots, SnapRadiusSq);

	// Pass 2: wrap-around fallback over wrong-side slots for any squad
	// members still unallocated. Only kicks in when preferred-side capacity
	// is exhausted OR when a member's per-member radius gate has no
	// preferred-side slot within range.
	const int32 SnappedWrongSide = SeinCoverSquadSnapLocal::GreedyAllocatePass(
		NearbySlots, WrongSideIndices, InOutPositions, RemainingMembers, AllocatedSlots, SnapRadiusSq);

	const int32 TotalSnapped = SnappedPreferred + SnappedWrongSide;

	// Throttled state-change summary — demoted to Verbose after cover-snap was
	// confirmed working. Steady-state default verbosity has zero output from
	// this hot path. Re-enable with `Log LogSeinCoverSquadResolver Verbose`
	// when diagnosing cover-snap regressions. Tracks `(MembersInputCount,
	// EligibleCount, SnappedCount, NearbyCount)` and only emits on transition,
	// so even at Verbose it's quiet during normal cursor hover and only fires
	// when something interesting changes (cursor entered cover, snap happened,
	// eligibility changed, member dies). The explanatory message at the end
	// is the single most useful diagnostic when snap silently no-ops — it
	// names the gate that failed.
	static int32 LastMembers = -1;
	static int32 LastEligible = -1;
	static int32 LastSnapped = -1;
	static int32 LastNearby = -1;
	const bool bChanged = (Members.Num() != LastMembers)
		|| (TotalEligible != LastEligible)
		|| (TotalSnapped != LastSnapped)
		|| (NearbySlots.Num() != LastNearby);
	if (bChanged)
	{
		UE_LOG(LogSeinCoverSquadResolver, Verbose,
			TEXT("Squad cover-snap: Members=%d, EligibleByTag=%d, NearbyCoverSlots=%d, Snapped=%d (preferred=%d, wrong-side=%d). %s"),
			Members.Num(), TotalEligible, NearbySlots.Num(),
			TotalSnapped, SnappedPreferred, SnappedWrongSide,
			(TotalEligible == 0 && Members.Num() > 0)
				? TEXT("NO MEMBERS ELIGIBLE — check SeinARTS.Cover.UsesCover tag on member BPs.")
				: (NearbySlots.Num() == 0)
					? TEXT("NO COVER SLOTS NEAR CURSOR — check CoverSnapRadius vs cursor-to-cover distance.")
					: TEXT(""));
		LastMembers = Members.Num();
		LastEligible = TotalEligible;
		LastSnapped = TotalSnapped;
		LastNearby = NearbySlots.Num();
	}

	UE_LOG(LogSeinCoverSquadResolver, VeryVerbose,
		TEXT("Squad cover-snap: %d/%d members snapped (preferred=%d, wrong-side fallback=%d; %d preferred slots, %d wrong-side slots in candidate set)"),
		TotalSnapped, TotalEligible,
		SnappedPreferred, SnappedWrongSide,
		PreferredIndices.Num(), WrongSideIndices.Num());
}

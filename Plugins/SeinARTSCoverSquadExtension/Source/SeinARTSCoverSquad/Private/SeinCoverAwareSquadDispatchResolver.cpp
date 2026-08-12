/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverAwareSquadDispatchResolver.cpp
 *
 *          Squad adapter over Cover's shared deterministic assignment planner.
 */

#include "SeinCoverAwareSquadDispatchResolver.h"

#include "Lib/SeinCoverAssignmentPlanner.h"
#include "System/SeinCoverSubsystem.h"
#include "System/SeinCoverSystem.h"

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

	const FSeinCoverAssignmentPlan Plan =
		FSeinCoverAssignmentPlanner::PlanForMembers(
			WorldSub,
			Members,
			InOutPositions,
			NearbySlots,
			TargetLocation,
			CoverSnapRadius);
	Plan.Apply(InOutPositions, NearbySlots);

	UE_LOG(LogSeinCoverSquadResolver, Verbose,
		TEXT("Squad cover-snap: %d/%d members snapped (preferred=%d, wrong-side=%d; candidates=%d)."),
		Plan.Num(),
		Plan.EligibleMemberCount,
		Plan.PreferredAssignmentCount,
		Plan.WrongSideAssignmentCount(),
		NearbySlots.Num());
}

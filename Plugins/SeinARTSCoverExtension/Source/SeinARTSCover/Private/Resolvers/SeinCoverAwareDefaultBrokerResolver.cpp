/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverAwareDefaultBrokerResolver.cpp
 */

#include "Resolvers/SeinCoverAwareDefaultBrokerResolver.h"

#include "Formations/SeinFormation.h"
#include "Lib/SeinCoverAssignmentPlanner.h"
#include "System/SeinCoverSubsystem.h"
#include "System/SeinCoverSystem.h"

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

}

void USeinCoverAwareDefaultBrokerResolver::PostProcessPositions_Implementation(
	USeinWorldSubsystem* WorldSub,
	const TArray<FSeinEntityHandle>& Members,
	TArray<FFixedVector>& InOutPositions,
	FFixedVector TargetLocation)
{
	if (WorldSub && WorldSub->IsSelectionDestinationBaseLayoutInProgress())
	{
		return;
	}

	// Tuning sourced from plugin settings: one config surface for both
	// cover-aware resolvers. Live edits take effect on the next move command
	// (no editor restart). CoverSnapRadius is FFixedPoint, used DIRECTLY:
	// PostProcessPositions runs in sim command-processing and this radius gates
	// which slot candidates / members snap, so it MUST be deterministic (a
	// float->fixed conversion here would be a cross-client desync risk).
	const USeinARTSCoverSettings* Settings = GetDefault<USeinARTSCoverSettings>();
	const FFixedPoint CoverSnapRadius =
		(Settings ? Settings->CoverSnapRadius : FFixedPoint::FromInt(500));

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

	// Reservation policy: authored slots already claimed by a live frozen
	// destination (queued or settled, any broker) are not offered to this
	// artifact-less re-solve, mirroring the selection-plan provider's filter.
	// The order's own members are ignored so a unit may re-order off its own
	// settled claim. Artifact-carrying orders never reach this path — their
	// contention was validated at command admission and their exact positions
	// apply without re-solving, so an admitted artifact can never be re-snapped
	// here.
	FFixedPoint MaxMemberRadius = FFixedPoint::Zero;
	for (const FSeinEntityHandle& Member : Members)
	{
		const FFixedPoint MemberRadius =
			USeinFormation::GetFootprintRadius(WorldSub, Member);
		if (MemberRadius > MaxMemberRadius)
		{
			MaxMemberRadius = MemberRadius;
		}
	}
	if (MaxMemberRadius > FFixedPoint::Zero)
	{
		NearbySlots.RemoveAll(
			[WorldSub, MaxMemberRadius, &Members](
				const FSeinCoverSlotCandidate& Slot)
			{
				return WorldSub->IsDestinationFootprintReserved(
					Slot.WorldPosition,
					MaxMemberRadius,
					Members);
			});
		if (NearbySlots.Num() == 0) return;
	}

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

	UE_LOG(LogSeinCoverResolver, Verbose,
		TEXT("Cover-snap: %d/%d members snapped (preferred=%d, wrong-side=%d)."),
		Plan.Num(),
		Plan.EligibleMemberCount,
		Plan.PreferredAssignmentCount,
		Plan.WrongSideAssignmentCount());
}

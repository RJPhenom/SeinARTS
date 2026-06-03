/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDefaultCommandBrokerResolver.cpp
 * @brief   Framework-default CommandBroker resolver (DESIGN §5).
 *
 *          Dispatch rule, per effective member:
 *            1. Delegate to ResolveMemberAbility (virtual — reads the member's
 *               own DefaultCommands / FallbackAbilityTag by default, or
 *               whatever a subclass overrides it to do).
 *            2. If the returned ability tag is valid and the member owns that
 *               ability, dispatch it against the order's target entity/location.
 *            3. Otherwise (invalid tag OR member doesn't have the ability): if
 *               TagAlongAbility is set and the member owns it, dispatch it
 *               toward the formation slot. Non-combatants tagging along with
 *               an attack order, etc.
 *            4. Else silently skip — member stays idle for this order.
 *
 *          Positions: uniform-spaced square-ish grid centered on the anchor,
 *          facing-rotated. Good-enough MVP; designer resolvers replace for
 *          tight ranks, wedges, class-clustered formations.
 */

#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinCommandBrokerData.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Math/MathLib.h"

namespace SeinDefaultBrokerLocal
{
	/** Build a yaw-only quat that rotates world-X (FForwardVector) onto the
	 *  given direction in the XY plane. Z component of `DirXY` is ignored.
	 *  Returns Identity for a near-zero direction (degenerate). Shared between
	 *  the default resolver's facing computation and the squad resolver's
	 *  formation-facing path (squad resolver uses the same primitive via
	 *  USeinDefaultCommandBrokerResolver::ComputeFormationFacing). */
	static FFixedQuaternion YawFacingFromXY(const FFixedVector& DirXY)
	{
		FFixedVector Flat(DirXY.X, DirXY.Y, FFixedPoint::Zero);
		if (Flat.IsNearlyZero()) return FFixedQuaternion::Identity;
		const FFixedPoint Yaw = SeinMath::Atan2(Flat.Y, Flat.X);
		return FFixedQuaternion::FromAxisAndAngle(FFixedVector::UpVector, Yaw);
	}
}

FSeinFormationFacing USeinDefaultCommandBrokerResolver::ComputeFormationFacing(
	FFixedVector CurrentCentroid,
	FFixedQuaternion CurrentFacing,
	FFixedVector TargetLocation,
	bool bInvertWhenBackward)
{
	FSeinFormationFacing Result;
	Result.bIsBackwardWalk = false;

	FFixedVector ToTarget = TargetLocation - CurrentCentroid;
	ToTarget.Z = FFixedPoint::Zero;       // 2D — RTS top-down, ignore vertical

	if (ToTarget.IsNearlyZero())
	{
		// Move-to-where-we-are: keep current facing rather than degenerate-quat'ing.
		// Matches the prior in-line behavior in the squad/default dispatch paths.
		Result.Facing = CurrentFacing;
		return Result;
	}

	const FFixedVector ToTargetN = FFixedVector::GetSafeNormal(ToTarget);
	const FFixedQuaternion TargetFacing = SeinDefaultBrokerLocal::YawFacingFromXY(ToTargetN);

	if (bInvertWhenBackward)
	{
		// Threshold = 0 → "any backward at all triggers reverse-walk." Tighter
		// thresholds (e.g. -0.5) would only invert for strict 180° turns. Per
		// CoH-style natural-feel: even shallow backward components keep facing.
		const FFixedVector CurrentForward = CurrentFacing.RotateVector(FFixedVector::ForwardVector);
		const FFixedPoint Dot = FFixedVector::DotProduct(CurrentForward, ToTargetN);
		if (Dot < FFixedPoint::Zero)
		{
			Result.Facing = CurrentFacing;
			Result.bIsBackwardWalk = true;
			return Result;
		}
	}

	Result.Facing = TargetFacing;
	return Result;
}

FSeinFormationLayout USeinDefaultCommandBrokerResolver::ResolveFormationLayout_Implementation(
	USeinWorldSubsystem* World,
	const TArray<FSeinEntityHandle>& Members,
	FFixedVector CurrentCentroid,
	FFixedQuaternion CurrentFacing,
	FFixedVector TargetLocation,
	bool bInvertWhenBackward)
{
	const FSeinFormationFacing FacingResult = ComputeFormationFacing(
		CurrentCentroid, CurrentFacing, TargetLocation, bInvertWhenBackward);

	FSeinFormationLayout Layout;
	Layout.Facing = FacingResult.Facing;
	Layout.bIsBackwardWalk = FacingResult.bIsBackwardWalk;
	Layout.Positions = ResolvePositions(World, Members, TargetLocation, FacingResult.Facing);
	// Default resolver's grid is symmetric — backward-walk slot mirroring is a
	// squad-resolver concern (authored slot offsets are asymmetric). The flag
	// is still surfaced in the layout so previews can render a backward-walk
	// indicator if they want to.

	// Hook subclasses (e.g. cover-aware resolvers) to mutate positions before
	// the layout returns. Empty default impl on the base class — no-op for
	// non-overriding subclasses.
	PostProcessPositions(World, Members, Layout.Positions, TargetLocation);
	return Layout;
}

FSeinBrokerDispatchPlan USeinDefaultCommandBrokerResolver::ResolveDispatch_Implementation(
	USeinWorldSubsystem* World,
	FSeinEntityHandle BrokerHandle,
	const FSeinBrokerOrderInput& Order)
{
	FSeinBrokerDispatchPlan Plan;
	if (!World) return Plan;

	FSeinCommandBrokerData* Broker = World->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
	if (!Broker) return Plan;

	// Iterate the EFFECTIVE members — the subset this order targets (or the full
	// member list if the order is unrestricted). Formation positions are computed
	// over the effective set so subset orders layout around the target sensibly
	// (a 1-member "go repair that wall" order places that member at the target,
	// not at slot 3 of a 5-unit grid).
	const TArray<FSeinEntityHandle>& Effective = Order.EffectiveMembers;
	if (Effective.Num() == 0) return Plan;

	// Targeter-originated path: the player picked the ability before placing
	// targets, so we skip per-member context resolution. Consult the ability's
	// dispatch policy (All / Single / ByTag) via the shared helper on the
	// resolver base. Default policy (Mode::All) fans the ability out to every
	// capable member — replaces the historic "first capable only" rule.
	// Designers wanting "leader throws the smoke" set the ability's
	// DispatchMode to Single + Selector: Leader (or ByTag with a role tag) in
	// the ability BP CDO — that authoring drives every broker uniformly,
	// squad or selection.
	if (Order.PredeterminedAbilityTag.IsValid())
	{
		// Build candidates from the broker's capability map (squad-aware
		// dedup already applied if this is a squad broker). Member entries
		// must be in `Effective`; the broker handle itself (squad-owned
		// ability case) passes through.
		TArray<FSeinEntityHandle> Candidates;
		if (Broker)
		{
			if (const FSeinBrokerCapabilityBucket* Bucket = Broker->CapabilityMap.Find(Order.PredeterminedAbilityTag))
			{
				for (const FSeinEntityHandle& C : Bucket->Members)
				{
					if (C == BrokerHandle || Effective.Contains(C))
					{
						Candidates.Add(C);
					}
				}
			}
		}
		// Fallback for brokers whose capability map hasn't been built yet
		// (defensive — broker tick should have done this): walk Effective and
		// HasAbilityWithTag each member to assemble the candidate list.
		if (Candidates.Num() == 0)
		{
			for (const FSeinEntityHandle& Member : Effective)
			{
				const FSeinAbilityComponent* AC = World->GetComponent<FSeinAbilityComponent>(Member);
				if (AC && AC->HasAbilityWithTag(*World, Order.PredeterminedAbilityTag))
				{
					Candidates.Add(Member);
				}
			}
		}

		// Look up an ability instance to read its dispatch policy. Class-level
		// fields, so any candidate's instance is equivalent.
		const USeinAbility* Ability = nullptr;
		for (const FSeinEntityHandle& C : Candidates)
		{
			if (const FSeinAbilityComponent* AC = World->GetComponent<FSeinAbilityComponent>(C))
			{
				if (USeinAbility* Found = AC->FindAbilityByTag(*World, Order.PredeterminedAbilityTag))
				{
					Ability = Found;
					break;
				}
			}
		}

		const TArray<FSeinEntityHandle> Dispatchers = ApplyAbilityDispatchPolicy(
			World, BrokerHandle, Ability, Candidates);

		for (const FSeinEntityHandle& Dispatcher : Dispatchers)
		{
			FSeinBrokerMemberDispatch MD;
			MD.Member = Dispatcher;
			MD.AbilityTag = Order.PredeterminedAbilityTag;
			MD.TargetEntity = Order.TargetEntity;
			MD.TargetLocation = Order.TargetLocation;
			MD.TargeterPoints = Order.TargeterPoints;
			Plan.MemberDispatches.Add(MD);
		}
		return Plan;
	}

	// Formation layout: rotate the formation forward axis along centroid →
	// target and lay out per-member positions around the target. Single
	// entry point shared with the destination preview decals so commit and
	// preview never drift.
	const FSeinFormationLayout Layout = ResolveFormationLayout(
		World, Effective, Broker->Centroid, Broker->AnchorFacing,
		Order.TargetLocation, /*bInvertWhenBackward=*/false);
	Broker->AnchorFacing = Layout.Facing;
	const TArray<FFixedVector>& Positions = Layout.Positions;

	// Entity-targeted orders (attack a unit, repair a building, etc.): every
	// member dispatches against the same entity. Their own ability handles
	// "get in range" — formation slots don't apply because the target moves.
	// Non-entity-targeted (right-click ground / move): dispatch each member
	// to its formation slot, NOT the click point. Without this, every member
	// targets the same cell and you get the visible "30 units converging on
	// one cell" clumping bug. Mirrors the squad resolver's slot-routing.
	const bool bEntityTargeted = Order.TargetEntity.IsValid();

	Plan.MemberDispatches.Reserve(Effective.Num());

	for (int32 i = 0; i < Effective.Num(); ++i)
	{
		const FSeinEntityHandle Member = Effective[i];
		const FFixedVector MemberGoal = Positions.IsValidIndex(i) ? Positions[i] : Order.TargetLocation;

		const FSeinAbilityComponent* AC = World->GetComponent<FSeinAbilityComponent>(Member);
		if (!AC) continue;

		// Layer 1: per-member tag resolution. Virtual, so subclass overrides
		// apply here. Default impl reads FSeinAbilityComponent::ResolveCommandContext.
		const FGameplayTag ResolvedTag = ResolveMemberAbility(World, Member, Order.Context);

		// Primary: dispatch the resolved tag if the member owns that ability.
		if (ResolvedTag.IsValid() && AC->HasAbilityWithTag(*World, ResolvedTag))
		{
			FSeinBrokerMemberDispatch MD;
			MD.Member = Member;
			MD.AbilityTag = ResolvedTag;
			MD.TargetEntity = Order.TargetEntity;
			MD.TargetLocation = bEntityTargeted ? Order.TargetLocation : MemberGoal;
			Plan.MemberDispatches.Add(MD);
			continue;
		}

		// Tag-along: resolver-level fallback for members whose own tables didn't
		// map this context. Dispatches against the formation slot (cohesion),
		// not the order target. Opt-in — empty TagAlongAbility disables.
		if (TagAlongAbility.IsValid() && AC->HasAbilityWithTag(*World, TagAlongAbility))
		{
			FSeinBrokerMemberDispatch MD;
			MD.Member = Member;
			MD.AbilityTag = TagAlongAbility;
			MD.TargetLocation = MemberGoal;
			Plan.MemberDispatches.Add(MD);
			continue;
		}
		// else: no capable ability — silently skip. Member stays idle.
	}

	return Plan;
}

TArray<FFixedVector> USeinDefaultCommandBrokerResolver::ResolvePositions_Implementation(
	USeinWorldSubsystem* World,
	const TArray<FSeinEntityHandle>& Members,
	FFixedVector Anchor,
	FFixedQuaternion Facing)
{
	TArray<FFixedVector> Out;
	const int32 N = Members.Num();
	if (N == 0) return Out;
	Out.Reserve(N);

	// Uniform square-ish grid: compute side length = ceil(sqrt(N)), then iterate
	// row/column centered on anchor with InterUnitSpacing. Units = UE world cm.
	const FFixedPoint Spacing = InterUnitSpacing;
	int32 Side = 1;
	while (Side * Side < N) ++Side;

	const FFixedPoint HalfExtent = (FFixedPoint::FromInt(Side - 1) * Spacing) / FFixedPoint::Two;

	for (int32 i = 0; i < N; ++i)
	{
		const int32 Col = i % Side;
		const int32 Row = i / Side;

		// Offset in formation-local space (X forward, Y right).
		const FFixedVector LocalOffset(
			FFixedPoint::FromInt(Row) * Spacing - HalfExtent,
			FFixedPoint::FromInt(Col) * Spacing - HalfExtent,
			FFixedPoint::Zero
		);

		// Rotate by Facing, translate by Anchor.
		const FFixedVector WorldOffset = Facing.RotateVector(LocalOffset);
		FFixedVector SlotPos = Anchor + WorldOffset;

		// Project slot to passable nav cell. Without this, formation grids
		// spreading off raised platforms / past nav volume edges place
		// members on impassable terrain — they pathfind to the off-platform
		// cell via ramps and then steering tries to keep them at the
		// impassable destination, producing oscillation at ramp corners.
		// Projection snaps to the nearest walkable cell. Resolver unbound
		// (tests / nav-less games) → identity, behavior matches pre-projection.
		// Projection failure → fall back to Anchor (passable by definition,
		// it's the click cell).
		if (World && World->NavProjectResolver.IsBound())
		{
			FFixedVector Projected;
			if (World->NavProjectResolver.Execute(SlotPos, Projected))
			{
				SlotPos = Projected;
			}
			else
			{
				SlotPos = Anchor;
			}
		}

		Out.Add(SlotPos);
	}

	return Out;
}

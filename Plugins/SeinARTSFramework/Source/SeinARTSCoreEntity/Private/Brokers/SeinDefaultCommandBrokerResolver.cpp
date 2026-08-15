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
#include "Formations/SeinFormation.h"
#include "Formations/SeinBoxFormation.h"
#include "Formations/SeinWedgeFormation.h"
#include "Formations/SeinRingFormation.h"
#include "Formations/SeinSquareFormation.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Formations/SeinBlobFormation.h"
#include "Settings/PluginSettings.h"
#include "Math/MathLib.h"
#include "Types/Entity.h" // FSeinEntity — current member positions for the slot match
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace SeinDefaultBrokerLocal
{
	/** 1-D rank match along a single unit axis: project each member's CURRENT position and each slot
	 *  onto Axis, sort both (with a handle/slot-index tie-break → deterministic total order), pair by
	 *  rank. Translation-invariant — rank ignores the common offset, so no centroid subtraction is
	 *  needed and the linear projection can't overflow on large world coordinates. */
	static void Reassign1D(
		const TArray<FSeinEntityHandle>& Members,
		const TArray<FFixedVector>& MemberPos,
		TArray<FFixedVector>& Positions,
		const FFixedVector& Axis)
	{
		const int32 N = Positions.Num();
		TArray<int32> MemberOrder; TArray<int32> SlotOrder;
		TArray<FFixedPoint> MemberProj; TArray<FFixedPoint> SlotProj;
		MemberOrder.Reserve(N); SlotOrder.Reserve(N);
		MemberProj.SetNum(N); SlotProj.SetNum(N);
		for (int32 i = 0; i < N; ++i)
		{
			MemberProj[i] = FFixedVector::DotProduct(MemberPos[i], Axis);
			SlotProj[i]   = FFixedVector::DotProduct(Positions[i], Axis);
			MemberOrder.Add(i);
			SlotOrder.Add(i);
		}
		MemberOrder.Sort([&MemberProj, &Members](int32 A, int32 B)
		{
			if (MemberProj[A] != MemberProj[B]) return MemberProj[A] < MemberProj[B];
			return Members[A].Index < Members[B].Index;
		});
		SlotOrder.Sort([&SlotProj](int32 A, int32 B)
		{
			if (SlotProj[A] != SlotProj[B]) return SlotProj[A] < SlotProj[B];
			return A < B;
		});
		TArray<FFixedVector> NewPositions; NewPositions.SetNum(N);
		for (int32 k = 0; k < N; ++k) NewPositions[MemberOrder[k]] = Positions[SlotOrder[k]];
		Positions = MoveTemp(NewPositions);
	}

	/** 2-D nearest-slot match: greedily pair the globally-closest free (member, slot) first, in
	 *  centroid-ALIGNED local space (so the bulk move translation cancels — squared distances stay
	 *  formation-scale and the assignment is translation-invariant). Deterministic total order:
	 *  squared distance, then member handle index, then slot index. Min-squared-distance matching is
	 *  non-crossing, and this greedy closely approximates it for the blob → grid convergence that
	 *  formations produce. */
	static void Reassign2D(
		const TArray<FSeinEntityHandle>& Members,
		const TArray<FFixedVector>& MemberPos,
		TArray<FFixedVector>& Positions)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Formation_Reassign2D);
		const int32 N = Positions.Num();

		// Align both clouds by their own centroids: only the relative arrangement drives the match, and
		// squared distances stay small (no 32.32 overflow for a unit half a map from its slot).
		FFixedVector MCentroid = FFixedVector::ZeroVector;
		FFixedVector SCentroid = FFixedVector::ZeroVector;
		for (int32 i = 0; i < N; ++i) { MCentroid = MCentroid + MemberPos[i]; SCentroid = SCentroid + Positions[i]; }
		const FFixedPoint FN = FFixedPoint::FromInt(N);
		MCentroid = MCentroid / FN;
		SCentroid = SCentroid / FN;

		TArray<FFixedVector> MLocal; MLocal.SetNum(N);
		TArray<FFixedVector> SLocal; SLocal.SetNum(N);
		for (int32 i = 0; i < N; ++i)
		{
			MLocal[i] = MemberPos[i] - MCentroid; MLocal[i].Z = FFixedPoint::Zero;
			SLocal[i] = Positions[i]  - SCentroid; SLocal[i].Z = FFixedPoint::Zero;
		}

		struct FPair { FFixedPoint DistSq; int32 M; int32 S; };
		TArray<FPair> Pairs; Pairs.Reserve(N * N);
		for (int32 m = 0; m < N; ++m)
		{
			for (int32 s = 0; s < N; ++s)
			{
				const FFixedVector D = MLocal[m] - SLocal[s];
				Pairs.Add({ D.X * D.X + D.Y * D.Y, m, s });
			}
		}
		Pairs.Sort([&Members](const FPair& A, const FPair& B)
		{
			if (A.DistSq != B.DistSq) return A.DistSq < B.DistSq;
			if (Members[A.M].Index != Members[B.M].Index) return Members[A.M].Index < Members[B.M].Index;
			return A.S < B.S;
		});

		TArray<int32> MemberSlot; MemberSlot.Init(INDEX_NONE, N);
		TArray<bool> SlotTaken;   SlotTaken.Init(false, N);
		int32 Assigned = 0;
		for (const FPair& P : Pairs)
		{
			if (Assigned >= N) break;
			if (MemberSlot[P.M] != INDEX_NONE || SlotTaken[P.S]) continue;
			MemberSlot[P.M] = P.S;
			SlotTaken[P.S] = true;
			++Assigned;
		}

		// 2-OPT UN-CROSS. Greedy nearest-pair is a heuristic and can leave crossed paths — most visibly
		// along DEPTH, where a shallow/clumped source cloud gives it little to separate front-from-back,
		// so it resolves near-equidistant slots by tie-break (a unit "routes into the middle"). Sweep
		// every member pair and swap their two slots whenever that lowers the summed squared distance.
		// Each swap STRICTLY lowers total cost over a finite assignment set, so this converges; a
		// converged (no-improving-swap) assignment is monotone in every direction = no crossings.
		// Deterministic: fixed (i, j) iteration order, fixed-point costs, run-to-stable.
		auto LocalDistSq = [](const FFixedVector& A, const FFixedVector& B)
		{
			const FFixedVector D = A - B;
			return D.X * D.X + D.Y * D.Y;
		};
		bool bImproved = true;
		while (bImproved)
		{
			bImproved = false;
			for (int32 i = 0; i < N; ++i)
			{
				for (int32 j = i + 1; j < N; ++j)
				{
					const int32 Si = MemberSlot[i];
					const int32 Sj = MemberSlot[j];
					const FFixedPoint Now     = LocalDistSq(MLocal[i], SLocal[Si]) + LocalDistSq(MLocal[j], SLocal[Sj]);
					const FFixedPoint Swapped  = LocalDistSq(MLocal[i], SLocal[Sj]) + LocalDistSq(MLocal[j], SLocal[Si]);
					if (Swapped < Now)
					{
						MemberSlot[i] = Sj;
						MemberSlot[j] = Si;
						bImproved = true;
					}
				}
			}
		}

		TArray<FFixedVector> NewPositions; NewPositions.SetNum(N);
		for (int32 m = 0; m < N; ++m) NewPositions[m] = Positions[MemberSlot[m]];
		Positions = MoveTemp(NewPositions);
	}
}

USeinDefaultCommandBrokerResolver::USeinDefaultCommandBrokerResolver()
{
	// Ship the stock formations mapped to their tags so order gestures / designers can
	// nominate any of them via FormationsByTag. The default order gesture nominates NONE
	// for a drag (so it falls back to the project Default Formation, default Box) and
	// Formation.Blob for a single-point click. Designers re-point/extend this map on the CDO.
	FormationsByTag.Add(SeinARTSTags::Formation_Box,    USeinBoxFormation::StaticClass());
	FormationsByTag.Add(SeinARTSTags::Formation_Wedge,  USeinWedgeFormation::StaticClass());
	FormationsByTag.Add(SeinARTSTags::Formation_Ring,   USeinRingFormation::StaticClass());
	FormationsByTag.Add(SeinARTSTags::Formation_Square, USeinSquareFormation::StaticClass());
	FormationsByTag.Add(SeinARTSTags::Formation_Blob,   USeinBlobFormation::StaticClass());
}

void USeinDefaultCommandBrokerResolver::ReassignSlots(
	USeinWorldSubsystem* World,
	const TArray<FSeinEntityHandle>& Members,
	TArray<FFixedVector>& Positions,
	FFixedQuaternion FormationFacing,
	bool bLateral,
	bool bDepth)
{
	const int32 N = Positions.Num();
	if (N <= 1 || !World || Members.Num() < N) return;
	if (!bLateral && !bDepth) return; // nothing opted in → keep ResolvePositions' index order

	// Snapshot each member's CURRENT position. Fallback to the slot if the entity vanished mid-resolve
	// — keeps the arrays index-aligned without a special case downstream.
	TArray<FFixedVector> MemberPos; MemberPos.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		const FSeinEntity* Entity = World->GetEntity(Members[i]);
		MemberPos[i] = Entity ? Entity->Transform.GetLocation() : Positions[i];
	}

	// FOOTPRINT-CLASS PARTITION. A footprint-aware formation reserves each slot for a SPECIFIC
	// footprint — a 3×3 box slot is sized for a tank, a 1×1 for a rifleman — and the per-member Radii
	// it emits are index-aligned to MEMBERS, not slots. The anti-cross re-match below permutes only
	// Positions, so a footprint-BLIND global match (the old code) would hand the tank's reserved slot
	// to whichever unit happens to stand nearest it and drop the tank into a rifleman's slot — the big
	// ring then renders at a small slot, the small ring in the big space. So we re-match INSIDE each
	// footprint class only: a class's members compete for exactly the slots the formation gave that
	// class. Anti-cross is preserved among same-size units; a cross-size swap can never happen. The
	// uniform-infantry case is one class == a single partition == the old whole-array behaviour.
	TArray<FFixedPoint> Radii;
	USeinFormation::GatherFootprintRadii(World, Members, Radii);
	TArray<FFixedPoint> Classes;
	for (int32 i = 0; i < N; ++i) { Classes.AddUnique(Radii[i]); }

	// One axis → 1-D rank match along it. Lateral = the formation RIGHT axis (left/right order);
	// Depth = the formation FORWARD axis (front/back order). Loop-invariant, hoisted out.
	const FFixedVector Axis = bLateral
		? FormationFacing.RotateVector(FFixedVector::RightVector)
		: FormationFacing.RotateVector(FFixedVector::ForwardVector);

	for (const FFixedPoint& ClassRadius : Classes)
	{
		TArray<int32> Idx;
		for (int32 i = 0; i < N; ++i) { if (Radii[i] == ClassRadius) { Idx.Add(i); } }
		if (Idx.Num() <= 1) { continue; } // a lone unit in its class — its reserved slot is fixed.

		TArray<FSeinEntityHandle> SubMembers;   SubMembers.Reserve(Idx.Num());
		TArray<FFixedVector>      SubMemberPos; SubMemberPos.Reserve(Idx.Num());
		TArray<FFixedVector>      SubPositions; SubPositions.Reserve(Idx.Num());
		for (const int32 i : Idx)
		{
			SubMembers.Add(Members[i]);
			SubMemberPos.Add(MemberPos[i]);
			SubPositions.Add(Positions[i]);
		}

		if (bLateral && bDepth) { SeinDefaultBrokerLocal::Reassign2D(SubMembers, SubMemberPos, SubPositions); }
		else                    { SeinDefaultBrokerLocal::Reassign1D(SubMembers, SubMemberPos, SubPositions, Axis); }

		for (int32 k = 0; k < Idx.Num(); ++k) { Positions[Idx[k]] = SubPositions[k]; }
	}
}

FSeinFormationLayout USeinDefaultCommandBrokerResolver::ResolveFormationLayout_Implementation(
	USeinWorldSubsystem* World,
	const TArray<FSeinEntityHandle>& Members,
	const FSeinOrderTarget& Target,
	bool bReassignLateral,
	bool bReassignDepth)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Formation_ResolveLayout);
	FSeinFormationLayout Layout;
	ESeinFormationFacing FacingMode = ESeinFormationFacing::Uniform;
	// An explicit class override (e.g. a squad's authored FormationClass) wins over the tag map; else
	// resolve the gesture FormationTag via FormationsByTag / DefaultFormationClass as usual.
	const USeinFormation* Formation = nullptr;
	if (!Target.FormationClass.IsNull())
	{
		if (UClass* OverrideClass = Target.FormationClass.LoadSynchronous())
		{
			if (!OverrideClass->HasAnyClassFlags(CLASS_Abstract))
			{
				Formation = GetDefault<USeinFormation>(OverrideClass);
			}
		}
	}
	if (!Formation) { Formation = ResolveFormation(Target.FormationTag); }
	if (Formation)
	{
		// Pluggable formation owns positions + facing. Preview and commit both
		// enter the same stateless scratch boundary; the CDO is config only.
		USeinFormation::ExecuteStateless(
			World,
			Formation->GetClass(),
			Members,
			Target,
			Layout,
			FacingMode);
	}
	else
	{
		// Fallback: framework-default geometry via this resolver's own ResolvePositions
		// (a blob unless a subclass overrides it — e.g. the squad resolver's authored
		// slots). Facing via the shared static now living on USeinFormation.
		Layout.Facing    = USeinFormation::ComputeFormationFacing(Target.CurrentCentroid, Target.CurrentFacing, Target.Anchor);
		Layout.Positions = ResolvePositions(World, Members, Target.Anchor, Layout.Facing);
	}

	// De-overlap / de-dup safety net: no two members may sit on top of each other (footprint-aware,
	// using each unit's real radius). Spreads slots that under-spaced on a tight curve/corner, and any
	// that nav-projected to the same nearest free cell when snapped off the play-area edge. For
	// degenerate layouts this pass CREATES the real geometry — Blob emits every position ON the
	// anchor and relies on this scatter. Shared path → preview === commit.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Formation_Separate);
		USeinFormation::SeparatePositions(Layout.Radii, Layout.Positions, 16);
	}

	// Placement safety net: the de-overlap above is nav- and occupancy-blind, so it can shove an edge
	// slot off the play area, and raw layouts can drop slots onto PARKED units (an order into a settled
	// crowd). Clamp any position left off the nav area OR on an idle body onto the nearest FREE cell,
	// occupancy-aware so they pack open ground without piling. Members of THIS order are excluded from
	// the occupancy read — they vacate their spots. Runs BEFORE the cover hook so authoritative cover
	// slots (which intentionally overrule the bake) are the last word. Shared path → preview === commit.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Formation_ProjectNavigable);
		USeinFormation::ProjectPositionsToNavigable(World, Layout.Radii, Layout.Positions, Members);
	}

	// ANTI-CROSS SLOT MATCH. Re-match members to slots so a rotating/translating formation doesn't
	// make everyone cross to their old INDEX slot (the cross-cutting-paths bug). Runs on the FINAL
	// slot geometry — AFTER the scatter and nav/occupancy projection — because it must see real
	// slots to minimize crossings: matching before the scatter degenerated on Blob click orders
	// (every slot still ON the anchor → every pairing cost identical → index-order match), handing
	// members far-side spots in the destination disc so the crowd's internal goal vectors crossed
	// maximally (measured as the departure jam's presser standoff). Pure PERMUTATION, partitioned
	// by footprint class inside ReassignSlots: no slot moves (the projected/separated geometry —
	// and the preview's slot set — is untouched) and same-radius-only swaps preserve every pairwise
	// spacing guarantee SeparatePositions just established. Per-axis via the two flags — the default
	// resolver passes its formation-level opt-OUT flags (default both on → 2-D); the squad resolver
	// passes the squad's per-squad opt-IN flags. Both paths run THROUGH this shared call, so preview
	// and commit stay byte-identical. Deterministic. Stays BEFORE the cover hook: cover snap binds a
	// specific member's slot to a cover position, so the member↔slot pairing must be final first.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Formation_ReassignSlots);
		ReassignSlots(World, Members, Layout.Positions, Layout.Facing, bReassignLateral, bReassignDepth);
	}

	// Hook subclasses (e.g. cover-aware resolvers) to mutate positions before
	// the layout returns. Empty default impl on the base class — no-op for
	// non-overriding subclasses.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Formation_PostProcess);
		PostProcessPositions(World, Members, Layout.Positions, Target.Anchor);
	}

	// Per-member facing from the formation's FacingMode, computed from the FINAL positions (a ring
	// faces each member radially out). Carried to consumers — a squad rotates its whole authored body
	// to its element's facing. Uniform formations just replicate Layout.Facing. Shared path → preview
	// === commit.
	USeinFormation::ComputeMemberFacings(FacingMode, Layout.Positions, Target.Anchor, Layout.Facing, Layout.Facings);
	return Layout;
}

FSeinBrokerDispatchPlan USeinDefaultCommandBrokerResolver::ResolveDispatch_Implementation(
	USeinWorldSubsystem* World,
	FSeinEntityHandle BrokerHandle,
	const FSeinBrokerOrderInput& Order)
{
	FSeinBrokerDispatchPlan Plan;
	if (!World) return Plan;

	FSeinCommandBrokerData* Broker =
		World->GetComponentMutable<FSeinCommandBrokerData>(
			BrokerHandle);
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

	// Per-member goal positions. A2: when the order carries PRE-PLACED positions (the loose subset of a
	// UNIFIED parent formation already solved in ProcessCommands so squads + loose share ONE shape), use
	// them directly — solving a formation here too is exactly what made a mixed selection render two
	// overlapping formations. Otherwise solve the formation as usual (the shared entry the preview also
	// calls, so commit and preview never drift).
	TArray<FFixedVector> Positions;
	TArray<FFixedQuaternion> SlotFacings; // filled by the layout branch; padded at capture below
	const FSeinPlayerID BrokerOwner = World->GetEntityOwner(BrokerHandle);
	const int32 BrokerResolverID = Broker->ResolverID;
	const TArray<FSeinEntityHandle> BrokerMembers = Broker->Members;
	const FFixedVector BrokerCentroid = Broker->Centroid;
	const FFixedQuaternion BrokerAnchorFacing = Broker->AnchorFacing;
	if (Order.DestinationArtifact.Num() > 0)
	{
		Positions.Reserve(Effective.Num());
		for (const FSeinEntityHandle& Member : Effective)
		{
			const FSeinFrozenDestination* Frozen =
				Order.DestinationArtifact.FindByPredicate(
					[Member](const FSeinFrozenDestination& Entry)
					{
						return Entry.Member == Member;
					});
			Positions.Add(Frozen ? Frozen->WorldPosition : Order.TargetLocation);
		}
	}
	else if (Order.PreplacedPositions.Num() > 0)
	{
		Positions.Reserve(Effective.Num());
		for (const FSeinEntityHandle& Member : Effective)
		{
			const int32 Pidx = Order.PreplacedMembers.IndexOfByKey(Member);
			Positions.Add(Order.PreplacedPositions.IsValidIndex(Pidx) ? Order.PreplacedPositions[Pidx] : Order.TargetLocation);
		}
	}
	else
	{
		// Formation layout: rotate the formation forward axis along centroid → target and lay out
		// per-member positions around the target.
		FSeinOrderTarget Target;
		Target.Anchor          = Order.TargetLocation;
		Target.GuidePoints     = Order.GuidePoints;
		Target.TargetEntity    = Order.TargetEntity;
		Target.FormationTag    = Order.FormationTag;
		Target.CurrentCentroid = BrokerCentroid;
		Target.CurrentFacing   = BrokerAnchorFacing;

		// ResolveFormationLayout reaches designer-pluggable formation, position,
		// and post-process hooks. Any of them may synchronously grow component
		// storage, so no broker pointer may survive the call.
		Broker = nullptr;
		const FSeinFormationLayout Layout = ResolveFormationLayout(
			World, Effective, Target, bReassignSlotsLateral, bReassignSlotsDepth);

		Broker = World->GetComponentMutable<FSeinCommandBrokerData>(
			BrokerHandle);
		if (!World->IsEntityAlive(BrokerHandle) || !Broker
			|| World->GetEntityOwner(BrokerHandle) != BrokerOwner
			|| Broker->ResolverID != BrokerResolverID
			|| Broker->Centroid != BrokerCentroid
			|| Broker->Members != BrokerMembers)
		{
			// The broker system's outer precondition rejects the stale plan and
			// retries the still-pending order on a later tick.
			return Plan;
		}
		Plan.bApplyAnchorFacing = true;
		Plan.AnchorFacing = Layout.Facing;
		Positions = Layout.Positions;
		SlotFacings = Layout.Facings;
	}

	// Entity-targeted orders (attack a unit, repair a building, etc.): every
	// member dispatches against the same entity. Their own ability handles
	// "get in range" — formation slots don't apply because the target moves.
	// Non-entity-targeted (right-click ground / move): dispatch each member
	// to its formation slot, NOT the click point. Without this, every member
	// targets the same cell and you get the visible "30 units converging on
	// one cell" clumping bug. Mirrors the squad resolver's slot-routing.
	const bool bEntityTargeted = Order.TargetEntity.IsValid();

	// Return the resolved layout for transactional broker-system commit. The formation
	// owns its slots; members are transient assignees (which member fills which slot is
	// re-decided at use via ReassignSlots). FULL-BROKER ground orders only: entity-targeted
	// dispatches carry no slots, and SUBSET orders (a shift-click on part of the selection,
	// an AutoMoveThen prefix, an idle re-form wave) must never clobber the formation's
	// standing layout with a partial one. Facings pad with AnchorFacing for paths that
	// carry none (pre-placed parent slots). Consumers: formation re-form / re-seek +
	// per-slot settle policies.
	if (!bEntityTargeted && Positions.Num() > 0
		&& Effective.Num() == BrokerMembers.Num())
	{
		const FFixedQuaternion SettledFacing = Plan.bApplyAnchorFacing
			? Plan.AnchorFacing : BrokerAnchorFacing;
		while (SlotFacings.Num() < Positions.Num())
		{
			SlotFacings.Add(SettledFacing);
		}
		SlotFacings.SetNum(Positions.Num());
		Plan.bApplySettledSlots = true;
		Plan.SettledSlotPositions = Positions;
		Plan.SettledSlotFacings = MoveTemp(SlotFacings);
		Plan.bSettledSlotsMemberAligned = false;
	}

	Plan.MemberDispatches.Reserve(Effective.Num());

	for (int32 i = 0; i < Effective.Num(); ++i)
	{
		const FSeinEntityHandle Member = Effective[i];
		const FFixedVector MemberGoal = Positions.IsValidIndex(i) ? Positions[i] : Order.TargetLocation;
		if (!World->GetComponent<FSeinAbilityComponent>(Member)) continue;

		// Layer 1: per-member tag resolution. Virtual, so subclass overrides
		// apply here. Default impl reads FSeinAbilityComponent::ResolveCommandContext.
		const FGameplayTag ResolvedTag = ResolveMemberAbility(World, Member, Order.Context);

		// ResolveMemberAbility is Blueprint-pluggable and may grow or replace
		// component storage. Acquire the member component only after it returns.
		const FSeinAbilityComponent* AC = World->GetComponent<FSeinAbilityComponent>(Member);
		if (!AC) continue;

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
	// Framework-default geometry: a BLOB — every member shares the one (already
	// nav-projected) anchor. The mass-select single-destination model; the hard collision floor packs
	// them into a no-overlap cluster on arrival. This is the fallback used when no
	// USeinFormation is configured (DefaultFormationClass null). Real spread / shape
	// is opt-in via a USeinFormation (Grid, Line, custom) selected through
	// ResolveFormation. Subclasses (e.g. the squad resolver) override this for
	// authored layouts.
	TArray<FFixedVector> Out;
	Out.Init(Anchor, Members.Num());
	return Out;
}

const USeinFormation* USeinDefaultCommandBrokerResolver::ResolveFormation(FGameplayTag FormationTag) const
{
	TSoftClassPtr<USeinFormation> ClassPtr;
	if (FormationTag.IsValid())
	{
		if (const TSoftClassPtr<USeinFormation>* Found = FormationsByTag.Find(FormationTag))
		{
			ClassPtr = *Found;
		}
	}
	if (ClassPtr.IsNull())
	{
		ClassPtr = DefaultFormationClass;
	}
	if (ClassPtr.IsNull())
	{
		// Ultimate fallback: the project-wide Default Formation (Project Settings ->
		// SeinARTS -> Formation). Lets designers set the default order formation without
		// subclassing the resolver. Reached only when neither a gesture tag nor this
		// resolver's own DefaultFormationClass resolved (e.g. the loose-unit default
		// resolver). The squad resolver sets DefaultFormationClass = SlotFormation, so it
		// never falls through here -- squads keep their authored slots.
		if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
		{
			if (UClass* DefaultClass = Settings->DefaultFormation)
			{
				if (!DefaultClass->HasAnyClassFlags(CLASS_Abstract))
				{
					return GetDefault<USeinFormation>(DefaultClass);
				}
			}
			else
			{
				// DefaultFormation is None → no formation shape (WYSIWYG). Loose orders use the blob
				// fallback below; that is a valid mode, so this is a gentle one-time nudge, not an
				// error. The render-side preview path reaches here first; the log dedupes to once.
				USeinARTSCoreSettings::ReportDisabledSystem(TEXT("Default Formation"),
					TEXT("Loose move orders use the blob (no formation shape); set a Default Formation to enable shapes."), /*bHighSeverity*/ false);
			}
		}
		return nullptr; // neither resolves → caller uses the blob ResolvePositions fallback
	}

	UClass* FormationClass = ClassPtr.LoadSynchronous();
	if (!FormationClass || FormationClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return nullptr;
	}
	// The CDO supplies immutable configuration. ResolveFormationLayout executes
	// on the per-world stateless scratch boundary.
	return GetDefault<USeinFormation>(FormationClass);
}

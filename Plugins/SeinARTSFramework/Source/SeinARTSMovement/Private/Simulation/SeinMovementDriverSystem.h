/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementDriverSystem.h
 * @brief   The always-on per-unit movement driver (CP2.1, Decisions D-R2).
 *
 *          Every entity carrying an FSeinMovementComponent is driven EVERY sim
 *          tick — there is no tick-orphaned state anymore. The split of one
 *          tick's responsibility:
 *            - An entity with an ACTIVE move order this tick was already
 *              steered by its USeinMoveToAction (the latent-action manager
 *              ticks FIRST in the AbilityExecution phase, hardcoded in
 *              USeinWorldSubsystem::TickSystems before the phase's registered
 *              systems run) — `FSeinMovementComponent::bHasTarget` is therefore
 *              the authoritative "an order steered me this tick" discriminator
 *              by the time this system reads it, and the driver skips the unit.
 *            - Everything else gets `USeinMovement::TickIdle` on its PERSISTENT
 *              movement instance (USeinMovementSubsystem registry): first-
 *              contact ground snap (this subsumed the retired
 *              FSeinInitialSnapSystem), coast-down of residual order momentum
 *              through the decel ramp, and per-tick shove-settle (Z/slope
 *              re-snap at the current position — settle-in-place semantics, no
 *              return-to-home).
 *
 *          Contained entities (garrison / transport / attachment) are posed by
 *          their container — the driver never ground-snaps or coasts them.
 *
 *          DETERMINISM: iteration drives off the movement storage's live bits
 *          in ascending slot order (SeinQuery::ForEachAlive — identical order
 *          and visited set as the old entity-pool iteration, stable across
 *          peers); TickIdle is pure self-mutation (no neighbour reads,
 *          no spatial-hash queries — see its docstring), so pool order is not
 *          load-bearing. All fixed-point. The registry sweep removes dead
 *          entries only (no sim-state mutation), so its timing is inert.
 *
 *          CP2.3 NOTE (momentum push): when the push lands, the per-unit move
 *          STEP (integrate + push exchange + nav floor) migrates here as the
 *          single integration point for ordered AND idle motion — this system
 *          is the seam it slots into. Until then ordered integration stays
 *          inside the order's steering tick (unchanged legacy structure).
 *
 * Phase: AbilityExecution | Priority: 10 — after the latent-action ticks
 *        (hardcoded pre-systems) and ability ticks (priority 0), before
 *        Production (50). PostTick collision resolution still runs after all
 *        movement, exactly as before.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Core/SeinSystemPriority.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/ComponentStorage.h"
#include "Simulation/SeinComponentQuery.h"
#include "Core/SeinParallel.h"
#include "SeinMovementSubsystem.h"
#include "Movement/SeinMovement.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Components/SeinContainmentMemberData.h"
#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"
#include "SeinPathTypes.h"
#include "Types/Entity.h"
#include "Engine/World.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FSeinMovementDriverSystem final : public ISeinSystem
{
public:
	explicit FSeinMovementDriverSystem(USeinMovementSubsystem* InOwner)
		: OwnerSubsystem(InOwner)
	{}

	virtual void Tick(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override
	{
		USeinMovementSubsystem* Sub = OwnerSubsystem.Get();
		if (!Sub) return;

		// Drop registry entries for dead entities. Handle generations make a
		// recycled pool slot a DIFFERENT key, so stale entries can never
		// collide with a new entity — this is purely a memory sweep.
		Sub->SweepStaleMovementInstances(World);

		// Resolve the active nav once per tick. May be null / bake-less early
		// in a level's life — TickIdle's first-contact branch waits on
		// HasRuntimeData and retries, so this is not an error state.
		USeinNavigation* Nav = nullptr;
		if (UWorld* UnrealWorld = World.GetWorld())
		{
			if (USeinNavigationSubsystem* NavSub = UnrealWorld->GetSubsystem<USeinNavigationSubsystem>())
			{
				Nav = NavSub->GetNavigation();
			}
		}

		// Shared empty path satisfying the context shape — TickIdle never
		// reads Path / the waypoint index (contract in its docstring).
		static const FSeinPath IdlePath;

		// Gather idle units serially (movement-instance creation + its registry
		// insert MUST be serial), pre-fetching the stable component pointers — no
		// AddComponent runs this phase, so they don't dangle. Partition by whether
		// the movement CLASS is native: a native (C++) mode's TickIdle is compiled
		// code and fans cleanly across worker threads; a Blueprint-authored mode's
		// TickIdle may be a BP graph, and UE's Blueprint VM is NOT thread-safe, so
		// those stay on the serial spine. TickIdle is otherwise pure self-mutation
		// (its docstring: no neighbour reads, no spatial queries; nav reads are the
		// scratch-free immutable bake), so the native batch is a clean SeinParallelFor.
		// `Sein.Sim.Parallel 0` forces it all serial; the result is bit-identical.
		struct FIdleUnit
		{
			FSeinEntity* Entity;
			FSeinEntityHandle Handle;
			FSeinMovementComponent* Move;
			const FSeinNavigationComponent* NavComp;
			USeinMovement* Movement;
			FSeinEntity EntityBefore;
			FFixedVector VelocityBefore;
			FFixedPoint SmoothedPitchBefore;
			FFixedPoint SmoothedRollBefore;
			bool bInitialGroundSnapDoneBefore;
			FFixedVector HomePosBefore;
			bool bHomeSeededBefore;
			bool bDeferredStateTracking;
		};
		TArray<FIdleUnit> NativeIdle;   // C++ movement class — parallel-safe
		TArray<FIdleUnit> ScriptIdle;   // Blueprint movement class — serial only

		FSeinEntityPool* Pool = World.GetEntityPoolMutable();
		if (!Pool) return;
		// Hoisted typed views (SeinComponentQuery.h): one storage resolve per
		// type per tick; per-entity joins below are direct slot arithmetic
		// instead of a hash-map probe per component per entity. Iteration
		// order (ascending slot) and the visited set are identical to the old
		// pool-order + GetComponent-null-check pattern.
		TSeinComponentView<FSeinMovementComponent> MoveView(World);
		// Read-only joins use the ungated read view — exact match for the old
		// const GetComponent<T> semantics (no mutable-state-access gate).
		TSeinComponentReadView<FSeinContainmentMemberData> ContainView(World);
		TSeinComponentReadView<FSeinNavigationComponent> NavView(World);
		if (!MoveView.IsBound()) return;
		const FSeinEntityPool& ConstPool = World.GetEntityPool();
		SeinQuery::ForEachAlive(ConstPool, MoveView,
			[&](FSeinEntityHandle Handle, int32 Slot)
		{
			const FSeinMovementComponent* ReadMove = MoveView.GetConstAt(Slot);
			if (!ReadMove) return;

			// An active move order steered this entity this tick — the order
			// owns the tick. (Set every USeinMoveToAction::TickAction; cleared
			// on complete / cancel / fail via ResetTransientMoveState.)
			if (ReadMove->bHasTarget) return;

			// Contained entities are posed by their container.
			if (const FSeinContainmentMemberData* Containment =
					ContainView.GetConstAt(Slot, Handle))
			{
				if (Containment->CurrentContainer.IsValid()) return;
			}

			const FSeinEntity* ReadEntityPtr = ConstPool.Get(Handle);
			if (!ReadEntityPtr) return;
			const FSeinEntity& ReadEntity = *ReadEntityPtr;

			USeinMovement* Movement =
				Sub->GetOrCreateMovementInstance(Handle, *ReadMove);
			if (!Movement) return;
			// Blueprint movement classes may mutate their own reflected variables in
			// BP_TickIdle, so conservatively invalidate their policy object. The
			// shipped native classes all use USeinMovement's native idle implementation;
			// its persistent state lives on Entity/MovementData, not reflected policy
			// fields, so invalidating every native policy object here only forced a
			// redundant UObject serialization on every root boundary.
			const bool bScriptMovement = !Movement->GetClass()->IsNative();
			const bool bDeferredStateTracking =
				!bScriptMovement
				&& Movement->SupportsExactIdleMutationTracking();
			FSeinEntity* Entity = bDeferredStateTracking
				? Pool->GetForDeferredMutation(Handle)
				: World.GetEntityMutable(Handle);
			FSeinMovementComponent* Move = bDeferredStateTracking
				? MoveView.GetDeferredAt(Slot)
				: MoveView.GetMutableAt(Slot);
			if (!Entity || !Move) return;
			if (!bDeferredStateTracking)
			{
				Sub->MarkMovementStateDirty(Handle);
			}

			const FIdleUnit Unit{ Entity, Handle, Move,
				NavView.GetConstAt(Slot, Handle),
				Movement,
				ReadEntity,
				Move->Velocity,
				Move->SmoothedPitch,
				Move->SmoothedRoll,
				Move->bInitialGroundSnapDone,
				Move->HomePos,
				Move->bHomeSeeded,
				bDeferredStateTracking };
			(bScriptMovement ? ScriptIdle : NativeIdle).Add(Unit);
		});

		// Tick one idle unit: builds the per-unit context (own WaypointIndex local
		// so concurrent bodies never share it) and runs TickIdle on its instance.
		const auto TickOneIdle = [&](const FIdleUnit& Unit)
		{
			int32 IdleWaypointIndex = 0;
			FSeinMovementContext Ctx{
				*Unit.Entity,
				Unit.Move,
				Unit.NavComp,
				IdlePath,
				IdleWaypointIndex,
				FFixedPoint::Zero,   // acceptance — meaningless while idle
				DeltaTime,
				Nav,
				&World,
				Unit.Handle
			};
			Unit.Movement->TickIdle(Ctx);
		};

		// Native modes → parallel. Blueprint modes → serial (BP VM not thread-safe).
		SeinParallelFor(NativeIdle.Num(), [&](int32 Index) { TickOneIdle(NativeIdle[Index]); });
		for (const FIdleUnit& Unit : NativeIdle)
		{
			if (!Unit.bDeferredStateTracking) continue;
			const bool bEntityChanged =
				Unit.Entity->ID != Unit.EntityBefore.ID
				|| Unit.Entity->Transform != Unit.EntityBefore.Transform
				|| Unit.Entity->Flags != Unit.EntityBefore.Flags;
			if (bEntityChanged)
			{
				Pool->CommitDeferredMutation(Unit.Handle);
			}
			const bool bMovementChanged =
				Unit.Move->Velocity != Unit.VelocityBefore
				|| Unit.Move->SmoothedPitch != Unit.SmoothedPitchBefore
				|| Unit.Move->SmoothedRoll != Unit.SmoothedRollBefore
				|| Unit.Move->bInitialGroundSnapDone
					!= Unit.bInitialGroundSnapDoneBefore
				|| Unit.Move->HomePos != Unit.HomePosBefore
				|| Unit.Move->bHomeSeeded != Unit.bHomeSeededBefore;
			if (bMovementChanged)
			{
				MoveView.CommitDeferred(Unit.Handle);
			}
		}
		for (const FIdleUnit& Unit : ScriptIdle) { TickOneIdle(Unit); }
	}

	virtual FSeinSystemDescriptor DescribeSystem() const override
	{
		return FSeinSystemDescriptor::WithCanonicalState(
			FName(TEXT("seinarts.movement.driver")),
			1u,
			ESeinTickPhase::AbilityExecution,
			SeinSystemPriority::MovementDriver,
			{FName(TEXT(
				"seinarts.movement/persistent-policy-instances"))});
	}

private:
	/** The owning movement subsystem — hosts the persistent-instance registry
	 *  (and GC-roots the instances). Weak: the subsystem outlives this system
	 *  by construction (it registers/unregisters us), the weak ptr is belt-
	 *  and-braces against teardown races. */
	TWeakObjectPtr<USeinMovementSubsystem> OwnerSubsystem;
};

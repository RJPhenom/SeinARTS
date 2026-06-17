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
 *              re-snap at the current position — BAR semantics, no
 *              return-to-home).
 *
 *          Contained entities (garrison / transport / attachment) are posed by
 *          their container — the driver never ground-snaps or coasts them.
 *
 *          DETERMINISM: iteration is entity-pool order (index order — stable
 *          across peers); TickIdle is pure self-mutation (no neighbour reads,
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

		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			FSeinMovementComponent* Move = World.GetComponent<FSeinMovementComponent>(Handle);
			if (!Move) return;

			// An active move order steered this entity this tick — the order
			// owns the tick. (Set every USeinMoveToAction::TickAction; cleared
			// on complete / cancel / fail via ResetTransientMoveState.)
			if (Move->bHasTarget) return;

			// Contained entities are posed by their container.
			if (const FSeinContainmentMemberData* Containment =
					World.GetComponent<FSeinContainmentMemberData>(Handle))
			{
				if (Containment->CurrentContainer.IsValid()) return;
			}

			USeinMovement* Movement = Sub->GetOrCreateMovementInstance(Handle, *Move);
			if (!Movement) return;

			int32 IdleWaypointIndex = 0;
			FSeinMovementContext Ctx{
				Entity,
				Move,
				World.GetComponent<FSeinNavigationComponent>(Handle),
				IdlePath,
				IdleWaypointIndex,
				FFixedPoint::Zero,   // acceptance — meaningless while idle
				DeltaTime,
				Nav,
				&World,
				Handle
			};
			Movement->TickIdle(Ctx);
		});
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::AbilityExecution; }
	virtual int32 GetPriority() const override { return SeinSystemPriority::MovementDriver; }
	virtual FName GetSystemName() const override { return TEXT("MovementDriver"); }

private:
	/** The owning movement subsystem — hosts the persistent-instance registry
	 *  (and GC-roots the instances). Weak: the subsystem outlives this system
	 *  by construction (it registers/unregisters us), the weak ptr is belt-
	 *  and-braces against teardown races. */
	TWeakObjectPtr<USeinMovementSubsystem> OwnerSubsystem;
};

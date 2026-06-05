/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinInitialSnapSystem.h
 * @brief   PreTick one-time spawn floor-snap.
 *
 *          A unit placed in a level keeps its authored transform (placed Z, no
 *          slope) until its first move order runs the movement Tick's ground snap —
 *          so idle / never-moved units float or clip until ordered. This system
 *          snaps each entity ONCE: for any entity with a movement component whose
 *          bInitialGroundSnapDone flag is still false, it instantly snaps Z + slope
 *          pitch/roll to the terrain (the same result movement produces) and sets
 *          the flag.
 *
 *          WHY a sim system (not OnEntitySpawned): the snap needs USeinNavigation +
 *          the USeinMovement snap logic, so it must live in the Movement layer
 *          (dep points Movement -> CoreEntity, never back). Running in the sim tick
 *          (a) guarantees nav is loaded — gated on HasRuntimeData(), so it harmlessly
 *          retries on later ticks if the bake isn't up yet at spawn — and (b) respects
 *          USeinWorldSubsystem::OnEntitySpawned's "do not mutate sim state" contract.
 *
 *          DETERMINISM: all fixed-point; one write per entity; the done-flag lives on
 *          the (hashed, serialized) FSeinMovementComponent so snapshot/replay stay
 *          consistent. The snap runs on the movement class CDO — it is const and routes
 *          through the virtual QueryReferenceZ / GetAltitude, so no per-instance state
 *          is needed and there is no per-entity NewObject churn.
 *
 * Phase: PreTick | Priority: 4  (before the collision broadphase rebuild at 5 and
 *        avoidance at 6 — snap a freshly-spawned unit before anything reads it).
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinMovementComponent.h"
#include "Movement/SeinMovement.h"
#include "Movement/SeinBasicMovement.h"
#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"
#include "Types/Entity.h"
#include "Engine/World.h"

class FSeinInitialSnapSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		// Resolve the active nav once; bail (and retry next tick) until the bake
		// is loaded — level-placed units can spawn before the nav subsystem loads.
		USeinNavigation* Nav = nullptr;
		if (UWorld* UnrealWorld = World.GetWorld())
		{
			if (USeinNavigationSubsystem* NavSub = UnrealWorld->GetSubsystem<USeinNavigationSubsystem>())
			{
				Nav = NavSub->GetNavigation();
			}
		}
		if (!Nav || !Nav->HasRuntimeData()) return;

		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			FSeinMovementComponent* Move = World.GetComponent<FSeinMovementComponent>(Handle);
			if (!Move || Move->bInitialGroundSnapDone) return;

			// Resolve the entity's movement class (soft path); fall back to Basic.
			UClass* MoveClass = Move->MovementClass.IsValid()
				? Move->MovementClass.TryLoadClass<USeinMovement>()
				: nullptr;
			if (!MoveClass || MoveClass->HasAnyClassFlags(CLASS_Abstract))
			{
				MoveClass = USeinBasicMovement::StaticClass();
			}

			// Snap via the class CDO — const + no per-instance state, so no alloc.
			if (const USeinMovement* MoveCDO = MoveClass->GetDefaultObject<USeinMovement>())
			{
				MoveCDO->SnapToGroundImmediate(Entity, *Move, Nav);
			}
			// Latch once per entity even if class resolution failed, so a bad
			// MovementClass can't make this retry forever.
			Move->bInitialGroundSnapDone = true;
		});
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PreTick; }
	virtual int32 GetPriority() const override { return 4; }
	virtual FName GetSystemName() const override { return TEXT("InitialGroundSnap"); }
};

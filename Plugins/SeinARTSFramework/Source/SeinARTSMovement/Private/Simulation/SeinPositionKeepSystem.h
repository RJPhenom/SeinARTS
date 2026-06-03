/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPositionKeepSystem.h
 * @brief   Passive re-seek: keeps idle units on their DesiredPosition.
 *
 *          Every unit that has executed a move carries a DesiredPosition ("home")
 *          on its FSeinMovementComponent — claimed by USeinMoveToAction on the
 *          move's first tick (formation slot, cover slot, or a raw move target).
 *          After penetration resolution or a shove bumps an idle unit off that
 *          home, this system re-issues a real pathed MoveTo back to it, so units
 *          settle back into formation / cover instead of staying scattered.
 *
 *          "Newest move wins": a fresh player order (or another re-seek) overwrites
 *          DesiredPosition with its own destination; the in-flight action detects
 *          the mismatch and bows out (see USeinMoveToAction::TickAction). So the
 *          re-seek never fights a live order — no tug-of-war.
 *
 *          Phase PostTick / priority 60 — after movement (AbilityExecution) has
 *          advanced positions and after squad(30)/broker(40) housekeeping, so it
 *          reacts to this frame's settled positions.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Components/SeinMovementComponent.h"
#include "Actions/SeinMoveToAction.h"
#include "Types/Entity.h"
#include "Types/Vector.h"
#include "Types/FixedPoint.h"

/** See file header. Header-only like FSeinSquadSystem — instantiated and
 *  registered by USeinMovementSubsystem on world begin-play. */
class FSeinPositionKeepSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		USeinLatentActionManager* LAM = World.LatentActionManager;
		if (!LAM) { return; }

		// Re-seek only once a unit has drifted more than this off its home. Set
		// comfortably above the ~footprint nudge penetration resolution applies,
		// so routine jostling between neighbours doesn't trigger a constant
		// re-path storm — only a genuine displacement does. (Tunable; ~1.5m.)
		const FFixedPoint ReseekThresholdSq =
			FFixedPoint::FromInt(150) * FFixedPoint::FromInt(150);

		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			const FSeinMovementComponent* Move = World.GetComponent<FSeinMovementComponent>(Handle);
			// Opt-in (infantry-oriented) + must have an established home. The
			// bMaintainPosition gate is first + cheapest, so non-opted units
			// (vehicles, anything that shouldn't auto-return) cost one bool read.
			if (!Move || !Move->bMaintainPosition || !Move->bHasDesiredPosition) { return; }

			// Idle only — never fight an active order (player move or an in-flight
			// re-seek). This also stops us from stacking a second re-seek on top of
			// one we issued last frame: the registered action keeps the entity busy.
			if (LAM->HasActiveActionForEntity(Handle)) { return; }

			// Planar displacement from home (ignore Z — units share a ground plane;
			// vertical offset is render-only and shouldn't provoke a re-path).
			const FFixedVector Home = Move->DesiredPosition;
			FFixedVector Delta = Entity.Transform.GetLocation() - Home;
			Delta.Z = FFixedPoint::Zero;
			if (Delta.SizeSquared() <= ReseekThresholdSq) { return; }

			// Drifted off home while idle — walk back along a fresh path. No owning
			// ability (system-initiated) and no observer (nothing in BP is awaiting
			// it). The action re-claims Home as DesiredPosition on its first tick,
			// which is a no-op here (Home already equals DesiredPosition).
			USeinMoveToAction* Action = NewObject<USeinMoveToAction>(&World);
			Action->OwnerEntity = Handle;
			Action->Initialize(Home);
			LAM->RegisterAction(Action);
		});
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PostTick; }
	virtual int32 GetPriority() const override { return 60; }
	virtual FName GetSystemName() const override { return TEXT("PositionKeep"); }
};

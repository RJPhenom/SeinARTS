/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWar.cpp
 */

#include "SeinFogOfWar.h"
#include "SeinFogOfWarTypes.h"
#include "Components/SeinVisionComponent.h"
#include "Components/SeinFogVisibilityComponent.h"
#include "Components/SeinExtentsComponent.h"

#include "Actor/SeinActor.h"
#include "Actor/SeinEntityComponent.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Types/Entity.h"

#include "Engine/World.h"

uint8 USeinFogOfWar::GetEntityVisibleBits(FSeinPlayerID Observer,
	USeinWorldSubsystem& Sim, FSeinEntityHandle Target) const
{
	// Single-point fallback. Subclasses override to do the volumetric
	// (extents-aware) sweep.
	const FSeinEntity* Entity = Sim.GetEntity(Target);
	if (!Entity) return 0;
	return GetCellBitfield(Observer, Entity->Transform.GetLocation());
}

bool USeinFogOfWar::IsEntityVisibleToObserver(FSeinPlayerID Observer,
	USeinWorldSubsystem& Sim, FSeinEntityHandle Target) const
{
	// Caller hasn't specified an observer → permissive (no filtering).
	// Lets cover queries and other consumers opt out cleanly when running
	// in contexts without a meaningful "viewing player" (replay scrubs,
	// AI evaluating cover, combat scripts that need the full picture).
	if (!Observer.IsValid()) return true;

	// Owner-sees-own short-circuit: the player who owns the entity always
	// sees it regardless of fog. Covers self-deployed cover, units, and
	// the common "my own buildings" case without a bitfield lookup.
	if (Sim.GetEntityOwner(Target) == Observer) return true;

	// Resolve the entity's FogVisibilityPolicy + FogVisibilityLayerMask
	// from FSeinFogVisibilityComponent (a top-level component since the
	// Phase-5+ split). Defaults apply when not authored — VisionLayersOnly
	// policy + Normal-bit emission match the historic implicit-defaults
	// behaviour for entities that previously had no extents.
	ESeinFogVisibilityPolicy Policy = ESeinFogVisibilityPolicy::VisionLayersOnly;
	uint8 EmissionMask = SEIN_FOW_BIT_NORMAL;
	if (const FSeinFogVisibilityComponent* FogVis = Sim.GetComponent<FSeinFogVisibilityComponent>(Target))
	{
		Policy = FogVis->FogVisibilityPolicy;
		EmissionMask = FogVis->FogVisibilityLayerMask;
	}

	if (Policy == ESeinFogVisibilityPolicy::AlwaysVisible) return true;

	// VisibleOnceExplored widens the mask to include the sticky per-cell
	// Explored bit — once that bit is set anywhere in the footprint, the
	// entity is permanently visible (terrain-scouted ghost reveal). Note this
	// fires even for entities that arrived AFTER the cell was explored;
	// VisibleOnceSeen (below) is the per-entity alternative that doesn't.
	if (Policy == ESeinFogVisibilityPolicy::VisibleOnceExplored)
	{
		EmissionMask |= SEIN_FOW_BIT_EXPLORED;
	}
	if (EmissionMask == 0) return false;     // entity configured as never-visible

	// Currently spotted? A matching live emission bit anywhere in the
	// footprint means visible right now — for every policy that got this far.
	const uint8 ObserverBits = GetEntityVisibleBits(Observer, Sim, Target);
	if ((ObserverBits & EmissionMask) != 0) return true;

	// VisibleOnceSeen: not spotted this instant, but stays revealed as a ghost
	// if this observer has ever had live vision of the entity ITSELF. The
	// latch is maintained deterministically each fog tick (HasObserverSeenEntity);
	// unlike VisibleOnceExplored it never reveals on terrain-scouting alone, so
	// a thing that appears in explored-but-unseen fog stays hidden until seen.
	if (Policy == ESeinFogVisibilityPolicy::VisibleOnceSeen)
	{
		return HasObserverSeenEntity(Observer, Target);
	}

	return false;
}

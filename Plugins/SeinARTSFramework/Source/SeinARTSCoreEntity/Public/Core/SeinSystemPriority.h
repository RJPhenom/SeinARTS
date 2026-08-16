/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSystemPriority.h
 * @brief   Named tick priorities for ISeinSystems, grouped by ESeinTickPhase.
 *
 *          Within a phase, systems tick in ASCENDING priority (lower = earlier;
 *          ties broken by canonical stable system ID for cross-client
 *          determinism). These are the slots the shipped systems occupy —
 *          when authoring a custom ISeinSystem,
 *          reference one of these (or pick a gap between them) so your registration
 *          order is intentional instead of a copied magic number.
 *
 *          These constants ARE the source of truth: every shipped system's
 *          DescribeSystem().Priority uses one of them, so changing a slot here changes
 *          the live tick order. Keep them ordered as they tick within a phase.
 */

#pragma once

#include "CoreMinimal.h"

namespace SeinSystemPriority
{
	// ── PreTick ──
	inline constexpr int32 EffectTick          = 0;
	inline constexpr int32 CollisionBroadphase = 5;
	inline constexpr int32 Avoidance           = 6;
	inline constexpr int32 NavBlockerStamp     = 7;
	inline constexpr int32 CooldownTick        = 10;
	inline constexpr int32 WeaponCycle         = 11;

	// ── AbilityExecution ──
	inline constexpr int32 AbilityTick      = 0;
	inline constexpr int32 MovementDriver   = 10;
	inline constexpr int32 ProjectileFlight = 20;
	inline constexpr int32 Production       = 50;

	// PostTick
	inline constexpr int32 Lifespan            = -10;
	inline constexpr int32 CollisionResolution = 10;
	inline constexpr int32 NavContainment      = 11;
	inline constexpr int32 Squad               = 30;
	inline constexpr int32 CommandBroker       = 40;
	inline constexpr int32 FogOfWar            = 80;

	// FinalObservation (stateless, non-canonical writes only)
	inline constexpr int32 MovementPresentation = 0;
	inline constexpr int32 MovementTrace        = 10;
}

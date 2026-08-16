/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinCombatDamage.h
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @brief        Deterministic damage application service.
 *
 *          The single sanctioned path from "a payload hit an entity" to
 *          "vitals changed / entity died": resolve the payload's formula
 *          policy, clamp, subtract, emit the damage/death/kill visual events,
 *          and route death through ordinary deferred teardown. Splash gathers
 *          its victims in canonical entity order so multi-victim application
 *          is bit-identical on every peer.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Combat/SeinCombatTypes.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"

class USeinWorldSubsystem;

class SEINARTSCOMBAT_API FSeinCombatDamage
{
public:
	/** Apply one payload to one target. Returns the damage actually dealt
	 *  (zero for invulnerable/missing/dead vitals). Sim-authorized callers
	 *  only — the restricted BPFL front door enforces the guard for
	 *  Blueprint. */
	static FFixedPoint ApplyDamage(
		USeinWorldSubsystem& World,
		FSeinEntityHandle Target,
		FSeinEntityHandle Instigator,
		const FSeinDamagePayload& Payload,
		FFixedPoint DistanceFromImpact = FFixedPoint::Zero);

	/** Resolve a payload at a world point: single-target when the payload has
	 *  no area, otherwise every vitals-bearing entity within AreaRadius in
	 *  canonical order, each with its own impact distance. DirectTarget (when
	 *  valid) is always evaluated at distance zero. Returns victims damaged. */
	static int32 ResolveImpact(
		USeinWorldSubsystem& World,
		const FFixedVector& ImpactPoint,
		FSeinEntityHandle DirectTarget,
		FSeinEntityHandle Instigator,
		const FSeinDamagePayload& Payload);

private:
	FSeinCombatDamage() = delete;
};

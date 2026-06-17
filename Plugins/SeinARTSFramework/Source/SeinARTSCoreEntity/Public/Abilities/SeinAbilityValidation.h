/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAbilityValidation.h
 * @brief   Declarative target-validation helper per DESIGN §7 Q5c + Q6.
 *          Evaluates range / ValidTargetTags / LOS before CanActivate.
 *
 *          Pure sim-side utility — consumed by ProcessCommands::ActivateAbility.
 *          Returns a distinguishing failure reason so the caller can emit the
 *          right CommandRejected visual event + FSeinAbilityAvailability reason.
 */

#pragma once

#include "CoreMinimal.h"
#include "Abilities/SeinAbilityTypes.h"
#include "Core/SeinEntityHandle.h"
#include "Types/Vector.h"

class USeinAbility;
class USeinWorldSubsystem;

/** Outcome of `FSeinAbilityValidation::ValidateTarget`. */
enum class ESeinAbilityTargetValidationResult : uint8
{
	Valid,
	OutOfRange,
	InvalidTarget,		// target fails ValidTargetTags query
	NoLineOfSight
};

/**
 * Declarative target validation. Run before USeinAbility::CanActivate so the
 * BP-level escape hatch only sees targets that already pass range / tag / LOS.
 *
 * `bRequiresLineOfSight` consults USeinWorldSubsystem::LineOfSightResolver
 * (bound by USeinFogOfWarSubsystem); permissive when unbound (tests / fog-less).
 */
struct SEINARTSCOREENTITY_API FSeinAbilityValidation
{
	static ESeinAbilityTargetValidationResult ValidateTarget(
		const USeinAbility& Ability,
		FSeinEntityHandle Owner,
		FSeinEntityHandle Target,
		const FFixedVector& Location,
		USeinWorldSubsystem& World);
};

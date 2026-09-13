/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinPlacementValidation.h
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Shared placement admission for previews and simulation commands.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#pragma once

#include "CoreMinimal.h"
#include "Abilities/SeinTargeterTypes.h"

class USeinWorldSubsystem;
class USeinAbility;
class USeinPointFacingTargeterSpec;

namespace SeinPlacementValidation
{
	/** Resolve the authoritative footprint source, including legacy authoring. */
	SEINARTSCOREENTITY_API TSoftClassPtr<AActor> ResolveActorClass(const USeinAbility& Ability);
	/** Validate a footprint class independently of the input gesture. */
	SEINARTSCOREENTITY_API bool IsValidClass(const USeinWorldSubsystem& World,
		TSoftClassPtr<AActor> ActorClass, const FFixedVector& Location, FFixedPoint Yaw);
	/** Check every authored footprint against baked navigation and current simulation blockers.
	 * Missing building/extents fail closed. No navigation resolver skips only the baked check. */
	SEINARTSCOREENTITY_API bool IsValid(const USeinWorldSubsystem& World,
		const USeinPointFacingTargeterSpec* Spec, const FFixedVector& Location, FFixedPoint Yaw);

	/** Apply the ability's Requires Free Footprint gate to every captured placement. */
	SEINARTSCOREENTITY_API bool IsValidForAbility(const USeinWorldSubsystem& World,
		const USeinAbility& Ability, const TArray<FSeinTargeterPoint>& Points);
}

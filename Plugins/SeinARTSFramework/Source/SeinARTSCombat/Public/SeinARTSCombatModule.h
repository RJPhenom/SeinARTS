/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinARTSCombatModule.h
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @latest       23 Aug 2026
 * @brief        Module interface for the SeinARTS combat toolkit.
 *
 *          The combat module owns ONLY the genre-free acquisition mechanism:
 *          the deterministic target query (range / arc / tag / fog-LoS /
 *          component gates over a derived spatial index), the per-target
 *          Check Target verdict, the Blueprint scorer policy seam, and the
 *          presentation notifications (damage / heal / death visual events).
 *          It ships NO vitals, weapon, damage, or projectile schema and no
 *          tick systems: what a unit's stats are, how a hit is computed, how
 *          fast a weapon cycles, what a projectile is, and when something dies
 *          are the consuming game's components (native or UDS), abilities, and
 *          effects — mutated through the generic Apply Field Delta / Apply
 *          Effect / Destroy Entity verbs. The module never decides which kind
 *          of RTS is being made.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class SEINARTSCOMBAT_API FSeinARTSCombatModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void PreUnloadCallback() override;
	virtual void ShutdownModule() override;
};

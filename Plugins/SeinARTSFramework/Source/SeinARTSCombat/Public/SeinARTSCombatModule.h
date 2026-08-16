/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinARTSCombatModule.h
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @brief        Module interface for the SeinARTS combat substrate.
 *
 *          The combat module owns the genre-free combat MECHANISMS: vitals and
 *          deterministic damage resolution, weapon cycling timers, the target
 *          query service, and instant/projectile delivery. Everything that
 *          defines a game's combat FEEL — damage formulas, target scoring,
 *          engagement stances, suppression, morale, shields — is a pluggable
 *          policy class, an ability, or an effect authored by the consuming
 *          game. The module never decides which kind of RTS is being made.
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
	virtual void ShutdownModule() override;
};

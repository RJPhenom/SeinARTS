/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementTuningExport.h
 * @brief   Mirrors a movement-mode Blueprint's tuning variables into a paired
 *          UserDefinedStruct ("<BPName>TuningData") and stamps that UDS onto the BP's
 *          CDO `TuningStruct` so it auto-fills `FSeinMovementPayload::MovementClassData`.
 *
 *          This is the "export BP variables -> UDS" automation. It is user-triggered
 *          (the Class-Defaults "Sync Tuning Struct" button) — NOT run during compile —
 *          so it can never reenter the BP/UDS compiler. Decoupled from the Movement
 *          module: the base class is resolved by path, the CDO property by reflection,
 *          so SeinARTSEditor keeps no link dependency on SeinARTSMovement.
 */

#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UUserDefinedStruct;

namespace SeinMovementTuning
{
	/** True if `Blueprint`'s generated class derives from `USeinMovement` (resolved by
	 *  path — no link dependency on the Movement module). False for non-BP / non-movement. */
	bool IsMovementModeBlueprint(const UBlueprint* Blueprint);

	/** Sync `Blueprint`'s tuning variables into its paired tuning UDS and link it.
	 *  Tuning vars = Instance-Editable, deterministic-typed `NewVariables`
	 *  (per SeinDeterminism::IsPinTypeDeterministic). Get-or-creates the UDS next to the
	 *  BP, adds/removes/retypes its fields to match (keyed by name), then stamps it onto
	 *  the CDO `TuningStruct`. With zero tuning vars, clears the link and makes no UDS.
	 *  Returns the linked UDS (or null). Editor-only; safe to call any time outside compile. */
	UUserDefinedStruct* SyncTuningStructForBlueprint(UBlueprint* Blueprint);
}

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBasicMatchSettings.h
 * @brief   Designer-convenience starter struct for common RTS match knobs.
 *
 * IMPORTANT: framework C++ does NOT reference this type. It exists purely
 * as a designer convenience — a pre-defined `FInstancedStruct` payload
 * shape that covers the most common RTS match-level knobs. Designers add
 * an instance to `FSeinMatchSettings::Extensions` and read it from BP
 * scripts via `FindMatchExtension<FSeinBasicMatchSettings>` (or the BPFL
 * wrapper `SeinFindBasicMatchSettings`).
 *
 * Per-field designer responsibility:
 *   - `bFriendlyFire`     — designer's ability scripts read this and skip
 *                           `SeinApplyDamage` calls when allied.
 *   - `bResourceSharing`  — designer's resource API treats `Permission.
 *                           ResourceShare`-paired players as a shared pool.
 *   - `bAllowSpectators`  — designer's lobby UI exposes the Spectator slot
 *                           state when true.
 *   - `TimeLimit`         — designer's victory-condition system reads this.
 *                           Framework match-flow doesn't enforce.
 *
 * Games with completely different rule sets ignore this struct and ship
 * their own — the `Extensions` array accepts any USTRUCT.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinBasicMatchSettings.generated.h"

/**
 * Common-RTS match-rule knobs. Opt-in: designer adds this to
 * `FSeinMatchSettings::Extensions`; framework code never reads it.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic, DisplayName = "Sein Basic Match Settings"))
struct SEINARTSCOREENTITY_API FSeinBasicMatchSettings
{
	GENERATED_BODY()

	/** When true, allied units can damage each other. When false, designer's
	 *  ability code is expected to skip `SeinApplyDamage` for allied targets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Match")
	bool bFriendlyFire = false;

	/** When true, allied players share a resource pool (designer's resource
	 *  API decides exact aliasing). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Match")
	bool bResourceSharing = false;

	/** When true, the lobby UI exposes a Spectator slot type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Match")
	bool bAllowSpectators = true;

	/** Match time limit in minutes. 0 = no limit. Framework doesn't enforce
	 *  — designer's victory-condition system / scenario script reads this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Match",
		meta = (ClampMin = "0"))
	int32 TimeLimit = 0;
};

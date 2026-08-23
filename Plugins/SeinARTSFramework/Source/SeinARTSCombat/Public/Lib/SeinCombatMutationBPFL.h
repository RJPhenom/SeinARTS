/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinCombatMutationBPFL.h
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @latest       23 Aug 2026
 * @brief        Restricted combat notifications for ability/effect graphs —
 *               the sim → render bridge for designer-owned combat.
 *
 *          The framework does not resolve damage: the designer's ability reads
 *          its own weapon struct, runs its own formula, and writes its own
 *          vitals field with `Apply Field Delta`. What the framework DOES own
 *          is the presentation vocabulary those outcomes flow through —
 *          `FSeinVisualEvent` DamageApplied / HealApplied / Death / Kill, which
 *          `ASeinActor` routes to On Damage Applied / On Heal Applied /
 *          On Death and UI consumes for floating text, kill feeds, and death
 *          animation. These nodes enqueue exactly those events and nothing
 *          else; they never touch canonical state. Destroying the entity is a
 *          separate, explicit `Destroy Entity` call — death is the designer's
 *          rule, not a side effect here.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Types/FixedPoint.h"
#include "SeinCombatMutationBPFL.generated.h"

UCLASS(meta = (DisplayName = "SeinARTS Combat Mutation Library", RestrictedToClasses = "SeinAbility,SeinEffect"))
class SEINARTSCOMBAT_API USeinCombatMutationBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Route a resolved hit to presentation: enqueues the DamageApplied
	 *  visual event (Target receives On Damage Applied with Amount, Source,
	 *  and your damage-type tag). Call it AFTER the stat write — typically
	 *  right after `Apply Field Delta` on your vitals struct. Source may be
	 *  invalid for environmental damage. Returns false (with a warning) when
	 *  the sim rejects the call: no world, unauthorized timing, or a Target
	 *  that is no longer alive — notify BEFORE you Destroy Entity. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Notify Damage Applied"))
	static bool SeinNotifyDamageApplied(
		const UObject* WorldContextObject,
		FSeinEntityHandle Target,
		FSeinEntityHandle Source,
		FFixedPoint Amount,
		FGameplayTag DamageType);

	/** Route a resolved heal to presentation: enqueues the HealApplied
	 *  visual event (Target receives On Heal Applied). Same contract as
	 *  Notify Damage Applied. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Notify Heal Applied"))
	static bool SeinNotifyHealApplied(
		const UObject* WorldContextObject,
		FSeinEntityHandle Target,
		FSeinEntityHandle Source,
		FFixedPoint Amount,
		FGameplayTag HealType);

	/** Route a death decided by YOUR rule to presentation: enqueues the Death
	 *  visual event for Dying (On Death → death animation routing) and, when
	 *  Killer is a live entity, the matching Kill event for kill-feed /
	 *  scoreboard UI attributed to Killer's owner. Does NOT destroy the
	 *  entity — follow with `Destroy Entity` (or keep it around as a wreck,
	 *  downed model, etc.). ORDER MATTERS: call this while Dying is still
	 *  alive, i.e. before `Destroy Entity`; a dead Dying is refused with a
	 *  warning. An invalid or already-destroyed Killer still produces the Death
	 *  event, just no Kill attribution. Returns false when the sim rejects the
	 *  call. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Notify Death"))
	static bool SeinNotifyDeath(
		const UObject* WorldContextObject,
		FSeinEntityHandle Dying,
		FSeinEntityHandle Killer);
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinCombatBPFL.h
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @brief        Read-side combat queries for any Blueprint graph.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Combat/SeinCombatTypes.h"
#include "Components/SeinVitalsComponent.h"
#include "Core/SeinEntityHandle.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SeinCombatBPFL.generated.h"

UCLASS(meta = (DisplayName = "SeinARTS Combat Library"))
class SEINARTSCOMBAT_API USeinCombatBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Run one deterministic acquisition query (see FSeinTargetQuery). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Find Targets"))
	static TArray<FSeinTargetCandidate> SeinFindTargets(
		const UObject* WorldContextObject, const FSeinTargetQuery& Query);

	/** The entity's vitals; Found=false when it carries none. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Vitals"))
	static FSeinVitalsComponent SeinGetVitals(
		const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle,
		bool& bFound);

	/** Timer/magazine readiness of one weapon slot (no legality checks). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Is Weapon Ready"))
	static bool SeinIsWeaponReady(
		const UObject* WorldContextObject,
		FSeinEntityHandle EntityHandle,
		int32 WeaponIndex);
};

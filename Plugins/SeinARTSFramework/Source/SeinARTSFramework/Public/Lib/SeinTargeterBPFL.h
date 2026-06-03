/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTargeterBPFL.h
 * @brief   Action-slot dispatcher BPFL — the seam between the action-panel UI
 *          and the targeter subsystem.
 *
 *          Action-panel widgets call TriggerAbilityFromActionSlot(PC, AbilityTag)
 *          when the player clicks an ability button or presses its hotkey. The
 *          BPFL reads the ability's TargetType + TargeterSpec and either:
 *            - fires immediately (None / Self / Passive — no targeting needed),
 *              by calling PC->IssueTargetedAbility with empty Points;
 *            - activates the targeter subsystem (Entity / Point / Area), which
 *              then submits the command itself once the user finishes placing.
 *
 *          Living in the framework module so it can call into the targeter
 *          subsystem (which itself lives here, not in CoreEntity, because it
 *          needs ASeinPlayerController and AActor-based preview classes).
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Types/Vector.h"
#include "Types/Rotator.h"
#include "Types/Transform.h"
#include "Abilities/SeinTargeterTypes.h"
#include "SeinTargeterBPFL.generated.h"

class ASeinPlayerController;

UCLASS()
class SEINARTSFRAMEWORK_API USeinTargeterBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Dispatch a player-triggered ability invocation from the action panel.
	 *
	 * Decides between immediate fire and targeter activation based on the
	 * ability's declared TargetType + TargeterSpec. The player's currently-
	 * focused or first-selected entity is used as the leader (the entity whose
	 * ability data we resolve against).
	 *
	 * No-ops if PC is null, no entity is selected, or the ability is not
	 * granted to any selected entity.
	 *
	 * @param PC          The local player's SeinPlayerController.
	 * @param AbilityTag  Tag of the ability to invoke (must match an entity's
	 *                    granted ability instance).
	 *
	 * @return True if the dispatch was accepted (either immediate-fired or
	 *         the targeter was activated); false on any precondition failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Targeter",
		meta = (DisplayName = "Trigger Ability From Action Slot"))
	static bool TriggerAbilityFromActionSlot(ASeinPlayerController* PC, FGameplayTag AbilityTag);

	/**
	 * Compose a captured TargeterPoint into a ready-to-use FFixedTransform:
	 * Location → Translation, YawDegrees → Yaw rotation, Scale 1.
	 *
	 * Returns the deterministic fixed-point type by design — the framework
	 * never converts up to float-based FTransform unless absolutely necessary,
	 * because the float→fixed direction is lossy and propagates determinism
	 * holes. Fall through to FTransform only at the UE-API boundary (Spawn
	 * Actor From Class, etc.) using FFixedTransform's ToTransform conversion.
	 *
	 * Designer's typical OnActivate spawn graph:
	 *   Get Targeter Point Transform → ToTransform → Spawn Actor From Class
	 */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Targeter",
		meta = (DisplayName = "Get Targeter Point Transform"))
	static FFixedTransform GetTargeterPointTransform(const FSeinTargeterPoint& Point);

	/**
	 * Just the yaw rotation as an FFixedRotator (Pitch=0, Roll=0). For sim-side
	 * composition with other rotations, or designer convenience when they only
	 * need rotation (orient a one-shot effect, etc.). Convert to FRotator with
	 * ToRotator at the UE-API boundary if needed.
	 */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Targeter",
		meta = (DisplayName = "Get Targeter Point Rotation"))
	static FFixedRotator GetTargeterPointRotation(const FSeinTargeterPoint& Point);

	/**
	 * Just the world-space location as an FFixedVector. Direct passthrough of
	 * Point.Location — saved as a named accessor so designers don't have to
	 * "break struct" to get at it for cases where they only need the location.
	 */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Targeter",
		meta = (DisplayName = "Get Targeter Point Location"))
	static FFixedVector GetTargeterPointLocation(const FSeinTargeterPoint& Point);
};

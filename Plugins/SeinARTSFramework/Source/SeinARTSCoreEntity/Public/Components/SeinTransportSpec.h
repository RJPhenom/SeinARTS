/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTransportSpec.h
 * @brief   Transport specialization spec (DESIGN §14). Optional — layer on
 *          `FSeinContainmentData` when you want load/unload timing hints
 *          + a deploy offset for exit spawn placement.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Components/SeinComponent.h"
#include "SeinTransportSpec.generated.h"

/**
 * Per-container transport metadata. Framework does not itself drive
 * load/unload timing — designer abilities (`Ability.Enter` / `Ability.Exit`
 * BP subclasses) read these as timing hints. `DeployOffset` is applied in
 * container-local space and used by `USeinWorldSubsystem::ExitContainer`
 * to place exiters when a dedicated exit target isn't provided.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinTransportSpec : public FSeinComponent
{
	GENERATED_BODY()

	/** Sim-seconds to load each entity. Designer reads from entry-ability graphs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Transport",
		meta = (ClampMin = "0.0"))
	FFixedPoint LoadDuration;

	/** Sim-seconds to unload each entity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Transport",
		meta = (ClampMin = "0.0"))
	FFixedPoint UnloadDuration;

	/** Offset from the container's transform at which exiters materialize
	 *  (rotates with the container's facing). Zero puts them at the container. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Transport")
	FFixedVector DeployOffset;
};

FORCEINLINE uint32 GetTypeHash(const FSeinTransportSpec& Spec)
{
	uint32 Hash = GetTypeHash(Spec.LoadDuration);
	Hash = HashCombine(Hash, GetTypeHash(Spec.UnloadDuration));
	Hash = HashCombine(Hash, GetTypeHash(Spec.DeployOffset));
	return Hash;
}

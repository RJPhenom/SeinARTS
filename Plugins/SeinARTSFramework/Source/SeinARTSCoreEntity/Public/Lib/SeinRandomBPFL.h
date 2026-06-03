/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:   SeinRandomBPFL.h
 * @brief:  Deterministic random primitives backed by the world subsystem's
 *          framework-owned PRNG (FFixedRandom / Xorshift128+). Replay-
 *          identical across all clients. Designers compose any higher-level
 *          random behavior (accuracy rolls, loot tables, AI variance, ...)
 *          from these.
 *
 *          Every call advances the shared PRNG stream — call order MUST be
 *          deterministic across clients (don't branch a random call on a
 *          client-only condition). Designers who want an isolated random
 *          stream (e.g. per-weapon RNG that won't interleave with framework
 *          rolls) can hold their own `FFixedRandom` in a sim component and
 *          call its methods directly from C++; this BPFL exposes only the
 *          shared subsystem stream.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Types/FixedPoint.h"
#include "SeinRandomBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Random Library"))
class SEINARTSCOREENTITY_API USeinRandomBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** True or false with 50/50 probability. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Random",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Random Bool"))
	static bool SeinRandomBool(const UObject* WorldContextObject);

	/** Returns true with the given probability. `Probability` clamped to [0,1] —
	 *  values ≤ 0 always return false (PRNG not advanced); values ≥ 1 always
	 *  return true (PRNG not advanced). Use for accuracy rolls, weighted-coin
	 *  checks, etc. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Random",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Random Bool With Probability"))
	static bool SeinRandomBoolWithProbability(const UObject* WorldContextObject, FFixedPoint Probability);

	/** Uniform fixed-point in [0, 1]. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Random",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Random Fixed Point"))
	static FFixedPoint SeinRandomFixedPoint(const UObject* WorldContextObject);

	/** Uniform fixed-point in [Min, Max]. If Min >= Max, returns Min and does
	 *  not advance the PRNG. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Random",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Random Fixed Point Range"))
	static FFixedPoint SeinRandomFixedPointRange(const UObject* WorldContextObject, FFixedPoint Min, FFixedPoint Max);

	/** Uniform integer in [Min, Max] (inclusive). If Min >= Max, returns Min and
	 *  does not advance the PRNG. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Random",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Random Int Range"))
	static int32 SeinRandomIntRange(const UObject* WorldContextObject, int32 Min, int32 Max);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};

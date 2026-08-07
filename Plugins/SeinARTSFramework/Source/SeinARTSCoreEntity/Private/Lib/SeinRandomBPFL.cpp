/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinRandomBPFL.cpp
 */

#include "Lib/SeinRandomBPFL.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Simulation/SeinWorldSubsystem.h"

USeinWorldSubsystem* USeinRandomBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

bool USeinRandomBPFL::SeinRandomBool(const UObject* WorldContextObject)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem
		|| !Subsystem->RequireStateMutationAuthorization(TEXT("RandomBool")))
	{
		return false;
	}
	return Subsystem->SimRandom.Bool();
}

bool USeinRandomBPFL::SeinRandomBoolWithProbability(const UObject* WorldContextObject, FFixedPoint Probability)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	// Early-out at the extremes so deterministic / boundary calls don't advance the PRNG stream.
	if (Probability <= FFixedPoint::Zero) return false;
	if (Probability >= FFixedPoint::One) return true;
	if (!Subsystem->RequireStateMutationAuthorization(
		TEXT("RandomBoolWithProbability")))
	{
		return false;
	}
	return Subsystem->SimRandom.Bool(Probability);
}

FFixedPoint USeinRandomBPFL::SeinRandomFixedPoint(const UObject* WorldContextObject)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem
		|| !Subsystem->RequireStateMutationAuthorization(TEXT("RandomFixedPoint")))
	{
		return FFixedPoint::Zero;
	}
	return Subsystem->SimRandom.FixedPoint();
}

FFixedPoint USeinRandomBPFL::SeinRandomFixedPointRange(const UObject* WorldContextObject, FFixedPoint Min, FFixedPoint Max)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return Min;

	// `Range` itself early-outs on Min >= Max (returns Min without advancing). Match that here.
	if (Min >= Max) return Min;
	if (!Subsystem->RequireStateMutationAuthorization(TEXT("RandomFixedPointRange")))
	{
		return Min;
	}
	return Subsystem->SimRandom.Range(Min, Max);
}

int32 USeinRandomBPFL::SeinRandomIntRange(const UObject* WorldContextObject, int32 Min, int32 Max)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return Min;

	if (Min >= Max) return Min;
	if (!Subsystem->RequireStateMutationAuthorization(TEXT("RandomIntRange")))
	{
		return Min;
	}
	return Subsystem->SimRandom.IntRange(Min, Max);
}

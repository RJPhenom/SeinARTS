/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		Time.h
 * @date:		1/17/2026
 * @author:		RJ Macklem
 * @brief:		Deterministic simulation time using tick counters.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "FixedPoint.h"
#include "Time.generated.h"

/**
 * Simulation time using deterministic tick counters.
 * Uses integer tick counts instead of floating-point time for perfect determinism.
 * Time is derived from ticks * (1 / tick_rate).
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCORE_API FFixedTime
{
	GENERATED_BODY()

	/** Current tick count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int64 Tick;

	/** Ticks per second (tick rate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 TickRate;

	// Constructors
	// ====================================================================================================

	FORCEINLINE FFixedTime()
		: Tick(0)
		, TickRate(20) // Default 20 Hz
	{}

	FORCEINLINE FFixedTime(int64 InTick, int32 InTickRate)
		: Tick(InTick)
		, TickRate(InTickRate)
	{}

	/** Creates simulation time from real time (seconds). */
	FORCEINLINE static FFixedTime FromSeconds(FFixedPoint Seconds, int32 TickRate = 30)
	{
		// Ticks = Seconds * TickRate
		FFixedPoint TicksFloat = Seconds * FFixedPoint::FromInt(TickRate);
		int64 Ticks = TicksFloat.ToInt64();
		return FFixedTime(Ticks, TickRate);
	}

	/** Creates simulation time from milliseconds. */
	FORCEINLINE static FFixedTime FromMilliseconds(FFixedPoint Milliseconds, int32 TickRate = 30)
	{
		return FromSeconds(Milliseconds / FFixedPoint::FromInt(1000), TickRate);
	}

	/** Creates simulation time from ticks. */
	FORCEINLINE static FFixedTime FromTicks(int64 Ticks, int32 TickRate = 30)
	{
		return FFixedTime(Ticks, TickRate);
	}

	/** Creates simulation time representing zero time. */
	FORCEINLINE static FFixedTime Zero(int32 TickRate = 30)
	{
		return FFixedTime(0, TickRate);
	}

	// Queries
	// ====================================================================================================

	/** Returns the time in seconds. */
	FORCEINLINE FFixedPoint ToSeconds() const
	{
		// Seconds = Tick / TickRate
		return FFixedPoint::FromInt64(Tick) / FFixedPoint::FromInt(TickRate);
	}

	/** Returns the time in milliseconds. */
	FORCEINLINE FFixedPoint ToMilliseconds() const
	{
		return ToSeconds() * FFixedPoint::FromInt(1000);
	}

	/** Returns the fixed delta time for one tick. */
	FORCEINLINE FFixedPoint GetDeltaTime() const
	{
		// Delta = 1 / TickRate
		return FFixedPoint::One / FFixedPoint::FromInt(TickRate);
	}

	/** Returns true if this is zero time. */
	FORCEINLINE bool IsZero() const
	{
		return Tick == 0;
	}

	// Modification
	// ====================================================================================================

	/** Advances time by one tick. */
	FORCEINLINE FFixedTime& AdvanceTick()
	{
		++Tick;
		return *this;
	}

	/** Advances time by a number of ticks. */
	FORCEINLINE FFixedTime& AdvanceTicks(int64 NumTicks)
	{
		Tick += NumTicks;
		return *this;
	}

	/** Resets the time to zero. */
	FORCEINLINE FFixedTime& Reset()
	{
		Tick = 0;
		return *this;
	}

	// Operators
	// ====================================================================================================

	FORCEINLINE bool operator==(const FFixedTime& Other) const
	{
		// Note: Only compares ticks, assumes same tick rate
		return Tick == Other.Tick;
	}

	FORCEINLINE bool operator!=(const FFixedTime& Other) const
	{
		return Tick != Other.Tick;
	}

	FORCEINLINE bool operator<(const FFixedTime& Other) const
	{
		return Tick < Other.Tick;
	}

	FORCEINLINE bool operator<=(const FFixedTime& Other) const
	{
		return Tick <= Other.Tick;
	}

	FORCEINLINE bool operator>(const FFixedTime& Other) const
	{
		return Tick > Other.Tick;
	}

	FORCEINLINE bool operator>=(const FFixedTime& Other) const
	{
		return Tick >= Other.Tick;
	}

	FORCEINLINE FFixedTime operator+(const FFixedTime& Other) const
	{
		return FFixedTime(Tick + Other.Tick, TickRate);
	}

	FORCEINLINE FFixedTime operator-(const FFixedTime& Other) const
	{
		return FFixedTime(Tick - Other.Tick, TickRate);
	}

	FORCEINLINE FFixedTime& operator+=(const FFixedTime& Other)
	{
		Tick += Other.Tick;
		return *this;
	}

	FORCEINLINE FFixedTime& operator-=(const FFixedTime& Other)
	{
		Tick -= Other.Tick;
		return *this;
	}

	// String Conversion
	// ====================================================================================================

	FORCEINLINE FString ToString() const
	{
		return FString::Printf(TEXT("SimTime(Tick: %lld, Rate: %d, Seconds: %s)"), 
			Tick, TickRate, *ToSeconds().ToString());
	}
};

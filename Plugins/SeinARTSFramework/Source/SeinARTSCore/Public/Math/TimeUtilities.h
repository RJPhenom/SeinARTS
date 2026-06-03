/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		TimeUtilities.h
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		Utilities for deterministic simulation time management.
 * 				Provides the SeinTime namespace with functions to convert
 * 				between real-time seconds and simulation ticks, compute
 * 				fixed delta time, accumulate variable real-time into
 * 				discrete simulation ticks (with spiral-of-death guard),
 * 				calculate render interpolation alpha, lerp between
 * 				simulation times, and clamp simulation time ranges.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "FixedPoint.h"
#include "Types/Time.h"

/**
 * Utilities for deterministic time management.
 */
namespace SeinTime
{
	/**
	 * Converts real-time seconds to simulation ticks.
	 * @param Seconds - Real-time seconds (fixed-point).
	 * @param TickRate - Ticks per second.
	 * @return Number of ticks.
	 */
	FORCEINLINE int64 SecondsToTicks(FFixedPoint Seconds, int32 TickRate)
	{
		return (Seconds * FFixedPoint::FromInt32(TickRate)).ToInt64();
	}

	/**
	 * Converts simulation ticks to real-time seconds.
	 * @param Ticks - Number of ticks.
	 * @param TickRate - Ticks per second.
	 * @return Real-time seconds (fixed-point).
	 */
	FORCEINLINE FFixedPoint TicksToSeconds(int64 Ticks, int32 TickRate)
	{
		return FFixedPoint::FromInt64(Ticks) / FFixedPoint::FromInt32(TickRate);
	}

	/**
	 * Returns the fixed delta time for one tick.
	 * @param TickRate - Ticks per second.
	 * @return Delta time in seconds (fixed-point).
	 */
	FORCEINLINE FFixedPoint GetFixedDeltaTime(int32 TickRate)
	{
		return FFixedPoint::One / FFixedPoint::FromInt32(TickRate);
	}

	/**
	 * Interpolates between two simulation times using alpha [0, 1].
	 * Used for smooth rendering between fixed ticks.
	 * @param A - Start time.
	 * @param B - End time.
	 * @param Alpha - Interpolation factor [0, 1].
	 * @return Interpolated time in seconds.
	 */
	FORCEINLINE FFixedPoint LerpTime(const FSimulationTime& A, const FSimulationTime& B, FFixedPoint Alpha)
	{
		FFixedPoint TimeA = A.ToSeconds();
		FFixedPoint TimeB = B.ToSeconds();
		return TimeA + (TimeB - TimeA) * Alpha;
	}

	/**
	 * Calculates the difference between two simulation times in seconds.
	 * @param A - Later time.
	 * @param B - Earlier time.
	 * @return Time difference in seconds (fixed-point).
	 */
	FORCEINLINE FFixedPoint GetTimeDifference(const FSimulationTime& A, const FSimulationTime& B)
	{
		int64 TickDelta = A.Tick - B.Tick;
		return TicksToSeconds(TickDelta, A.TickRate);
	}

	/**
	 * Accumulates real-time delta and returns the number of simulation ticks to execute.
	 * Used to convert variable real-time to fixed simulation ticks.
	 * @param Accumulator - [in/out] Accumulated time in seconds.
	 * @param RealDeltaTime - Real-time delta from engine (seconds).
	 * @param TickRate - Simulation tick rate.
	 * @param MaxTicksPerFrame - Maximum ticks to process per frame (prevents spiral of death).
	 * @return Number of ticks to execute this frame.
	 */
	FORCEINLINE int32 AccumulateTicks(FFixedPoint& Accumulator, FFixedPoint RealDeltaTime, int32 TickRate, int32 MaxTicksPerFrame = 10)
	{
		Accumulator += RealDeltaTime;
		FFixedPoint FixedDeltaTime = GetFixedDeltaTime(TickRate);
		
		int32 TicksToExecute = 0;
		while (Accumulator >= FixedDeltaTime && TicksToExecute < MaxTicksPerFrame)
		{
			Accumulator -= FixedDeltaTime;
			++TicksToExecute;
		}
		
		return TicksToExecute;
	}

	/**
	 * Calculates the interpolation alpha for rendering between two simulation ticks.
	 * @param Accumulator - Accumulated time since last tick.
	 * @param TickRate - Simulation tick rate.
	 * @return Alpha value [0, 1] for interpolation.
	 */
	FORCEINLINE FFixedPoint GetInterpolationAlpha(FFixedPoint Accumulator, int32 TickRate)
	{
		FFixedPoint FixedDeltaTime = GetFixedDeltaTime(TickRate);
		return Accumulator / FixedDeltaTime;
	}

	/**
	 * Clamps a simulation time to a range.
	 * @param Time - Time to clamp.
	 * @param Min - Minimum time.
	 * @param Max - Maximum time.
	 * @return Clamped time.
	 */
	FORCEINLINE FSimulationTime ClampTime(const FSimulationTime& Time, const FSimulationTime& Min, const FSimulationTime& Max)
	{
		if (Time.Tick < Min.Tick)
			return Min;
		if (Time.Tick > Max.Tick)
			return Max;
		return Time;
	}
}

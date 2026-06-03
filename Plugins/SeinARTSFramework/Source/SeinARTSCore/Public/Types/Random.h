/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		Random.h
 * @date:		1/17/2026
 * @author:		RJ Macklem
 * @brief:		Deterministic random number generator for lockstep simulation.
 * 				Uses Xorshift128+ algorithm for fast, high-quality random numbers.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/Vector2D.h"
#include "Types/Rotator.h"
#include "Math/MathLib.h"
#include "Random.generated.h"

/**
 * Deterministic RNG state using Xorshift128+ algorithm.
 * Guaranteed to produce identical results across all platforms.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCORE_API FFixedRandom
{
	GENERATED_BODY()

	// Default seed constants
	static constexpr uint64 DEFAULT_SEED = 0x853C49E6748FEA9BULL;
	static constexpr uint64 DEFAULT_SEED_1 = 0xDA3E39CB94B95BDBULL;
	
	// SplitMix64 algorithm constants
	static constexpr uint64 SPLITMIX64_GOLDEN_RATIO_SEED = 0x9E3779B97F4A7C15ULL;
	static constexpr uint64 SPLITMIX64_MIX1_SEED = 0xBF58476D1CE4E5B9ULL;
	static constexpr uint64 SPLITMIX64_MIX2_SEED = 0x94D049BB133111EBULL;

	// RNG state (128 bits total)
	// Note: uint64 is not Blueprint-compatible, so we don't expose these as UPROPERTY
	// The struct is still BlueprintType and can be passed around in Blueprints
	uint64 State0;
	uint64 State1;

	// Constructors
	// ====================================================================================================

	FORCEINLINE FFixedRandom()
		: State0(DEFAULT_SEED)
		, State1(DEFAULT_SEED_1)
	{}

	FORCEINLINE explicit FFixedRandom(uint64 Seed)
	{
		SetSeed(Seed);
	}

	FORCEINLINE FFixedRandom(uint64 InState0, uint64 InState1)
		: State0(InState0)
		, State1(InState1)
	{
		// Ensure state is not all zeros
		if (State0 == 0 && State1 == 0)
		{
			State0 = DEFAULT_SEED;
			State1 = DEFAULT_SEED_1;
		}
	}

	// Seed Management
	// ====================================================================================================

	/**
	 * Initialize RNG with a seed value.
	 * Uses SplitMix64 to generate initial state from seed.
	 */
	FORCEINLINE void SetSeed(uint64 Seed)
	{
		// SplitMix64 algorithm to generate initial state
		auto SplitMix64 = [](uint64& x) -> uint64
		{
			uint64 z = (x += SPLITMIX64_GOLDEN_RATIO_SEED);
			z = (z ^ (z >> 30)) * SPLITMIX64_MIX1_SEED;
			z = (z ^ (z >> 27)) * SPLITMIX64_MIX2_SEED;
			return z ^ (z >> 31);
		};

		State0 = SplitMix64(Seed);
		State1 = SplitMix64(Seed);

		// Ensure state is not all zeros
		if (State0 == 0 && State1 == 0)
		{
			State0 = DEFAULT_SEED;
			State1 = DEFAULT_SEED_1;
		}
	}

	/**
	 * Get the current state for serialization/debugging.
	 */
	FORCEINLINE void GetState(uint64& OutState0, uint64& OutState1) const
	{
		OutState0 = State0;
		OutState1 = State1;
	}

	/**
	 * Set the current state for deserialization.
	 */
	FORCEINLINE void SetState(uint64 InState0, uint64 InState1)
	{
		State0 = InState0;
		State1 = InState1;

		// Ensure state is not all zeros
		if (State0 == 0 && State1 == 0)
		{
			State0 = DEFAULT_SEED;
			State1 = DEFAULT_SEED_1;
		}
	}

	// Core Random Generation
	// ====================================================================================================

	/**
	 * Generate next random uint64 value using Xorshift128+.
	 * This is the core function - all other random functions build on this.
	 */
	FORCEINLINE uint64 Next64()
	{
		uint64 s1 = State0;
		const uint64 s0 = State1;
		const uint64 result = s0 + s1;
		
		State0 = s0;
		s1 ^= s1 << 23;
		State1 = s1 ^ s0 ^ (s1 >> 18) ^ (s0 >> 5);
		
		return result;
	}

	/**
	 * Generate next random uint32 value.
	 */
	FORCEINLINE uint32 Next32()
	{
		return static_cast<uint32>(Next64() >> 32);
	}

	// Primary API
	// ====================================================================================================

	/**
	 * Generate random int32 value.
	 */
	FORCEINLINE int32 Int()
	{
		return static_cast<int32>(Next32());
	}

	/**
	 * Generate random int64 value.
	 */
	FORCEINLINE int64 Int64()
	{
		return static_cast<int64>(Next64());
	}

	/**
	 * Generate random FFixedPoint in range [0, 1].
	 */
	FORCEINLINE FFixedPoint FixedPoint()
	{
		// Map uint32 to [0, 1] in fixed-point
		const uint32 Value = Next32();
		// Explicitly: (Value * 2^32) >> 32 = Value (in fixed-point format)
		return FFixedPoint(static_cast<int64>((static_cast<uint64>(Value) * static_cast<uint64>(FFixedPoint::One.Value)) >> 32));
	}

	/**
	 * Generate random boolean (50/50 chance).
	 */
	FORCEINLINE bool Bool()
	{
		return (Next32() & 1) != 0;
	}

	/**
	 * Generate random boolean with probability (0 to 1).
	 * Returns true with given probability.
	 */
	FORCEINLINE bool Bool(FFixedPoint Probability)
	{
		return FixedPoint() < Probability;
	}

	/**
	 * Generate random FFixedPoint in range [Min, Max].
	 */
	FORCEINLINE FFixedPoint Range(FFixedPoint Min, FFixedPoint Max)
	{
		if (Min >= Max) return Min;
		return Min + FixedPoint() * (Max - Min);
	}

	/**
	 * Generate random int32 in range [Min, Max] (inclusive).
	 */
	FORCEINLINE int32 IntRange(int32 Min, int32 Max)
	{
		if (Min >= Max) return Min;
		
		const uint32 Range = static_cast<uint32>(Max - Min + 1);
		const uint32 Value = Next32();
		
		return Min + static_cast<int32>(Value % Range);
	}

	/**
	 * Generate random array index in range [0, Num-1].
	 */
	template<typename T>
	FORCEINLINE int32 Index(const TArray<T>& Array)
	{
		check(Array.Num() > 0);
		return IntRange(0, Array.Num() - 1);
	}

	/**
	 * Select random element from array.
	 */
	template<typename T>
	FORCEINLINE T& Element(TArray<T>& Array)
	{
		check(Array.Num() > 0);
		return Array[Index(Array)];
	}

	/**
	 * Select random element from const array.
	 */
	template<typename T>
	FORCEINLINE const T& Element(const TArray<T>& Array) const
	{
		check(Array.Num() > 0);
		return Array[const_cast<FFixedRandom*>(this)->Index(Array)];
	}

	// Spatial Functions
	// ====================================================================================================

	/**
	 * Generate random 2D point inside a circle.
	 * @param Centre - Center of the circle
	 * @param Radius - Radius of the circle
	 * @param Normal - Optional normal vector (for painting on different planes, not used in 2D)
	 */
	FORCEINLINE FFixedVector2D PointInCircle(FFixedVector2D Centre, FFixedPoint Radius, FFixedVector Normal = FFixedVector::UpVector)
	{
		// Rejection sampling for uniform distribution
		FFixedVector2D Offset;
		FFixedPoint LengthSq;
		
		do
		{
			Offset.X = Range(-Radius, Radius);
			Offset.Y = Range(-Radius, Radius);
			LengthSq = Offset.SizeSquared();
		} while (LengthSq > Radius * Radius);
		
		return Centre + Offset;
	}

	/**
	 * Generate random 3D point inside a sphere.
	 * @param Centre - Center of the sphere
	 * @param Radius - Radius of the sphere
	 */
	FORCEINLINE FFixedVector PointInSphere(FFixedVector Centre, FFixedPoint Radius)
	{
		// Rejection sampling for uniform distribution
		FFixedVector Offset;
		FFixedPoint LengthSq;
		
		do
		{
			Offset.X = Range(-Radius, Radius);
			Offset.Y = Range(-Radius, Radius);
			Offset.Z = Range(-Radius, Radius);
			LengthSq = Offset.SizeSquared();
		} while (LengthSq > Radius * Radius);
		
		return Centre + Offset;
	}

	/**
	 * Generate random 2D point inside a rectangle.
	 * @param Centre - Center of the rectangle
	 * @param HalfExtents - Half-size of the rectangle (from center to edge)
	 * @param Normal - Optional normal vector (for orientation on different planes, not used in 2D)
	 */
	FORCEINLINE FFixedVector2D PointInRect(FFixedVector2D Centre, FFixedVector2D HalfExtents, FFixedVector Normal = FFixedVector::UpVector)
	{
		FFixedVector2D Offset(
			Range(-HalfExtents.X, HalfExtents.X),
			Range(-HalfExtents.Y, HalfExtents.Y)
		);
		
		return Centre + Offset;
	}

	/**
	 * Generate random 3D point inside a box.
	 * @param Centre - Center of the box
	 * @param HalfExtents - Half-size of the box (from center to edge)
	 */
	FORCEINLINE FFixedVector PointInBox(FFixedVector Centre, FFixedVector HalfExtents)
	{
		FFixedVector Offset(
			Range(-HalfExtents.X, HalfExtents.X),
			Range(-HalfExtents.Y, HalfExtents.Y),
			Range(-HalfExtents.Z, HalfExtents.Z)
		);
		
		return Centre + Offset;
	}

	// Unit Circle/Sphere Functions
	// ====================================================================================================

	/**
	 * Generate random 2D point inside unit circle.
	 */
	FORCEINLINE FFixedVector2D InsideUnitCircle()
	{
		return PointInCircle(FFixedVector2D::ZeroVector(), FFixedPoint::One);
	}

	/**
	 * Generate random unit 2D vector.
	 */
	FORCEINLINE FFixedVector2D OnUnitCircle()
	{
		// Use angle approach for uniform distribution on circle
		FFixedPoint Angle = Range(FFixedPoint::Zero, FFixedPoint::TwoPi);
		return FFixedVector2D(
			SeinMath::Cos(Angle),
			SeinMath::Sin(Angle)
		);
	}

	/**
	 * Generate random 3D point inside unit sphere.
	 */
	FORCEINLINE FFixedVector InsideUnitSphere()
	{
		return PointInSphere(FFixedVector::ZeroVector, FFixedPoint::One);
	}

	/**
	 * Generate random unit 3D vector.
	 */
	FORCEINLINE FFixedVector OnUnitSphere()
	{
		FFixedVector Result = InsideUnitSphere();
		return FFixedVector::GetSafeNormal(Result, FFixedPoint::SmallNumber);
	}

	// Utility
	// ====================================================================================================

	/**
	 * Shuffle an array using Fisher-Yates algorithm.
	 */
	template<typename T>
	FORCEINLINE void Shuffle(TArray<T>& Array)
	{
		const int32 Num = Array.Num();
		for (int32 i = Num - 1; i > 0; --i)
		{
			const int32 j = IntRange(0, i);
			Array.Swap(i, j);
		}
	}

	/**
	 * Generate random rotation (yaw only, for ground units).
	 */
	FORCEINLINE FFixedPoint Yaw()
	{
		return Range(FFixedPoint::Zero, FFixedPoint::TwoPi);
	}

	/**
	 * Generate random pitch rotation.
	 */
	FORCEINLINE FFixedPoint Pitch()
	{
		return Range(FFixedPoint::Zero, FFixedPoint::TwoPi);
	}

	/**
	 * Generate random roll rotation.
	 */
	FORCEINLINE FFixedPoint Roll()
	{
		return Range(FFixedPoint::Zero, FFixedPoint::TwoPi);
	}

	/**
	 * Generate random rotator with random pitch, yaw, and roll.
	 */
	FORCEINLINE FFixedRotator RandomRotator()
	{
		return FFixedRotator(Pitch(), Yaw(), Roll());
	}

	// Operators
	// ====================================================================================================

	FORCEINLINE bool operator==(const FFixedRandom& Other) const
	{
		return State0 == Other.State0 && State1 == Other.State1;
	}

	FORCEINLINE bool operator!=(const FFixedRandom& Other) const
	{
		return !(*this == Other);
	}

	// String Conversion
	// ====================================================================================================

	FORCEINLINE FString ToString() const
	{
		return FString::Printf(TEXT("RNG(State0: %llu, State1: %llu)"), State0, State1);
	}
};

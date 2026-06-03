/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		Entity.h
 * @date:		2/28/2026
 * @author:		RJ Macklem
 * @brief:		Core entity data structure for simulation.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/EntityID.h"
#include "Types/Transform.h"
#include "Types/FixedPoint.h"
#include "Entity.generated.h"

/**
 * Core entity data that all simulation entities have.
 * Kept minimal to ensure cache-friendly iteration.
 * All gameplay data (health, damage, etc.) lives in modular components.
 */
USTRUCT(BlueprintType)
struct SEINARTSCORE_API FSeinEntity
{
	GENERATED_BODY()

	/** Unique identifier for this entity */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Entity")
	FSeinID ID;

	/** Current transform in simulation space (fixed-point) */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Entity")
	FFixedTransform Transform;

	/** Entity state flags. Sim-side authoritative; clients read to decide
	 *  selection UI (FLAG_SELECTABLE — DESIGN §15). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Entity")
	int32 Flags;

	// State flag bits
	static constexpr int32 FLAG_ALIVE        = 1 << 0;
	static constexpr int32 FLAG_INVULNERABLE = 1 << 1;
	static constexpr int32 FLAG_SELECTABLE   = 1 << 2;

	FSeinEntity()
		: ID(FSeinID::Invalid())
		, Transform(FFixedTransform::Identity())
		, Flags(FLAG_ALIVE | FLAG_SELECTABLE)
	{}

	explicit FSeinEntity(FSeinID InID)
		: ID(InID)
		, Transform(FFixedTransform::Identity())
		, Flags(FLAG_ALIVE | FLAG_SELECTABLE)
	{}

	/** Check if entity is alive */
	FORCEINLINE bool IsAlive() const
	{
		return (Flags & FLAG_ALIVE) != 0;
	}

	/** Set alive flag */
	FORCEINLINE void SetAlive(bool bAlive)
	{
		if (bAlive) { Flags |= FLAG_ALIVE; }
		else        { Flags &= ~FLAG_ALIVE; }
	}

	/** Client selection gate (DESIGN §15). True = player UI can pick this entity. */
	FORCEINLINE bool IsSelectable() const { return (Flags & FLAG_SELECTABLE) != 0; }
	FORCEINLINE void SetSelectable(bool bSelectable)
	{
		if (bSelectable) { Flags |= FLAG_SELECTABLE; }
		else             { Flags &= ~FLAG_SELECTABLE; }
	}
};

/** Hash function for FSeinEntity for state hashing */
FORCEINLINE uint32 GetTypeHash(const FSeinEntity& Entity)
{
	uint32 Hash = GetTypeHash(Entity.ID);
	Hash = HashCombine(Hash, GetTypeHash(Entity.Transform));
	Hash = HashCombine(Hash, GetTypeHash(Entity.Flags));
	return Hash;
}

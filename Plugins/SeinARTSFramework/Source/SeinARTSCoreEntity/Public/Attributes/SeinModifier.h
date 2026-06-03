/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinModifier.h
 * @brief   Attribute modifier for the deterministic attribute system.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Core/SeinEntityHandle.h"
#include "GameplayTagContainer.h"
#include "SeinModifier.generated.h"

/** How the modifier value is applied to the base attribute. */
UENUM(BlueprintType)
enum class ESeinModifierOp : uint8
{
	Add,        // Final += Value
	Multiply,   // Final *= Value
	Override    // Final = Value (last override wins)
};

/** What pool of entities (or player-state fields) the modifier influences.
 *  Determined by the containing effect's scope (DESIGN §8). */
UENUM(BlueprintType)
enum class ESeinModifierScope : uint8
{
	/** Affects a single entity — modifier lives on the entity's FSeinActiveEffectsComponent. */
	Instance,
	/** Affects every entity owned by a player whose tags include TargetClassTag —
	 *  modifier lives on the player state's ClassEffects list. */
	Class,
	/** Affects the player state itself (resource rates, caps, pop cap, upkeep) —
	 *  modifier lives on the player state's PlayerEffects list. TargetComponentType
	 *  points at FSeinPlayerState or a designer-authored sub-struct on it. */
	Player
};

/**
 * A single attribute modifier. Targets a specific FFixedPoint field on a
 * component struct identified by UScriptStruct* and FName.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinModifier
{
	GENERATED_BODY()

	// --- Target ---

	/** The component struct type that contains the field to modify. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Ability|Target")
	TObjectPtr<UScriptStruct> TargetComponentType;

	/** The FFixedPoint field name within the target component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Ability|Target")
	FName TargetFieldName;

	// --- Modifier ---

	/** How the value is applied (Add, Multiply, Override). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Modifier")
	ESeinModifierOp Operation = ESeinModifierOp::Add;

	/** The modifier value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Modifier")
	FFixedPoint Value;

	// --- Scope ---

	/** Whether this targets a single entity, a class, or the whole player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Modifier")
	ESeinModifierScope Scope = ESeinModifierScope::Instance;

	// --- Source Tracking ---

	/** Entity that applied this modifier. */
	UPROPERTY()
	FSeinEntityHandle SourceEntity;

	/** Effect instance ID that owns this modifier (0 = standalone). */
	UPROPERTY()
	uint32 SourceEffectID = 0;

	// --- Class Targeting ---

	/** For Class-scope modifiers: gameplay tag identifying the target class. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Ability|Target", meta = (EditCondition = "Scope == ESeinModifierScope::Class"))
	FGameplayTag TargetClassTag;

	FSeinModifier() = default;
};

FORCEINLINE uint32 GetTypeHash(const FSeinModifier& Mod)
{
	uint32 Hash = GetTypeHash(Mod.TargetFieldName);
	Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Mod.Operation)));
	Hash = HashCombine(Hash, GetTypeHash(Mod.Value));
	Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Mod.Scope)));
	Hash = HashCombine(Hash, GetTypeHash(Mod.SourceEntity));
	Hash = HashCombine(Hash, GetTypeHash(Mod.SourceEffectID));
	return Hash;
}

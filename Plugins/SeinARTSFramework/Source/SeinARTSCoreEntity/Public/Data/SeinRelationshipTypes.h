/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinRelationshipTypes.h
 * @brief   Deterministic directional player-pair capability data.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinPlayerID.h"
#include "GameplayTagContainer.h"
#include "SeinRelationshipTypes.generated.h"

/**
 * One authoritative source grant for an ordered player pair.
 *
 * Direction is source-player -> target-player. The meaning is capability
 * specific; for ShareVision, A -> B means B may consume A's vision. Self-pairs
 * are implicit and are not stored.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinPairCapabilityGrantRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Relationship")
	FSeinPlayerID SourcePlayer;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Relationship")
	FSeinPlayerID TargetPlayer;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Relationship",
		meta = (Categories = "SeinARTS.Relationship.Capability"))
	FGameplayTag CapabilityTag;

	/** Kind of system or agreement that owns this grant instance. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Relationship",
		meta = (Categories = "SeinARTS.Relationship.Source"))
	FGameplayTag SourceKindTag;

	/** Stable positive identity of the exact source instance within its kind. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Relationship")
	int64 SourceInstanceID = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Relationship")
	int32 RefCount = 0;
};

/** Payload for the minimal built-in pair-capability mutation command. */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinSetPairCapabilityCommandPayload
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Relationship")
	FSeinPlayerID SourcePlayer;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Relationship")
	FSeinPlayerID TargetPlayer;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Relationship",
		meta = (Categories = "SeinARTS.Relationship.Capability"))
	FGameplayTag CapabilityTag;

	/** Kind of system or agreement that owns this grant instance. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Relationship",
		meta = (Categories = "SeinARTS.Relationship.Source"))
	FGameplayTag SourceKindTag;

	/** Stable positive identity of the exact source instance within its kind. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Relationship")
	int64 SourceInstanceID = 0;

	/** True grants one ref from the exact source; false revokes one matching ref. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Relationship")
	bool bGrant = true;
};

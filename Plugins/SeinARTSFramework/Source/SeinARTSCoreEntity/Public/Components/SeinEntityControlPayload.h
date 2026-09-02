/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEntityControlComponent.h
 * @brief   Deterministic, entity-scoped command-control grants.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SeinPayload.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "GameplayTagContainer.h"
#include "SeinEntityControlPayload.generated.h"

/**
 * Stable identity for one entity-control grant.
 *
 * Grant serials are monotonic per target entity. Pairing the serial with the
 * full generational entity handle makes the identity world-unique without a
 * hidden UObject allocator: two entities may both issue serial 1, but their
 * IDs remain distinct, and a recycled entity slot has a different generation.
 * The component's next serial is ordinary sim state, so snapshot/restore and
 * state hashing preserve subsequent allocation exactly.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinEntityControlGrantID
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command|Authority")
	FSeinEntityHandle TargetEntity;

	/** Positive, never-reused serial within TargetEntity's lifetime. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command|Authority")
	int64 Serial = 0;

	bool IsValid() const { return TargetEntity.IsValid() && Serial > 0; }

	bool operator==(const FSeinEntityControlGrantID& Other) const
	{
		return TargetEntity == Other.TargetEntity && Serial == Other.Serial;
	}

	bool operator!=(const FSeinEntityControlGrantID& Other) const
	{
		return !(*this == Other);
	}

	bool operator<(const FSeinEntityControlGrantID& Other) const
	{
		return TargetEntity < Other.TargetEntity
			|| (TargetEntity == Other.TargetEntity && Serial < Other.Serial);
	}
};

FORCEINLINE uint32 GetTypeHash(const FSeinEntityControlGrantID& ID)
{
	return HashCombine(GetTypeHash(ID.TargetEntity), GetTypeHash(ID.Serial));
}

template<>
struct TStructOpsTypeTraits<FSeinEntityControlGrantID>
	: public TStructOpsTypeTraitsBase2<FSeinEntityControlGrantID>
{
	enum { WithGetTypeHash = true };
};

/**
 * One exact entity-control delegation.
 *
 * StartTick is inclusive. EndTick is exclusive; INDEX_NONE means permanent.
 * AllowedCommandTypes uses exact tag matching. An empty list permits every
 * structurally valid entity command; match-level permissions are separate.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinEntityControlGrant
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command|Authority")
	FSeinEntityControlGrantID GrantID;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command|Authority")
	FSeinPlayerID Grantee;

	/** Empty means all entity command types. Otherwise entries are unique and
	 *  stored in canonical lexical tag-name order. Matching is exact. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command|Authority")
	TArray<FGameplayTag> AllowedCommandTypes;

	/** First sim tick on which this grant is active. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command|Authority")
	int32 StartTick = 0;

	/** First sim tick on which this grant is inactive. INDEX_NONE = permanent. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command|Authority")
	int32 EndTick = INDEX_NONE;
};

FORCEINLINE uint32 GetTypeHash(const FSeinEntityControlGrant& Grant)
{
	uint32 Hash = GetTypeHash(Grant.GrantID);
	Hash = HashCombine(Hash, GetTypeHash(Grant.Grantee));
	Hash = HashCombine(Hash, GetTypeHash(Grant.StartTick));
	Hash = HashCombine(Hash, GetTypeHash(Grant.EndTick));
	Hash = HashCombine(Hash, GetTypeHash(Grant.AllowedCommandTypes.Num()));
	for (const FGameplayTag& Tag : Grant.AllowedCommandTypes)
	{
		// Hash tag text, not process-local FName comparison indices.
		Hash = HashCombine(Hash, GetTypeHash(Tag.ToString()));
	}
	return Hash;
}

template<>
struct TStructOpsTypeTraits<FSeinEntityControlGrant>
	: public TStructOpsTypeTraitsBase2<FSeinEntityControlGrant>
{
	enum { WithGetTypeHash = true };
};

/**
 * Runtime delegation state stored on the entity being controlled.
 *
 * This is subdata, not an authored gameplay component. The entity-control
 * library adds it on the first grant and deliberately retains it after every
 * grant is revoked so NextGrantSerial can never rewind or reuse an ID.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic, SeinSubData))
struct SEINARTSCOREENTITY_API FSeinEntityControlPayload : public FSeinPayload
{
	GENERATED_BODY()

	/** Next positive serial. Zero means the int64 namespace is exhausted. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command|Authority")
	int64 NextGrantSerial = 1;

	/** Canonical ascending GrantID order. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command|Authority")
	TArray<FSeinEntityControlGrant> Grants;
};

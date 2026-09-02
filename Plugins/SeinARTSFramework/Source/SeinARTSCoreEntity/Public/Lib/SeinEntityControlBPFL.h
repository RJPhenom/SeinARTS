/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEntityControlBPFL.h
 * @brief   Blueprint and C++ API for deterministic entity-control grants.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Components/SeinEntityControlPayload.h"
#include "SeinEntityControlBPFL.generated.h"

class USeinWorldSubsystem;

/**
 * Default entity-control policy primitive.
 *
 * A player can control an entity when they are its current owner, or when an
 * active exact-entity grant permits the command type. The policy consumes only
 * canonical simulation state and grants no match-level permissions.
 */
UCLASS(meta = (DisplayName = "SeinARTS Entity Control Library"))
class SEINARTSCOREENTITY_API USeinEntityControlBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create an exact-entity control grant.
	 *
	 * StartTick is inclusive; INDEX_NONE resolves to the world's current tick.
	 * EndTick is exclusive; INDEX_NONE means permanent. A finite end must be
	 * greater than the resolved start. Invalid entities, neutral grantees, bad
	 * intervals, or exhausted ID state return an invalid ID without mutation.
	 * AllowedCommandTypes is normalized to unique lexical order; empty permits
	 * every structurally valid entity command.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Command|Authority",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Grant Entity Control",
			AdvancedDisplay = "AllowedCommandTypes,StartTick,EndTick"))
	static FSeinEntityControlGrantID SeinGrantEntityControl(
		const UObject* WorldContextObject,
		FSeinEntityHandle TargetEntity,
		FSeinPlayerID Grantee,
		const TArray<FGameplayTag>& AllowedCommandTypes,
		int32 StartTick = -1,
		int32 EndTick = -1);

	/** Revoke one grant by stable ID. IDs are never reused after revocation. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Command|Authority",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Revoke Entity Control"))
	static bool SeinRevokeEntityControl(
		const UObject* WorldContextObject,
		FSeinEntityControlGrantID GrantID);

	/** Remove every finite grant whose exclusive end is <= the current tick.
	 *  The allocator component remains so IDs cannot rewind. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Command|Authority",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Prune Expired Entity Control Grants"))
	static int32 SeinPruneExpiredEntityControlGrants(
		const UObject* WorldContextObject,
		FSeinEntityHandle TargetEntity);

	/** Owner-only by default, extended by active exact grants. Neutral is not a
	 *  player principal and therefore cannot gain control merely because an
	 *  entity is neutral-owned. Restricted grants match CommandType exactly. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Command|Authority",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Can Player Control Entity"))
	static bool SeinCanPlayerControlEntity(
		const UObject* WorldContextObject,
		FSeinPlayerID Player,
		FSeinEntityHandle TargetEntity,
		FGameplayTag CommandType);

	/** True when the identified grant exists and is active at the current tick. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Command|Authority",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Is Entity Control Grant Active"))
	static bool SeinIsEntityControlGrantActive(
		const UObject* WorldContextObject,
		FSeinEntityControlGrantID GrantID);

	/** Copy the canonical grant list. Includes inactive future and expired
	 *  records until they are explicitly revoked/pruned. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Command|Authority",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Entity Control Grants"))
	static TArray<FSeinEntityControlGrant> SeinGetEntityControlGrants(
		const UObject* WorldContextObject,
		FSeinEntityHandle TargetEntity);

	/** C++ policy entry point with an explicit tick. Useful to validate a
	 *  canonical command against the tick at which it will execute. */
	static bool CanPlayerControlEntityAtTick(
		const USeinWorldSubsystem& World,
		FSeinPlayerID Player,
		FSeinEntityHandle TargetEntity,
		FGameplayTag CommandType,
		int32 AtTick);

	/** C++ exact-grant query with an explicit tick. */
	static bool IsEntityControlGrantActiveAtTick(
		const USeinWorldSubsystem& World,
		FSeinEntityControlGrantID GrantID,
		int32 AtTick);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinEntityBPFL.h
 * @author       RJ Macklem
 * @created      27 Mar 2026
 * @latest       14 Aug 2026
 * @brief        Exposes deterministic entity lifecycle and query operations to Blueprint.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "Types/Transform.h"
#include "SeinEntityBPFL.generated.h"

class USeinWorldSubsystem;
class ASeinActor;

UCLASS(meta = (DisplayName = "SeinARTS Entity Library", SeinDeterministic))
class SEINARTSCOREENTITY_API USeinEntityBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Entity Lifecycle
	// ====================================================================================================

	/** Spawn an entity from a Blueprint class at the given transform, owned by the specified player */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity", meta = (WorldContext = "WorldContextObject", DisplayName = "Spawn Entity"))
	static FSeinEntityHandle SeinSpawnEntity(const UObject* WorldContextObject, TSubclassOf<ASeinActor> ActorClass, const FFixedTransform& SpawnTransform, FSeinPlayerID OwnerPlayerID);

	/** Destroy an entity (deferred to post-tick cleanup) */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity", meta = (WorldContext = "WorldContextObject", DisplayName = "Destroy Entity"))
	static void SeinDestroyEntity(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	// Entity Queries
	// ====================================================================================================

	/** Get the simulation transform of an entity */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Entity Transform"))
	static FFixedTransform SeinGetEntityTransform(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	/** Set the simulation transform of an entity */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Entity Transform"))
	static void SeinSetEntityTransform(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, const FFixedTransform& Transform);

	/** Get the owner player ID of an entity */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Entity Owner"))
	static FSeinPlayerID SeinGetEntityOwner(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	/** Check whether an entity handle refers to a living entity */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity", meta = (WorldContext = "WorldContextObject", DisplayName = "Is Entity Alive"))
	static bool SeinIsEntityAlive(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	/** Check whether an entity handle is valid (non-zero generation) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity", meta = (DisplayName = "Is Handle Valid"))
	static bool SeinIsHandleValid(FSeinEntityHandle EntityHandle);

	/** Returns an invalid entity handle */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity", meta = (DisplayName = "Invalid Handle"))
	static FSeinEntityHandle SeinInvalidHandle();

	// Archetype-definition access was removed in the Phase-5 refactor. Read identity /
	// producibility metadata via the typed `Get Component` K2 node against
	// `FSeinIdentityPayload` / `FSeinProduciblePayload` on the entity.

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};

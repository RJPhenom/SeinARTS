/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinChildTransformsBPFL.h
 * @brief   Read-only BPFL for entities' child-transform trees. Mutations
 *          live in USeinSimMutationBPFL (abilities/effects only).
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Components/SeinChildTransformsComponent.h"
#include "GameplayTagContainer.h"
#include "Types/Transform.h"
#include "SeinChildTransformsBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Child Transforms Library"))
class SEINARTSCOREENTITY_API USeinChildTransformsBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ===== Lookups =====

	/** True if the entity has a child node with this tag anywhere in its
	 *  tree. Walks DFS. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|ChildTransforms",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Has Child"))
	static bool SeinHasChild(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag);

	/** Read a full FSeinChildTransform copy by tag (DFS). Returns false on
	 *  invalid handle / missing component / tag-not-found (OutNode left
	 *  default-constructed). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|ChildTransforms",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Child"))
	static bool SeinGetChild(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag, FSeinChildTransform& OutNode);

	/** Local transform of the named child (relative to its parent). Returns
	 *  identity on miss. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|ChildTransforms",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Child Local Transform"))
	static FFixedTransform SeinGetChildLocalTransform(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag);

	/** World transform of the named child — composed by walking ancestors:
	 *  Entity.Transform * (each ancestor's LocalTransform) * (this node's
	 *  LocalTransform). Returns Entity transform on miss (so callers using
	 *  the result for "spawn at world position" still produce a valid
	 *  fallback at the unit's location). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|ChildTransforms",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Child World Transform"))
	static FFixedTransform SeinGetChildWorldTransform(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag);

	/** Tag of the immediate parent of this child, or invalid tag if the
	 *  child sits at the root of the tree (parent is the entity itself).
	 *  Useful for ability scripts walking up the hierarchy. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|ChildTransforms",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Child Parent Tag"))
	static FGameplayTag SeinGetChildParentTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag);

	/** Direct (one-level) children of the named parent tag. Pass an invalid
	 *  tag to get the root-level children of the entity. Returns copies. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|ChildTransforms",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Direct Children Of"))
	static TArray<FSeinChildTransform> SeinGetDirectChildrenOf(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag ParentTag);

	/** Whole tree as a copy. Useful for editors / debug overlays / one-off
	 *  walks. Hot paths should target individual tags via the lookups
	 *  above to avoid the array copy. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|ChildTransforms",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Child Transforms Data"))
	static bool SeinGetChildTransformsData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinChildTransformsComponent& OutData);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};

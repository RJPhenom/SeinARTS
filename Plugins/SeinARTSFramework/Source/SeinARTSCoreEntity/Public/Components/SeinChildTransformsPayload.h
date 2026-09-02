/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinChildTransformsComponent.h
 * @brief:   Sim-side child-transform tree on an entity. Each entity can host
 *           N tagged children, each with its own FFixedTransform relative to
 *           its parent (the entity for root-level children, or another
 *           FSeinChildTransform for nested ones).
 *
 *           Used for: turret rotation, MG mounts, hatch states, antennas,
 *           any visual or sim-relevant sub-pose that moves independently of
 *           the entity's body. Hardpoints / weapons / aim-tracking abilities
 *           layer on top — they reference children by tag and mutate the
 *           local transforms via USeinSimMutationBPFL.
 *
 *           Storage shape — FLAT array with ParentTag references rather
 *           than a nested tree. UHT doesn't support `TArray<FSelfType>`
 *           recursion via UPROPERTY, so the hierarchy is encoded as
 *           `ParentTag` on each node — invalid tag = root-level (parent is
 *           the entity), otherwise references another entry's Tag in the
 *           same array. Tree traversal walks ancestors via ParentTag and
 *           filters direct children by ParentTag scan.
 *
 *           Render-side application: USeinChildTransformsComponent declares
 *           a parallel flat array mapping each tag to a USceneComponent on
 *           the actor BP. The bridge subsystem walks the array each render
 *           frame and applies local transforms to the bound scene
 *           components.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SeinPayload.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "Types/Transform.h"
#include "SeinChildTransformsPayload.generated.h"

/**
 * One node in the entity's child-transform tree. Hierarchy is encoded via
 * ParentTag rather than nesting (UHT limitation on recursive USTRUCT
 * arrays). Designer authors the flat list on USeinChildTransformsComponent;
 * abilities mutate local transforms in place via USeinSimMutationBPFL
 * field-level setters.
 *
 * Tag uniqueness: tags are expected unique within an entity — BPFL lookups
 * walk linearly and return the first match. Convention is to namespace via
 * dotted tags (`Turret`, `Turret.Barrel`, `Turret.Barrel.Muzzle`) so
 * accidents are obvious in the editor.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinChildTransform
{
	GENERATED_BODY()

	/** Identifier for this child within its owning entity. Looked up via
	 *  SeinChildTransformsBPFL by tag, never by array index — so reordering
	 *  the array in the editor never invalidates references. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|ChildTransforms")
	FGameplayTag Tag;

	/** Parent reference. Invalid tag = root-level (parent is the entity's
	 *  own transform). Any other value must match the Tag of another entry
	 *  in the same `FSeinChildTransformsPayload::Children` array. World-space
	 *  composition walks the ParentTag chain up to the entity, multiplying
	 *  LocalTransforms.
	 *
	 *  Cycles (A's parent is B and B's parent is A) are designer error;
	 *  framework walks bound the chain depth at 32 to prevent infinite
	 *  loops, silently bottoming out if a cycle is detected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|ChildTransforms")
	FGameplayTag ParentTag;

	/** Transform relative to the parent (entity for root nodes, or the
	 *  named ParentTag node for nested ones). World-space transform is
	 *  composed on demand by walking the parent chain — see
	 *  `SeinChildTransformsBPFL::SeinGetChildWorldTransform`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|ChildTransforms")
	FFixedTransform LocalTransform;
};

/**
 * Sim component holding the entity's child-transform tree as a flat array.
 * Tree structure is encoded via FSeinChildTransform::ParentTag references.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinChildTransformsPayload : public FSeinPayload
{
	GENERATED_BODY()

	/** Flat array of all children. Hierarchy encoded via ParentTag on each
	 *  node. Order is the editor authoring order — composition walks via
	 *  tag references, not array order, so designers can rearrange freely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|ChildTransforms")
	TArray<FSeinChildTransform> Children;
};

/** Order-stable hash. Walks the array in authoring order, hashing each
 *  node's Tag + ParentTag + LocalTransform. Tree topology is implicit in
 *  the ParentTag values, so same-shape data on every client → same hash. */
FORCEINLINE uint32 GetTypeHash(const FSeinChildTransform& Node)
{
	uint32 Hash = GetTypeHash(Node.Tag);
	Hash = HashCombine(Hash, GetTypeHash(Node.ParentTag));
	Hash = HashCombine(Hash, GetTypeHash(Node.LocalTransform));
	return Hash;
}

FORCEINLINE uint32 GetTypeHash(const FSeinChildTransformsPayload& Data)
{
	uint32 Hash = 0;
	for (const FSeinChildTransform& Child : Data.Children)
	{
		Hash = HashCombine(Hash, GetTypeHash(Child));
	}
	return Hash;
}

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinChildTransformsBPFL.cpp
 */

#include "Lib/SeinChildTransformsBPFL.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Types/Entity.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinChildBPFL, Log, All);

namespace
{
	/** Maximum parent-chain depth we'll walk when composing world transforms.
	 *  Bounds runaway in the cycle-error case (designer set A's parent to B
	 *  and B's parent to A). 32 is more than any sane mount hierarchy
	 *  (chassis → turret → barrel → muzzle is 3 levels). */
	constexpr int32 MaxParentChainDepth = 32;

	/** Linear find by tag in the flat array. */
	const FSeinChildTransform* FindByTag(const TArray<FSeinChildTransform>& Nodes, FGameplayTag Tag)
	{
		for (const FSeinChildTransform& N : Nodes)
		{
			if (N.Tag == Tag) return &N;
		}
		return nullptr;
	}

	/** Walk up the ParentTag chain, multiplying LocalTransforms in
	 *  root-down order to produce the world transform of the leaf. */
	FFixedTransform ComposeWorldFromChain(
		const TArray<FSeinChildTransform>& Nodes,
		const FSeinChildTransform* Leaf,
		const FFixedTransform& EntityWorld)
	{
		// Build the chain leaf → root.
		TArray<const FSeinChildTransform*, TInlineAllocator<8>> Chain;
		Chain.Add(Leaf);
		const FSeinChildTransform* Cursor = Leaf;
		int32 Depth = 0;
		while (Cursor->ParentTag.IsValid() && Depth < MaxParentChainDepth)
		{
			const FSeinChildTransform* Parent = FindByTag(Nodes, Cursor->ParentTag);
			if (!Parent) break;          // dangling parent ref — bottom out at this level
			Chain.Add(Parent);
			Cursor = Parent;
			++Depth;
		}

		// Compose root-down. `Parent * Child` returns ChildWorld via the
		// FFixedTransform operator's Multiply convention.
		FFixedTransform W = EntityWorld;
		for (int32 i = Chain.Num() - 1; i >= 0; --i)
		{
			W = W * Chain[i]->LocalTransform;
		}
		return W;
	}
}

USeinWorldSubsystem* USeinChildTransformsBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

bool USeinChildTransformsBPFL::SeinHasChild(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return false;
	const FSeinChildTransformsComponent* Data = Sub->GetComponent<FSeinChildTransformsComponent>(EntityHandle);
	if (!Data) return false;
	return FindByTag(Data->Children, Tag) != nullptr;
}

bool USeinChildTransformsBPFL::SeinGetChild(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag, FSeinChildTransform& OutNode)
{
	OutNode = FSeinChildTransform{};
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return false;
	const FSeinChildTransformsComponent* Data = Sub->GetComponent<FSeinChildTransformsComponent>(EntityHandle);
	if (!Data) return false;
	if (const FSeinChildTransform* Found = FindByTag(Data->Children, Tag))
	{
		OutNode = *Found;
		return true;
	}
	return false;
}

FFixedTransform USeinChildTransformsBPFL::SeinGetChildLocalTransform(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return FFixedTransform::Identity();
	const FSeinChildTransformsComponent* Data = Sub->GetComponent<FSeinChildTransformsComponent>(EntityHandle);
	if (!Data) return FFixedTransform::Identity();
	if (const FSeinChildTransform* Found = FindByTag(Data->Children, Tag))
	{
		return Found->LocalTransform;
	}
	return FFixedTransform::Identity();
}

FFixedTransform USeinChildTransformsBPFL::SeinGetChildWorldTransform(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return FFixedTransform::Identity();
	const FSeinEntity* Entity = Sub->GetEntity(EntityHandle);
	if (!Entity) return FFixedTransform::Identity();
	// Fallback to entity's own transform if no children component or tag
	// missing — keeps "spawn projectile at child world position" callers
	// safe (they get the unit's transform, not a zero transform).
	const FSeinChildTransformsComponent* Data = Sub->GetComponent<FSeinChildTransformsComponent>(EntityHandle);
	if (!Data) return Entity->Transform;
	const FSeinChildTransform* Found = FindByTag(Data->Children, Tag);
	if (!Found) return Entity->Transform;
	return ComposeWorldFromChain(Data->Children, Found, Entity->Transform);
}

FGameplayTag USeinChildTransformsBPFL::SeinGetChildParentTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return FGameplayTag();
	const FSeinChildTransformsComponent* Data = Sub->GetComponent<FSeinChildTransformsComponent>(EntityHandle);
	if (!Data) return FGameplayTag();
	if (const FSeinChildTransform* Found = FindByTag(Data->Children, Tag))
	{
		return Found->ParentTag;
	}
	return FGameplayTag();
}

TArray<FSeinChildTransform> USeinChildTransformsBPFL::SeinGetDirectChildrenOf(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag ParentTag)
{
	TArray<FSeinChildTransform> Result;
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return Result;
	const FSeinChildTransformsComponent* Data = Sub->GetComponent<FSeinChildTransformsComponent>(EntityHandle);
	if (!Data) return Result;
	// Filter by ParentTag. Invalid tag = root-level children of the entity.
	for (const FSeinChildTransform& N : Data->Children)
	{
		if (N.ParentTag == ParentTag) Result.Add(N);
	}
	return Result;
}

bool USeinChildTransformsBPFL::SeinGetChildTransformsData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinChildTransformsComponent& OutData)
{
	OutData = FSeinChildTransformsComponent{};
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return false;
	const FSeinChildTransformsComponent* Data = Sub->GetComponent<FSeinChildTransformsComponent>(EntityHandle);
	if (!Data) return false;
	OutData = *Data;
	return true;
}

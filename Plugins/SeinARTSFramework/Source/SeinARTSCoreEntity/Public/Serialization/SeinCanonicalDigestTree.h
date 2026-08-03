/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCanonicalDigestTree.h
 * @brief   Batched canonical BLAKE3-128 Merkle tree for live-state leaves.
 */

#pragma once

#include "CoreMinimal.h"

/**
 * Non-authoritative acceleration structure for a fixed indexed leaf space.
 *
 * Leaf positions are meaningful (entity/component/pool slot indices). Empty
 * positions use a domain-bound sentinel. A batch may replace any number of
 * leaves, after which FinalizeUpdates hashes each affected ancestor at most
 * once. One changed leaf costs O(log N); a dense batch costs O(N), never
 * O(changes * log N). The tree and its revisions are cache only and are never
 * serialized as simulation state.
 */
class SEINARTSCOREENTITY_API FSeinCanonicalDigestTree
{
public:
	/** Reset to LogicalLeafCount positions and build the canonical empty tree. */
	bool Reset(
		FStringView InStableDomain,
		int32 LogicalLeafCount,
		FString& OutError);

	/** Queue one leaf replacement. An invalid digest means an empty position. */
	bool SetLeafDigest(
		int32 LeafIndex,
		const FGuid& Digest,
		FString& OutError);

	/** Hash every dirty ancestor once, bottom-up, and seal the current root. */
	bool FinalizeUpdates(FString& OutError);

	/** Root of the last completed Reset/FinalizeUpdates operation. */
	const FGuid& GetRoot() const { return RootDigest; }
	int32 Num() const { return LeafCount; }
	bool IsValid() const { return RootDigest.IsValid(); }
	bool HasPendingUpdates() const { return bHasPendingUpdates; }

private:
	bool ComputeNodeDigest(int32 NodeIndex, FGuid& OutDigest, FString& OutError) const;

	FString StableDomain;
	int32 LeafCount = 0;
	int32 LeafBase = 0;
	TArray<FGuid> Nodes;
	TBitArray<> DirtyNodes;
	FGuid EmptyLeafDigest;
	FGuid RootDigest;
	bool bHasPendingUpdates = false;
};

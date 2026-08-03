/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCanonicalDigestTree.cpp
 */

#include "Serialization/SeinCanonicalDigestTree.h"

#include "Serialization/SeinCanonicalInitialStateDigest.h"

namespace
{
	bool Fail(FString& OutError, const FString& Message)
	{
		OutError = Message;
		return false;
	}
}

bool FSeinCanonicalDigestTree::Reset(
	FStringView InStableDomain,
	int32 LogicalLeafCount,
	FString& OutError)
{
	OutError.Reset();
	RootDigest.Invalidate();
	bHasPendingUpdates = false;
	if (InStableDomain.IsEmpty() || LogicalLeafCount < 0)
	{
		return Fail(
			OutError,
			TEXT("Canonical digest tree requires a stable domain and non-negative leaf count."));
	}

	StableDomain = FString(InStableDomain);
	LeafCount = LogicalLeafCount;
	LeafBase = 1;
	const int32 RequiredLeaves = FMath::Max(1, LogicalLeafCount);
	while (LeafBase < RequiredLeaves)
	{
		if (LeafBase > MAX_int32 / 2)
		{
			return Fail(
				OutError,
				TEXT("Canonical digest tree leaf capacity overflowed."));
		}
		LeafBase *= 2;
	}
	if (LeafBase > MAX_int32 / 2)
	{
		return Fail(
			OutError,
			TEXT("Canonical digest tree node capacity overflowed."));
	}

	FSeinCanonicalDigestWriter EmptyWriter(
		TEXT("SeinARTS.LiveWorld.Merkle.Empty"), 1);
	if (!EmptyWriter.WriteString(StableDomain)
		|| !EmptyWriter.Finalize(EmptyLeafDigest, OutError))
	{
		return false;
	}

	Nodes.Init(FGuid(), LeafBase * 2);
	DirtyNodes.Init(false, Nodes.Num());
	for (int32 Index = LeafBase; Index < Nodes.Num(); ++Index)
	{
		Nodes[Index] = EmptyLeafDigest;
	}
	for (int32 NodeIndex = LeafBase - 1; NodeIndex >= 1; --NodeIndex)
	{
		if (!ComputeNodeDigest(NodeIndex, Nodes[NodeIndex], OutError))
		{
			RootDigest.Invalidate();
			return false;
		}
	}

	FSeinCanonicalDigestWriter RootWriter(
		TEXT("SeinARTS.LiveWorld.Merkle.Root"), 1);
	if (!RootWriter.WriteString(StableDomain)
		|| !RootWriter.WriteInt32(LeafCount)
		|| !RootWriter.WriteInt32(LeafBase)
		|| !RootWriter.WriteGuid(Nodes[1])
		|| !RootWriter.Finalize(RootDigest, OutError))
	{
		RootDigest.Invalidate();
		return false;
	}
	return true;
}

bool FSeinCanonicalDigestTree::SetLeafDigest(
	int32 LeafIndex,
	const FGuid& Digest,
	FString& OutError)
{
	if (LeafIndex < 0 || LeafIndex >= LeafCount
		|| LeafBase <= 0 || Nodes.Num() != LeafBase * 2)
	{
		return Fail(
			OutError,
			FString::Printf(
				TEXT("Canonical digest tree rejected leaf index %d for %d leaves."),
				LeafIndex,
				LeafCount));
	}

	const int32 NodeIndex = LeafBase + LeafIndex;
	const FGuid& Candidate = Digest.IsValid() ? Digest : EmptyLeafDigest;
	if (Nodes[NodeIndex] == Candidate)
	{
		return true;
	}
	Nodes[NodeIndex] = Candidate;
	for (int32 Parent = NodeIndex / 2; Parent >= 1; Parent /= 2)
	{
		DirtyNodes[Parent] = true;
		if (Parent == 1)
		{
			break;
		}
	}
	bHasPendingUpdates = true;
	return true;
}

bool FSeinCanonicalDigestTree::FinalizeUpdates(FString& OutError)
{
	OutError.Reset();
	if (!bHasPendingUpdates)
	{
		return RootDigest.IsValid()
			? true
			: Fail(OutError, TEXT("Canonical digest tree has no sealed root."));
	}

	for (int32 NodeIndex = LeafBase - 1; NodeIndex >= 1; --NodeIndex)
	{
		if (!DirtyNodes[NodeIndex])
		{
			continue;
		}
		if (!ComputeNodeDigest(NodeIndex, Nodes[NodeIndex], OutError))
		{
			RootDigest.Invalidate();
			return false;
		}
		DirtyNodes[NodeIndex] = false;
	}

	FSeinCanonicalDigestWriter RootWriter(
		TEXT("SeinARTS.LiveWorld.Merkle.Root"), 1);
	if (!RootWriter.WriteString(StableDomain)
		|| !RootWriter.WriteInt32(LeafCount)
		|| !RootWriter.WriteInt32(LeafBase)
		|| !RootWriter.WriteGuid(Nodes[1])
		|| !RootWriter.Finalize(RootDigest, OutError))
	{
		RootDigest.Invalidate();
		return false;
	}
	bHasPendingUpdates = false;
	return true;
}

bool FSeinCanonicalDigestTree::ComputeNodeDigest(
	int32 NodeIndex,
	FGuid& OutDigest,
	FString& OutError) const
{
	const int32 Left = NodeIndex * 2;
	const int32 Right = Left + 1;
	if (NodeIndex <= 0 || Right >= Nodes.Num())
	{
		return Fail(
			OutError,
			TEXT("Canonical digest tree encountered an invalid parent node."));
	}

	FSeinCanonicalDigestWriter Writer(
		TEXT("SeinARTS.LiveWorld.Merkle.Parent"), 1);
	return Writer.WriteString(StableDomain)
		&& Writer.WriteInt32(NodeIndex)
		&& Writer.WriteGuid(Nodes[Left])
		&& Writer.WriteGuid(Nodes[Right])
		&& Writer.Finalize(OutDigest, OutError);
}

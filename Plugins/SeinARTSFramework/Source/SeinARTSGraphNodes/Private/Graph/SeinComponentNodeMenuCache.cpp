/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinComponentNodeMenuCache.cpp
 */

#include "Graph/SeinComponentNodeMenuCache.h"

#include "CoreGlobals.h"                       // GFrameCounter
#include "UObject/UObjectIterator.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Components/SeinPayload.h"
#include "Components/SeinComponentEligibility.h"

namespace
{
	// "Valid entity component" eligibility is defined once in CoreEntity
	// (SeinComponentEligibility::IsEntityComponentStruct) and shared with the
	// editor bridge-picker filter — the menu and picker can't drift.

	// Per-rebuild memo. Weak ptrs so a UDS GC'd between rebuilds resolves to null
	// and is skipped rather than dangling; self-cleaning at module unload.
	TArray<TWeakObjectPtr<UScriptStruct>> GCachedCandidates;
	uint64 GCacheFrame = TNumericLimits<uint64>::Max();
	bool   GCacheValid = false;

	void RebuildCandidates()
	{
		GCachedCandidates.Reset();

		// Eligibility is the shared CoreEntity rule (matches the bridge picker):
		// native FSeinPayload children + UDS carrying the SeinEntityComponent
		// meta, minus SeinSubData. TObjectIterator sees only LOADED structs.
		const UScriptStruct* Base = FSeinPayload::StaticStruct();
		for (TObjectIterator<UScriptStruct> It; It; ++It)
		{
			UScriptStruct* S = *It;
			if (!S || S == Base) continue;
			if (SeinComponentEligibility::IsEntityComponentStruct(S))
			{
				GCachedCandidates.Add(S);
			}
		}

		// Same sort key the nodes used (case-insensitive FName lexical order), done
		// once here so consumers don't re-sort. All entries are live during the
		// rebuild, so the Get() calls resolve; the null guards are defensive.
		GCachedCandidates.Sort([](const TWeakObjectPtr<UScriptStruct>& A, const TWeakObjectPtr<UScriptStruct>& B)
		{
			const UScriptStruct* SA = A.Get();
			const UScriptStruct* SB = B.Get();
			if (SA == SB) return false;
			if (!SA) return false;
			if (!SB) return true;
			return SA->GetFName().LexicalLess(SB->GetFName());
		});

		GCacheFrame = GFrameCounter;
		GCacheValid = true;
	}
}

void SeinComponentNodeMenu::GetCandidateStructs(TArray<UScriptStruct*>& Out)
{
	// Rebuild on the first call of a new frame; reuse within the same frame (the
	// two nodes are visited within one synchronous action-DB rebuild). Every
	// distinct rebuild lands on a later frame, so the set is always re-derived
	// from the live object graph — identical output to an unconditional scan.
	if (!GCacheValid || GCacheFrame != GFrameCounter)
	{
		RebuildCandidates();
	}

	Out.Reset();
	Out.Reserve(GCachedCandidates.Num());
	for (const TWeakObjectPtr<UScriptStruct>& Weak : GCachedCandidates)
	{
		if (UScriptStruct* S = Weak.Get())
		{
			Out.Add(S);
		}
	}
}

void SeinComponentNodeMenu::ResetCandidateCache()
{
	GCachedCandidates.Reset();
	GCacheFrame = TNumericLimits<uint64>::Max();
	GCacheValid = false;
}

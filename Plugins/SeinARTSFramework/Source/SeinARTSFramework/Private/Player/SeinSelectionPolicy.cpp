/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinSelectionPolicy.cpp
 * @author       RJ Macklem
 * @created      5 Sep 2026
 * @latest       5 Sep 2026
 * @brief        Selection admission independent of rendering collision and command targeting.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */
#include "Player/SeinSelectionPolicy.h"
#include "Actor/SeinActor.h"
#include "Components/SeinExtentsPayload.h"
#include "Components/SeinSquadMemberPayload.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"

ASeinActor* SeinSelectionPolicy::ResolveActor(UWorld& World, ASeinActor* Actor)
{
	const USeinWorldSubsystem* Sim = World.GetSubsystem<USeinWorldSubsystem>();
	const USeinActorBridgeSubsystem* Bridge = World.GetSubsystem<USeinActorBridgeSubsystem>();
	if (!Sim || !Bridge) return nullptr;
	TSet<FSeinEntityHandle> Visited;
	while (IsValid(Actor) && Actor->GetWorld() == &World)
	{
		const FSeinEntityHandle Handle = Actor->GetEntityHandle();
		if (!Sim->IsEntityAlive(Handle) || Visited.Contains(Handle)
			|| Bridge->GetActorForEntity(Handle) != Actor) return nullptr;
		const FSeinSquadMemberPayload* Member = Sim->GetComponent<FSeinSquadMemberPayload>(Handle);
		if (!Member || !Member->SquadEntity.IsValid()) return Actor;
		Visited.Add(Handle);
		Actor = Bridge->GetActorForEntity(Member->SquadEntity);
	}
	return nullptr;
}

ASeinActor* SeinSelectionPolicy::ResolveEligibleActor(UWorld& World, FSeinPlayerID Player,
	ASeinActor* Input, bool bDrag)
{
	if (!IsValid(Input) || Input->IsHidden()) return nullptr;
	ASeinActor* Actor = ResolveActor(World, Input);
	if (!Actor || Actor->IsHidden()) return nullptr;
	const USeinWorldSubsystem* Sim = World.GetSubsystem<USeinWorldSubsystem>();
	const FSeinEntityHandle Handle = Actor->GetEntityHandle();
	const FSeinEntity* Entity = Sim->GetEntity(Handle);
	const FSeinExtentsPayload* Extents = Sim->GetComponent<FSeinExtentsPayload>(Handle);
	if (!Entity || !Entity->IsSelectable() || Sim->GetEntityOwner(Handle) != Player
		|| !Extents || Extents->Shapes.IsEmpty()
		|| static_cast<uint8>(Extents->SelectionPolicy) >= static_cast<uint8>(ESeinSelectionPolicy::Disabled)
		|| (bDrag && !Extents->bIncludeInDragSelection)) return nullptr;
	return Actor;
}

TArray<ASeinActor*> SeinSelectionPolicy::Resolve(UWorld& World, FSeinPlayerID Player,
	const TArray<ASeinActor*>& Current, const TArray<ASeinActor*>& Candidates, bool bDrag)
{
	TArray<ASeinActor*> Out;
	const USeinWorldSubsystem* Sim = World.GetSubsystem<USeinWorldSubsystem>();
	if (!Sim) return Out;
	struct FCandidate
	{
		ASeinActor* Actor;
		FSeinEntityHandle Handle;
		const FSeinExtentsPayload* Extents;
	};
	TSet<FSeinEntityHandle> Seen;
	const auto MakeCandidate = [&](ASeinActor* Input, bool bNewDrag, TArray<FCandidate>& Into)
	{
		ASeinActor* Actor = ResolveEligibleActor(World, Player, Input, bNewDrag);
		if (!Actor) return;
		const FSeinEntityHandle Handle = Actor->GetEntityHandle();
		const FSeinExtentsPayload* Extents = Sim->GetComponent<FSeinExtentsPayload>(Handle);
		if (!Seen.Contains(Handle))
		{
			Seen.Add(Handle);
			Into.Add({Actor, Handle, Extents});
		}
	};
	const auto SameGroup = [](const FCandidate& A, const FCandidate& B)
	{
		if (A.Extents->SelectionGroup.IsValid() || B.Extents->SelectionGroup.IsValid())
		{
			return A.Extents->SelectionGroup.IsValid() && A.Extents->SelectionGroup == B.Extents->SelectionGroup;
		}
		return A.Actor->GetClass() == B.Actor->GetClass();
	};
	TOptional<FCandidate> First;
	bool bAllSameGroup = true;
	bool bHasLikeRestriction = false;
	bool bHasSingleRestriction = false;
	TSet<FSeinEntityHandle> Accepted;
	const auto Admit = [&](const FCandidate& Candidate)
	{
		if (Accepted.Contains(Candidate.Handle)) return;
		const bool bLike = Candidate.Extents->SelectionPolicy == ESeinSelectionPolicy::LikeUnitsOnly;
		const bool bSingle = Candidate.Extents->SelectionPolicy == ESeinSelectionPolicy::SingleOnly;
		if (First.IsSet())
		{
			if (bHasSingleRestriction || bSingle) return;
			const bool bMatches = SameGroup(First.GetValue(), Candidate);
			if ((bHasLikeRestriction && !bMatches) || (bLike && (!bAllSameGroup || !bMatches))) return;
			bAllSameGroup &= bMatches;
		}
		else First = Candidate;
		bHasLikeRestriction |= bLike;
		bHasSingleRestriction |= bSingle;
		Accepted.Add(Candidate.Handle);
		Out.Add(Candidate.Actor);
	};
	TArray<FCandidate> Existing;
	for (ASeinActor* Actor : Current) MakeCandidate(Actor, false, Existing);
	for (const FCandidate& Candidate : Existing) Admit(Candidate);
	Seen.Reset();
	TArray<FCandidate> Incoming;
	for (ASeinActor* Actor : Candidates) MakeCandidate(Actor, bDrag, Incoming);
	Incoming.Sort([](const FCandidate& A, const FCandidate& B)
	{
		if (A.Extents->SelectionPriority != B.Extents->SelectionPriority)
			return A.Extents->SelectionPriority > B.Extents->SelectionPriority;
		return A.Handle < B.Handle;
	});
	for (const FCandidate& Candidate : Incoming) Admit(Candidate);
	return Out;
}

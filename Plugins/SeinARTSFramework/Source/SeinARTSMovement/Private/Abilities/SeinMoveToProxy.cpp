/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoveToProxy.cpp
 */

#include "Abilities/SeinMoveToProxy.h"
#include "Actions/SeinMoveToAction.h"
#include "SeinNavigationSubsystem.h"

#include "Abilities/SeinAbility.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"

USeinMoveToProxy* USeinMoveToProxy::SeinMoveTo(USeinAbility* Ability, FFixedVector Destination)
{
	USeinMoveToProxy* Proxy = NewObject<USeinMoveToProxy>();
	Proxy->CachedAbility = Ability;
	Proxy->CachedDestination = Destination;
	return Proxy;
}

void USeinMoveToProxy::Activate()
{
	if (!CachedAbility)
	{
		BroadcastFailure(ESeinMoveFailureReason::EntityDestroyed);
		return;
	}

	UWorld* World = CachedAbility->GetWorld();
	if (!World)
	{
		BroadcastFailure(ESeinMoveFailureReason::EntityDestroyed);
		return;
	}

	USeinWorldSubsystem* SimWorld = World->GetSubsystem<USeinWorldSubsystem>();
	if (!SimWorld || !SimWorld->LatentActionManager)
	{
		BroadcastFailure(ESeinMoveFailureReason::NoNavigation);
		return;
	}
	if (!SimWorld->RequireStateMutationAuthorization(TEXT("MoveTo")))
	{
		BroadcastFailure(ESeinMoveFailureReason::InvalidExecutionContext);
		return;
	}

	if (!USeinNavigationSubsystem::GetNavigationForWorld(World))
	{
		BroadcastFailure(ESeinMoveFailureReason::NoNavigation);
		return;
	}

	USeinMoveToAction* Action = NewObject<USeinMoveToAction>(this);
	Action->OwningAbility = CachedAbility;
	Action->OwnerEntity = CachedAbility->OwnerEntity;
	Action->Observer = this;
	Action->Initialize(CachedDestination);

	if (!SimWorld->LatentActionManager->RegisterAction(Action))
	{
		Action->Observer.Reset();
		BroadcastFailure(
			ESeinMoveFailureReason::InvalidExecutionContext);
		return;
	}
	RunningAction = Action;
}

void USeinMoveToProxy::NotifyCompleted()
{
	OnCompleted.Broadcast(FSeinMoveToResult());
	ReleaseAfterTerminal();
}

void USeinMoveToProxy::NotifyFailed(ESeinMoveFailureReason Reason)
{
	FSeinMoveToResult Result;
	Result.FailureReason = Reason;
	OnFailed.Broadcast(Result);
	ReleaseAfterTerminal();
}

void USeinMoveToProxy::NotifyWaypointReached(int32 Index, int32 Total)
{
	FSeinMoveToResult Result;
	Result.WaypointIndex = Index;
	Result.TotalWaypoints = Total;
	OnWaypointReached.Broadcast(Result);
}

void USeinMoveToProxy::NotifyCancelled()
{
	FSeinMoveToResult Result;
	Result.FailureReason = ESeinMoveFailureReason::Cancelled;
	OnCancelled.Broadcast(Result);
	ReleaseAfterTerminal();
}

void USeinMoveToProxy::NotifyPartialPath()
{
	// Non-terminal — the move continues toward the partial endpoint, and
	// OnCompleted fires on arrival as usual. Don't SetReadyToDestroy here.
	OnPartialPath.Broadcast(FSeinMoveToResult());
}

void USeinMoveToProxy::NotifyPathRecomputed()
{
	// Non-terminal — the move continues on the freshly recomputed route.
	OnPathRecomputed.Broadcast(FSeinMoveToResult());
}

void USeinMoveToProxy::AbandonForSnapshotRestore()
{
	ReleaseAfterTerminal();
}

void USeinMoveToProxy::ReleaseAfterTerminal()
{
	OnCompleted.Clear();
	OnFailed.Clear();
	OnWaypointReached.Clear();
	OnCancelled.Clear();
	OnPartialPath.Clear();
	OnPathRecomputed.Clear();
	RunningAction = nullptr;
	CachedAbility = nullptr;
	SetReadyToDestroy();
}

void USeinMoveToProxy::BroadcastFailure(ESeinMoveFailureReason Reason)
{
	FSeinMoveToResult Result;
	Result.FailureReason = Reason;
	OnFailed.Broadcast(Result);
	ReleaseAfterTerminal();
}

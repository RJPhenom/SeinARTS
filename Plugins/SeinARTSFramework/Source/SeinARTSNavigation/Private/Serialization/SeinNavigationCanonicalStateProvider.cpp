/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationCanonicalStateProvider.cpp
 * @brief   Reload-safe capture and restore of deferred pathfinding work.
 */

#include "Serialization/SeinNavigationCanonicalStateProvider.h"

#include "Serialization/SeinNavigationCanonicalState.h"
#include "SeinNavigationSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace
{
	const FName OwnerModuleId(TEXT("SeinARTSNavigation"));

	bool IsStrictlyAfter(
		const FSeinEntityHandle& Previous,
		const FSeinEntityHandle& Current)
	{
		return Previous < Current;
	}

	bool ValidateRequestOrder(
		TConstArrayView<FSeinPathRequest> Requests,
		const TCHAR* Lane,
		FString& OutError)
	{
		FSeinEntityHandle Previous;
		for (int32 Index = 0; Index < Requests.Num(); ++Index)
		{
			const FSeinEntityHandle Requester = Requests[Index].Requester;
			if (!Requester.IsValid()
				|| (Index > 0 && !IsStrictlyAfter(Previous, Requester)))
			{
				OutError = FString::Printf(
					TEXT("Navigation %s requests must have valid, unique requesters in strict canonical order."),
					Lane);
				return false;
			}
			Previous = Requester;
		}
		return true;
	}

	bool ValidateState(
		const FSeinNavigationContinuationState& State,
		FString& OutError)
	{
		if (State.PathRequestsThisTick < 0
			|| State.LastResetTick < -1
			|| State.LastDrainTick < -1
			|| !ValidateRequestOrder(
				State.QueuedRequests, TEXT("queued"), OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Navigation continuation counters are invalid.");
			}
			return false;
		}

		FSeinEntityHandle Previous;
		for (int32 Index = 0; Index < State.ReadyResults.Num(); ++Index)
		{
			const FSeinEntityHandle Requester =
				State.ReadyResults[Index].Request.Requester;
			if (!Requester.IsValid()
				|| (Index > 0 && !IsStrictlyAfter(Previous, Requester)))
			{
				OutError =
					TEXT("Navigation ready results must have valid, unique requesters in strict canonical order.");
				return false;
			}
			Previous = Requester;
		}
		return true;
	}

	struct FNavigationRestoreStage final
		: ISeinCanonicalStateRestoreStage
	{
		FSeinNavigationContinuationState State;
	};
}

struct FSeinNavigationCanonicalStateProvider
{
	static bool Capture(
		const FSeinCanonicalStateCaptureContext& Context,
		FInstancedStruct& OutState,
		FString& OutError)
	{
		const UWorld* UnrealWorld = Context.World.GetWorld();
		const USeinNavigationSubsystem* NavigationSubsystem =
			UnrealWorld
				? UnrealWorld->GetSubsystem<USeinNavigationSubsystem>()
				: nullptr;
		if (!NavigationSubsystem)
		{
			OutError =
				TEXT("Navigation canonical-state capture could not resolve its world subsystem.");
			return false;
		}

		FSeinNavigationContinuationState State;
		State.PathRequestsThisTick =
			NavigationSubsystem->PathRequestsThisTick;
		State.LastResetTick = NavigationSubsystem->LastResetTick;
		State.LastDrainTick = NavigationSubsystem->LastDrainTick;

		TArray<FSeinEntityHandle> QueueKeys;
		NavigationSubsystem->AsyncQueue.GetKeys(QueueKeys);
		QueueKeys.Sort();
		State.QueuedRequests.Reserve(QueueKeys.Num());
		for (const FSeinEntityHandle Requester : QueueKeys)
		{
			const FSeinPathRequest& Request =
				NavigationSubsystem->AsyncQueue.FindChecked(Requester);
			if (Request.Requester != Requester)
			{
				OutError =
					TEXT("Navigation async queue key and request identity disagree.");
				return false;
			}
			State.QueuedRequests.Add(Request);
		}

		TArray<FSeinEntityHandle> ResultKeys;
		NavigationSubsystem->AsyncResults.GetKeys(ResultKeys);
		ResultKeys.Sort();
		State.ReadyResults.Reserve(ResultKeys.Num());
		for (const FSeinEntityHandle Requester : ResultKeys)
		{
			const USeinNavigationSubsystem::FSeinAsyncPathResult& Result =
				NavigationSubsystem->AsyncResults.FindChecked(Requester);
			if (Result.Request.Requester != Requester)
			{
				OutError =
					TEXT("Navigation async result key and request identity disagree.");
				return false;
			}
			FSeinNavigationAsyncResultState& Record =
				State.ReadyResults.AddDefaulted_GetRef();
			Record.Request = Result.Request;
			Record.Path = Result.Path;
		}

		if (!ValidateState(State, OutError))
		{
			return false;
		}
		OutState = FInstancedStruct::Make(MoveTemp(State));
		return true;
	}

	static bool StageRestore(
		const FSeinCanonicalStateStageContext&,
		const FInstancedStruct& State,
		TUniquePtr<ISeinCanonicalStateRestoreStage>& OutStage,
		FString& OutError)
	{
		const FSeinNavigationContinuationState* Payload =
			State.GetPtr<FSeinNavigationContinuationState>();
		if (!Payload || !ValidateState(*Payload, OutError))
		{
			if (!Payload)
			{
				OutError =
					TEXT("Navigation canonical-state payload has the wrong root type.");
			}
			return false;
		}

		TUniquePtr<FNavigationRestoreStage> Stage =
			MakeUnique<FNavigationRestoreStage>();
		Stage->State = *Payload;
		OutStage = MoveTemp(Stage);
		return true;
	}

	static void CommitRestore(
		FSeinCanonicalStateCommitContext& Context,
		TUniquePtr<ISeinCanonicalStateRestoreStage>&& OpaqueStage)
	{
		FNavigationRestoreStage* Stage =
			static_cast<FNavigationRestoreStage*>(OpaqueStage.Get());
		check(Stage);
		UWorld* UnrealWorld = Context.World.GetWorld();
		USeinNavigationSubsystem* NavigationSubsystem =
			UnrealWorld
				? UnrealWorld->GetSubsystem<USeinNavigationSubsystem>()
				: nullptr;
		check(NavigationSubsystem);

		NavigationSubsystem->PathRequestsThisTick =
			Stage->State.PathRequestsThisTick;
		NavigationSubsystem->LastResetTick =
			Stage->State.LastResetTick;
		NavigationSubsystem->LastDrainTick =
			Stage->State.LastDrainTick;
		NavigationSubsystem->AsyncQueue.Reset();
		NavigationSubsystem->AsyncResults.Reset();

		for (const FSeinPathRequest& Request :
			Stage->State.QueuedRequests)
		{
			NavigationSubsystem->AsyncQueue.Add(
				Request.Requester, Request);
		}
		for (const FSeinNavigationAsyncResultState& Record :
			Stage->State.ReadyResults)
		{
			USeinNavigationSubsystem::FSeinAsyncPathResult Result;
			Result.Request = Record.Request;
			Result.Path = Record.Path;
			NavigationSubsystem->AsyncResults.Add(
				Record.Request.Requester, MoveTemp(Result));
		}
	}
};

FSeinCanonicalStateRegistrationHandle
SeinRegisterNavigationCanonicalStateProvider(FString& OutError)
{
	FSeinCanonicalStateDescriptor Descriptor;
	Descriptor.Key.StableDomainId = TEXT("seinarts.navigation");
	Descriptor.Key.StableContributorId =
		TEXT("async-path-continuation");
	Descriptor.SchemaVersion = 1;
	Descriptor.ImplementationRevision = 1;
	Descriptor.Role = ESeinCanonicalStateRole::Continuation;
	Descriptor.PayloadStruct =
		FSeinNavigationContinuationState::StaticStruct();
	Descriptor.Limits.MaxRecursionDepth = 64;
	Descriptor.Limits.MaxEncodedBytes = 16 * 1024 * 1024;
	Descriptor.Limits.MaxAggregateElements = 1024 * 1024;

	FSeinCanonicalStateContributorOps Ops;
	Ops.Capture = &FSeinNavigationCanonicalStateProvider::Capture;
	Ops.StageRestore =
		&FSeinNavigationCanonicalStateProvider::StageRestore;
	Ops.CommitRestore =
		&FSeinNavigationCanonicalStateProvider::CommitRestore;
	return FSeinCanonicalStateRegistry::Register(
		OwnerModuleId, Descriptor, MoveTemp(Ops), &OutError);
}

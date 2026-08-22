/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         OnlineServicesTestTypes.cpp
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Implements hostile timing doubles for Online Services lifecycle tests.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Online/OnlineServicesTestTypes.h"

#include "Async/Async.h"
#include "HAL/PlatformProcess.h"

namespace
{
	FSeinOnlineProviderResponse MakeAuthenticationSuccess(
		const FSeinOnlineProviderRequest& Request,
		const TCHAR* AccountValue,
		const TCHAR* DisplayName)
	{
		FSeinOnlineAuthenticateResult Result;
		Result.Account.AccountID.Provider = TEXT("DeferredTest");
		Result.Account.AccountID.Value = AccountValue;
		Result.Account.DisplayName = DisplayName;
		Result.Account.LocalUserIndex = 0;

		FSeinOnlineProviderResponse Response;
		Response.Handle = Request.Handle;
		Response.Operation = Request.Operation;
		Response.Payload = FInstancedStruct::Make(MoveTemp(Result));
		return Response;
	}
}

FName USeinOnlineDeferredTestProvider::GetProviderName() const
{
	return TEXT("DeferredTest");
}

bool USeinOnlineDeferredTestProvider::SupportsOperation(
	ESeinOnlineOperation Operation) const
{
	return Operation == ESeinOnlineOperation::Authenticate;
}

void USeinOnlineDeferredTestProvider::BeginRequestProvider(
	const FSeinOnlineProviderRequest& Request,
	FSeinOnlineProviderCompletion&& Completion)
{
	FPendingRequest& Pending = PendingRequests.AddDefaulted_GetRef();
	Pending.Request = Request;
	Pending.Completion = MoveTemp(Completion);
}

void USeinOnlineDeferredTestProvider::ShutdownProvider()
{
	PendingRequests.Reset();
	WaitForWorkerTasks();
}

void USeinOnlineDeferredTestProvider::WaitForWorkerTasks()
{
	for (TFuture<void>& Worker : WorkerTasks)
	{
		Worker.Wait();
	}
	WorkerTasks.Reset();
}

bool USeinOnlineDeferredTestProvider::CancelRequest(
	FSeinOnlineRequestHandle Handle)
{
	const int32 Removed = PendingRequests.RemoveAll(
		[Handle](const FPendingRequest& Pending)
		{
			return Pending.Request.Handle == Handle;
		});
	return Removed == 1;
}

void USeinOnlineDeferredTestProvider::CompleteAllSuccessfully()
{
	TArray<FPendingRequest> Local = MoveTemp(PendingRequests);
	PendingRequests.Reset();
	for (FPendingRequest& Pending : Local)
	{
		Pending.Completion(MakeAuthenticationSuccess(
			Pending.Request, TEXT("deferred-account"), TEXT("Deferred Account")));
	}
}

void USeinOnlineDeferredTestProvider::CompleteAllSuccessfullyTwice()
{
	TArray<FPendingRequest> Local = MoveTemp(PendingRequests);
	PendingRequests.Reset();
	for (FPendingRequest& Pending : Local)
	{
		Pending.Completion(MakeAuthenticationSuccess(
			Pending.Request, TEXT("duplicate-account"), TEXT("Duplicate Account")));
		Pending.Completion(MakeAuthenticationSuccess(
			Pending.Request, TEXT("duplicate-account"), TEXT("Duplicate Account")));
	}
}

void USeinOnlineDeferredTestProvider::CompleteAllSuccessfullyOnWorkerThread()
{
	TArray<FPendingRequest> Local = MoveTemp(PendingRequests);
	PendingRequests.Reset();
	WorkerTasks.Add(Async(EAsyncExecution::ThreadPool,
		[Local = MoveTemp(Local)]() mutable
		{
			FPlatformProcess::SleepNoStats(0.02f);
			for (FPendingRequest& Pending : Local)
			{
				Pending.Completion(MakeAuthenticationSuccess(
					Pending.Request,
					TEXT("threaded-account"),
					TEXT("Threaded Account")));
			}
		}));
}

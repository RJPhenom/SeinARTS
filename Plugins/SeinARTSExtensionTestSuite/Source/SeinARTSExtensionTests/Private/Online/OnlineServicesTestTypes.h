/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         OnlineServicesTestTypes.h
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       22 Aug 2026
 * @brief        Declares hostile timing doubles for Online Services lifecycle tests.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "Async/Future.h"
#include "HAL/Event.h"
#include "Provider/SeinOnlineServicesProvider.h"
#include "OnlineServicesTestTypes.generated.h"

/** Provider double for cancellation, duplicate, threaded, and shutdown races. */
UCLASS()
class USeinOnlineDeferredTestProvider final
	: public USeinOnlineServicesProvider
{
	GENERATED_BODY()

public:
	virtual FName GetProviderName() const override;
	virtual bool SupportsOperation(ESeinOnlineOperation Operation) const override;
	virtual bool CancelRequest(FSeinOnlineRequestHandle Handle) override;

	/** Fires every retained callback with a schema-valid success response. */
	void CompleteAllSuccessfully();

	/** Hostile provider behavior: invokes every callback twice. */
	void CompleteAllSuccessfullyTwice();

	/** Blocks a worker with retained callbacks until shutdown or an explicit join. */
	bool CompleteAllSuccessfullyOnWorkerThread();

	/** Joins and clears all worker tasks started by this test provider. */
	void WaitForWorkerTasks();

	/** Number of callbacks still retained by the provider. */
	int32 GetPendingRequestCount() const { return PendingRequests.Num(); }

private:
	virtual void BeginRequestProvider(
		const FSeinOnlineProviderRequest& Request,
		FSeinOnlineProviderCompletion&& Completion) override;
	virtual void ShutdownProvider() override;

	struct FPendingRequest
	{
		FSeinOnlineProviderRequest Request;
		FSeinOnlineProviderCompletion Completion;
	};

	TArray<FPendingRequest> PendingRequests;
	TArray<TFuture<void>> WorkerTasks;
	FSharedEventRef WorkerStartedEvent { EEventMode::ManualReset };
	FSharedEventRef WorkerCompletionGate { EEventMode::ManualReset };
};

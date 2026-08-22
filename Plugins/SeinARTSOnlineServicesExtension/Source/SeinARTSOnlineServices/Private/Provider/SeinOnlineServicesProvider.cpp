/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinOnlineServicesProvider.cpp
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Implements conservative defaults for SOS backend adapters.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Provider/SeinOnlineServicesProvider.h"

#include "Subsystem/SeinOnlineServicesSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinOnlineServicesProvider, Log, All);

struct FSeinOnlineProviderLifecycleState
{
	TAtomic<bool> bShuttingDown { false };
	TAtomic<int32> OutstandingCompletions { 0 };
};

namespace
{
	struct FSeinOnlineProviderCompletionLease
	{
		explicit FSeinOnlineProviderCompletionLease(
			TSharedRef<FSeinOnlineProviderLifecycleState> InState)
			: State(MoveTemp(InState))
		{
			State->OutstandingCompletions.IncrementExchange();
		}

		~FSeinOnlineProviderCompletionLease()
		{
			State->OutstandingCompletions.DecrementExchange();
		}

		TSharedRef<FSeinOnlineProviderLifecycleState> State;
	};
}

bool USeinOnlineServicesProvider::Initialize(
	USeinOnlineServicesSubsystem& InOwner,
	FSeinOnlineError& OutError)
{
	if (LifecycleState.IsValid())
	{
		OutError = FSeinOnlineError::Make(
			ESeinOnlineErrorCode::Conflict,
			TEXT("The online-services provider is already initialized."));
		return false;
	}
	LifecycleState = MakeShared<FSeinOnlineProviderLifecycleState>();
	Owner = &InOwner;
	OutError = FSeinOnlineError();
	if (InitializeProvider(OutError))
	{
		return true;
	}
	if (OutError.IsSuccess())
	{
		OutError = FSeinOnlineError::Make(
			ESeinOnlineErrorCode::ProviderUnavailable,
			TEXT("The online-services provider could not initialize."),
			true);
	}
	Shutdown();
	return false;
}

void USeinOnlineServicesProvider::Shutdown()
{
	if (!LifecycleState.IsValid())
	{
		Owner.Reset();
		return;
	}

	LifecycleState->bShuttingDown.Store(true);
	ShutdownProvider();
	const int32 Outstanding =
		LifecycleState->OutstandingCompletions.Load();
	if (Outstanding != 0)
	{
		UE_LOG(LogSeinOnlineServicesProvider, Fatal,
			TEXT("Online-services provider ShutdownProvider returned with %d retained or active completion(s). Provider shutdown must synchronously quiesce callbacks before module unload."),
			Outstanding);
	}
	LifecycleState.Reset();
	Owner.Reset();
}

bool USeinOnlineServicesProvider::InitializeProvider(FSeinOnlineError& OutError)
{
	OutError = FSeinOnlineError();
	return true;
}

void USeinOnlineServicesProvider::BeginRequest(
	const FSeinOnlineProviderRequest& Request,
	FSeinOnlineProviderCompletion&& Completion)
{
	if (!Completion)
	{
		return;
	}
	if (!LifecycleState.IsValid()
		|| LifecycleState->bShuttingDown.Load())
	{
		FSeinOnlineProviderResponse Response;
		Response.Handle = Request.Handle;
		Response.Operation = Request.Operation;
		Response.Error = FSeinOnlineError::Make(
			ESeinOnlineErrorCode::ProviderUnavailable,
			TEXT("The online-services provider is shutting down."),
			true);
		Completion(MoveTemp(Response));
		return;
	}

	TSharedRef<FSeinOnlineProviderLifecycleState> State =
		LifecycleState.ToSharedRef();
	TSharedRef<FSeinOnlineProviderCompletionLease> Lease =
		MakeShared<FSeinOnlineProviderCompletionLease>(State);
	BeginRequestProvider(
		Request,
		[State, Lease, Completion = MoveTemp(Completion)](
			FSeinOnlineProviderResponse&& Response) mutable
		{
			if (!State->bShuttingDown.Load())
			{
				Completion(MoveTemp(Response));
			}
		});
}

bool USeinOnlineServicesProvider::CancelRequest(FSeinOnlineRequestHandle)
{
	return false;
}

bool USeinOnlineServicesProvider::IsTrustedServerRequestAuthenticated(
	const FSeinOnlineProviderRequest&) const
{
	return false;
}

FSeinOnlineConnectionAdmissionDecision
USeinOnlineServicesProvider::AuthorizeConnection(
	const FSeinOnlineConnectionAdmissionRequest&)
{
	FSeinOnlineConnectionAdmissionDecision Decision;
	Decision.Error = FSeinOnlineError::Make(
		ESeinOnlineErrorCode::Unsupported,
		TEXT("The provider does not support authenticated connection admission."));
	return Decision;
}

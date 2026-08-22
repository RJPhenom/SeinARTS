/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinOnlineServicesSubsystem.cpp
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Implements bounded, deferred, generation-safe SOS request dispatch.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Subsystem/SeinOnlineServicesSubsystem.h"

#include "Contract/SeinOnlineServicesContract.h"
#include "Provider/SeinOnlineLoopbackProvider.h"
#include "Provider/SeinOnlineServicesProvider.h"
#include "Settings/SeinOnlineServicesSettings.h"
#include "Containers/Queue.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformAtomics.h"
#include "Online/CoreOnline.h"
#include "SeinConnectionAdmission.h"
#include "SeinNetSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinOnlineServices, Log, All);

namespace
{
	const FName OnlineAdmissionOwner(TEXT("SeinARTS.OnlineServices"));

	bool IsDevelopmentLoopbackPermitted(
		bool bSettingEnabled,
		bool bRequireAuthenticatedAdmission,
		bool bDedicatedServer,
		bool bShippingBuild)
	{
		return bSettingEnabled
			&& !bRequireAuthenticatedAdmission
			&& !bDedicatedServer
			&& !bShippingBuild;
	}

	void ResolveTransportIdentity(
		const FUniqueNetIdRepl& Identity,
		FString& OutType,
		FString& OutValue)
	{
		OutType.Reset();
		OutValue.Reset();
		if (!Identity.IsValid())
		{
			return;
		}
		OutValue = Identity.ToString();
		if (Identity.IsV1())
		{
			OutType = Identity.GetType().ToString();
		}
		else if (Identity.IsV2())
		{
			OutType = LexToString(
				Identity.GetV2Unsafe().GetOnlineServicesType());
		}
	}
}

struct FSeinOnlineQueuedCompletion
{
	uint64 ProviderGeneration = 0;
	FSeinOnlineProviderRequest Request;
	FSeinOnlineProviderResponse Response;
};

struct FSeinOnlineCompletionSink
{
	TAtomic<bool> bAccepting { true };
	TQueue<FSeinOnlineQueuedCompletion, EQueueMode::Mpsc> Queue;
};

void USeinOnlineServicesSubsystem::Initialize(
	FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<USeinNetSubsystem>();
	Super::Initialize(Collection);
	bReleased = false;
	const USeinOnlineServicesSettings* Settings =
		GetDefault<USeinOnlineServicesSettings>();
	MaxRequestRecords = FMath::Clamp(
		Settings ? Settings->MaxRequestRecords : 2048, 16, 65536);
	MaxRetainedCompletedRequests = FMath::Clamp(
		Settings ? Settings->MaxRetainedCompletedRequests : 512,
		1,
		MaxRequestRecords);
#if UE_BUILD_SHIPPING
	constexpr bool bShippingBuild = true;
#else
	constexpr bool bShippingBuild = false;
#endif
	bLoopbackPermitted = Settings && IsDevelopmentLoopbackPermitted(
		Settings->bAllowDevelopmentLoopback,
		Settings->bRequireAuthenticatedConnectionAdmission,
		IsRunningDedicatedServer(),
		bShippingBuild);
	CompletionSink = MakeShared<FSeinOnlineCompletionSink>();
	CompletionTicker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(
			this,
			&USeinOnlineServicesSubsystem::TickCompletions));

	FSeinOnlineError Error;
	CreateProvider(
		Settings ? Settings->ProviderClass : nullptr,
		/*bAllowFallback=*/bLoopbackPermitted,
		Error);
	if (!Error.IsSuccess())
	{
		UE_LOG(LogSeinOnlineServices, Error,
			TEXT("SOS provider initialization failed: %s"), *Error.Message);
	}
	if (Settings && Settings->bRequireAuthenticatedConnectionAdmission)
	{
		if (USeinNetSubsystem* Net =
			GetGameInstance()->GetSubsystem<USeinNetSubsystem>())
		{
			if (!Net->RegisterConnectionAdmissionAuthorizer(
					OnlineAdmissionOwner,
					FSeinConnectionAdmissionAuthorizer::CreateUObject(
						this,
						&USeinOnlineServicesSubsystem::AuthorizeIncomingConnection)))
			{
				UE_LOG(LogSeinOnlineServices, Error,
					TEXT("SOS could not register the required connection admission authority."));
			}
		}
	}
}

void USeinOnlineServicesSubsystem::Deinitialize()
{
	ReleaseModuleOwnedStateForModuleUnload();
	Super::Deinitialize();
}

void USeinOnlineServicesSubsystem::ReleaseModuleOwnedStateForModuleUnload()
{
	if (bReleased)
	{
		return;
	}
	bReleased = true;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USeinNetSubsystem* Net =
			GameInstance->GetSubsystem<USeinNetSubsystem>())
		{
			Net->UnregisterConnectionAdmissionAuthorizer(OnlineAdmissionOwner);
		}
	}
	if (CompletionTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(CompletionTicker);
		CompletionTicker.Reset();
	}
	++ProviderGeneration;
	if (Provider)
	{
		Provider->Shutdown();
		Provider = nullptr;
	}
	if (CompletionSink.IsValid())
	{
		CompletionSink->bAccepting.Store(false);
		FSeinOnlineQueuedCompletion Discarded;
		while (CompletionSink->Queue.Dequeue(Discarded))
		{
		}
		CompletionSink.Reset();
	}
	Requests.Reset();
	CompletedOrder.Reset();
	OnOperationCompleted.Clear();
}

bool USeinOnlineServicesSubsystem::IsReady() const
{
	return !bReleased && IsValid(Provider);
}

FName USeinOnlineServicesSubsystem::GetProviderName() const
{
	return Provider ? Provider->GetProviderName() : NAME_None;
}

bool USeinOnlineServicesSubsystem::GetRequestResult(
	FSeinOnlineRequestHandle Request,
	ESeinOnlineRequestStatus& Status,
	FSeinOnlineError& Error,
	FInstancedStruct& Result) const
{
	Status = ESeinOnlineRequestStatus::Unknown;
	Error = FSeinOnlineError();
	Result.Reset();
	const FRequestRecord* Record = Requests.Find(Request.Value);
	if (!Record)
	{
		return false;
	}
	Status = Record->Status;
	Error = Record->Error;
	Result = Record->Result;
	return true;
}

bool USeinOnlineServicesSubsystem::ConsumeRequestResult(
	FSeinOnlineRequestHandle Request,
	ESeinOnlineRequestStatus& Status,
	FSeinOnlineError& Error,
	FInstancedStruct& Result)
{
	if (!GetRequestResult(Request, Status, Error, Result)
		|| Status == ESeinOnlineRequestStatus::Pending)
	{
		return false;
	}
	Requests.Remove(Request.Value);
	CompletedOrder.RemoveSingle(Request.Value);
	return true;
}

bool USeinOnlineServicesSubsystem::CancelRequest(
	FSeinOnlineRequestHandle Request)
{
	FRequestRecord* Record = Requests.Find(Request.Value);
	if (!Record || Record->Status != ESeinOnlineRequestStatus::Pending
		|| !Provider || !Provider->CancelRequest(Request))
	{
		return false;
	}
	QueueLocalFailure(
		Record->Request,
		FSeinOnlineError::Make(
			ESeinOnlineErrorCode::Cancelled,
			TEXT("The online request was cancelled.")));
	return true;
}

#define SEIN_BEGIN_ONLINE_IMPL(FunctionName, OperationName, RequestType) \
	FSeinOnlineRequestHandle USeinOnlineServicesSubsystem::FunctionName( \
		RequestType Request) \
	{ \
		return BeginOperation( \
			ESeinOnlineOperation::OperationName, \
			FInstancedStruct::Make(MoveTemp(Request))); \
	}

SEIN_BEGIN_ONLINE_IMPL(Authenticate, Authenticate, FSeinOnlineAuthenticateRequest)
SEIN_BEGIN_ONLINE_IMPL(SignOut, SignOut, FSeinOnlineSignOutRequest)
SEIN_BEGIN_ONLINE_IMPL(CreateParty, CreateParty, FSeinOnlineCreatePartyRequest)
SEIN_BEGIN_ONLINE_IMPL(InviteToParty, InviteToParty, FSeinOnlineInviteToPartyRequest)
SEIN_BEGIN_ONLINE_IMPL(JoinParty, JoinParty, FSeinOnlineJoinPartyRequest)
SEIN_BEGIN_ONLINE_IMPL(LeaveParty, LeaveParty, FSeinOnlineLeavePartyRequest)
SEIN_BEGIN_ONLINE_IMPL(QueryParty, QueryParty, FSeinOnlineQueryPartyRequest)
SEIN_BEGIN_ONLINE_IMPL(StartMatchmaking, StartMatchmaking, FSeinOnlineStartMatchmakingRequest)
SEIN_BEGIN_ONLINE_IMPL(QueryMatchmaking, QueryMatchmaking, FSeinOnlineTicketRequest)
SEIN_BEGIN_ONLINE_IMPL(CancelMatchmaking, CancelMatchmaking, FSeinOnlineTicketRequest)
SEIN_BEGIN_ONLINE_IMPL(AllocateServer, AllocateServer, FSeinOnlineAllocateServerRequest)
SEIN_BEGIN_ONLINE_IMPL(RegisterMatch, RegisterMatch, FSeinOnlineRegisterMatchRequest)
SEIN_BEGIN_ONLINE_IMPL(IssueReconnectCredential, IssueReconnectCredential,
	FSeinOnlineIssueReconnectCredentialRequest)
SEIN_BEGIN_ONLINE_IMPL(ValidateReconnectCredential, ValidateReconnectCredential,
	FSeinOnlineValidateReconnectCredentialRequest)
SEIN_BEGIN_ONLINE_IMPL(SubmitMatchResult, SubmitMatchResult,
	FSeinOnlineSubmitMatchResultRequest)
SEIN_BEGIN_ONLINE_IMPL(WriteStats, WriteStats, FSeinOnlineWriteStatsRequest)
SEIN_BEGIN_ONLINE_IMPL(QueryStats, QueryStats, FSeinOnlineQueryStatsRequest)
SEIN_BEGIN_ONLINE_IMPL(QueryLeaderboard, QueryLeaderboard,
	FSeinOnlineQueryLeaderboardRequest)
SEIN_BEGIN_ONLINE_IMPL(PublishReplayEvidence, PublishReplayEvidence,
	FSeinOnlinePublishReplayEvidenceRequest)
SEIN_BEGIN_ONLINE_IMPL(QueryReplayEvidence, QueryReplayEvidence,
	FSeinOnlineQueryReplayEvidenceRequest)
SEIN_BEGIN_ONLINE_IMPL(WriteCampaignSave, WriteCampaignSave,
	FSeinOnlineWriteCampaignSaveRequest)
SEIN_BEGIN_ONLINE_IMPL(ReadCampaignSave, ReadCampaignSave,
	FSeinOnlineReadCampaignSaveRequest)
SEIN_BEGIN_ONLINE_IMPL(QueryCampaignSaves, QueryCampaignSaves,
	FSeinOnlineQueryCampaignSavesRequest)
SEIN_BEGIN_ONLINE_IMPL(SubmitTelemetry, SubmitTelemetry,
	FSeinOnlineSubmitTelemetryRequest)

#undef SEIN_BEGIN_ONLINE_IMPL

FSeinOnlineRequestHandle USeinOnlineServicesSubsystem::BeginOperation(
	ESeinOnlineOperation Operation,
	FInstancedStruct Payload)
{
	return BeginOperation(Operation, MoveTemp(Payload), ResolveCallerAuthority());
}

FSeinOnlineRequestHandle USeinOnlineServicesSubsystem::BeginOperation(
	ESeinOnlineOperation Operation,
	FInstancedStruct Payload,
	ESeinOnlineCallerAuthority Authority)
{
	check(IsInGameThread());
	TrimCompletedRecords();
	while (Requests.Num() >= MaxRequestRecords && !CompletedOrder.IsEmpty())
	{
		const int64 Oldest = CompletedOrder[0];
		CompletedOrder.RemoveAt(0, 1, EAllowShrinking::No);
		Requests.Remove(Oldest);
	}
	if (bReleased || Requests.Num() >= MaxRequestRecords
		|| NextRequestHandle <= 0 || NextRequestHandle == MAX_int64)
	{
		UE_LOG(LogSeinOnlineServices, Warning,
			TEXT("SOS rejected a request because its bounded handle table is full or unavailable."));
		return FSeinOnlineRequestHandle();
	}

	FSeinOnlineProviderRequest ProviderRequest;
	ProviderRequest.Handle.Value = NextRequestHandle++;
	ProviderRequest.Operation = Operation;
	ProviderRequest.Authority = Authority;
	ProviderRequest.Payload = MoveTemp(Payload);

	FRequestRecord& Record = Requests.Add(ProviderRequest.Handle.Value);
	Record.Request = ProviderRequest;

	FSeinOnlineError ValidationError;
	if (!SeinOnlineContract::ValidateRequest(ProviderRequest, ValidationError))
	{
		QueueLocalFailure(ProviderRequest, MoveTemp(ValidationError));
		return ProviderRequest.Handle;
	}
	if (!Provider)
	{
		QueueLocalFailure(
			ProviderRequest,
			FSeinOnlineError::Make(
				ESeinOnlineErrorCode::ProviderUnavailable,
				TEXT("The online-services provider is unavailable."),
				true));
		return ProviderRequest.Handle;
	}
	if (!Provider->SupportsOperation(Operation))
	{
		QueueLocalFailure(
			ProviderRequest,
			FSeinOnlineError::Make(
				ESeinOnlineErrorCode::Unsupported,
				TEXT("The active provider does not support this operation.")));
		return ProviderRequest.Handle;
	}

	const uint64 DispatchGeneration = ProviderGeneration;
	TWeakPtr<FSeinOnlineCompletionSink> WeakSink = CompletionSink;
	TSharedRef<TAtomic<bool>> CompletionAccepted =
		MakeShared<TAtomic<bool>>(false);
	Provider->BeginRequest(
		ProviderRequest,
		[WeakSink, CompletionAccepted, DispatchGeneration, ProviderRequest](
			FSeinOnlineProviderResponse&& Response) mutable
		{
			if (CompletionAccepted->Exchange(true))
			{
				return;
			}
			TSharedPtr<FSeinOnlineCompletionSink> Sink = WeakSink.Pin();
			if (!Sink.IsValid() || !Sink->bAccepting.Load())
			{
				return;
			}
			FSeinOnlineQueuedCompletion Queued;
			Queued.ProviderGeneration = DispatchGeneration;
			Queued.Request = MoveTemp(ProviderRequest);
			Queued.Response = MoveTemp(Response);
			Sink->Queue.Enqueue(MoveTemp(Queued));
		});
	return ProviderRequest.Handle;
}

bool USeinOnlineServicesSubsystem::CreateProvider(
	TSubclassOf<USeinOnlineServicesProvider> RequestedClass,
	bool bAllowFallback,
	FSeinOnlineError& OutError)
{
	OutError = FSeinOnlineError();
	UClass* ProviderClass = RequestedClass.Get();
	const bool bInvalidClass = !ProviderClass
		|| !ProviderClass->IsChildOf(USeinOnlineServicesProvider::StaticClass())
		|| ProviderClass->HasAnyClassFlags(CLASS_Abstract);
	if (bInvalidClass)
	{
		if (!bAllowFallback)
		{
			OutError = FSeinOnlineError::Make(
				ESeinOnlineErrorCode::InvalidRequest,
				TEXT("The configured online-services provider class is invalid."));
			return false;
		}
		UE_LOG(LogSeinOnlineServices, Error,
			TEXT("SOS provider class is invalid; falling back to Loopback."));
		ProviderClass = USeinOnlineLoopbackProvider::StaticClass();
	}
	if (ProviderClass == USeinOnlineLoopbackProvider::StaticClass()
		&& !bLoopbackPermitted)
	{
		OutError = FSeinOnlineError::Make(
			ESeinOnlineErrorCode::ProviderUnavailable,
			TEXT("The Loopback provider is disabled for this runtime."));
		return false;
	}

	Provider = NewObject<USeinOnlineServicesProvider>(this, ProviderClass);
	if (!Provider || !Provider->Initialize(*this, OutError))
	{
		if (Provider)
		{
			Provider->Shutdown();
			Provider = nullptr;
		}
		if (bAllowFallback
			&& ProviderClass != USeinOnlineLoopbackProvider::StaticClass())
		{
			UE_LOG(LogSeinOnlineServices, Error,
				TEXT("SOS provider initialization failed; falling back to Loopback."));
			return CreateProvider(
				USeinOnlineLoopbackProvider::StaticClass(),
				/*bAllowFallback=*/false,
				OutError);
		}
		if (OutError.IsSuccess())
		{
			OutError = FSeinOnlineError::Make(
				ESeinOnlineErrorCode::ProviderUnavailable,
				TEXT("The online-services provider could not initialize."),
				true);
		}
		return false;
	}
	return true;
}

void USeinOnlineServicesSubsystem::ResetProvider(
	TSubclassOf<USeinOnlineServicesProvider> RequestedClass,
	bool bAllowFallback,
	FSeinOnlineError& OutError)
{
	check(IsInGameThread());
	if (Provider)
	{
		Provider->Shutdown();
		Provider = nullptr;
	}
	// Preserve any terminal response already accepted in this generation before
	// stale callbacks are fenced out by the provider swap.
	DrainCompletions();
	++ProviderGeneration;
	for (TPair<int64, FRequestRecord>& Pair : Requests)
	{
		if (Pair.Value.Status == ESeinOnlineRequestStatus::Pending)
		{
			QueueLocalFailure(
				Pair.Value.Request,
				FSeinOnlineError::Make(
					ESeinOnlineErrorCode::ProviderUnavailable,
					TEXT("The provider changed while the request was pending."),
					true));
		}
	}
	CreateProvider(RequestedClass, bAllowFallback, OutError);
}

bool USeinOnlineServicesSubsystem::TickCompletions(float)
{
	DrainCompletions();
	return !bReleased;
}

void USeinOnlineServicesSubsystem::DrainCompletions()
{
	check(IsInGameThread());
	if (!CompletionSink.IsValid())
	{
		return;
	}
	FSeinOnlineQueuedCompletion Queued;
	while (CompletionSink->Queue.Dequeue(Queued))
	{
		if (Queued.ProviderGeneration != ProviderGeneration)
		{
			continue;
		}
		FRequestRecord* Record = Requests.Find(Queued.Request.Handle.Value);
		if (!Record || Record->Status != ESeinOnlineRequestStatus::Pending)
		{
			continue;
		}
		FSeinOnlineError ResponseValidationError;
		if (!SeinOnlineContract::ValidateResponse(
				Record->Request,
				Queued.Response,
				ResponseValidationError))
		{
			Record->Status = ESeinOnlineRequestStatus::Failed;
			Record->Error = FSeinOnlineError::Make(
				ESeinOnlineErrorCode::Internal,
				TEXT("The provider returned an invalid response schema."));
		}
		else if (Queued.Response.Error.Code == ESeinOnlineErrorCode::Cancelled)
		{
			Record->Status = ESeinOnlineRequestStatus::Cancelled;
			Record->Error = MoveTemp(Queued.Response.Error);
		}
		else if (!Queued.Response.Error.IsSuccess())
		{
			Record->Status = ESeinOnlineRequestStatus::Failed;
			Record->Error = MoveTemp(Queued.Response.Error);
		}
		else
		{
			Record->Status = ESeinOnlineRequestStatus::Succeeded;
			Record->Result = MoveTemp(Queued.Response.Payload);
		}
		CompletedOrder.Add(Queued.Request.Handle.Value);
		OnOperationCompleted.Broadcast(
			Queued.Request.Handle,
			Queued.Request.Operation,
			Record->Error);
	}
	TrimCompletedRecords();
}

void USeinOnlineServicesSubsystem::QueueLocalFailure(
	const FSeinOnlineProviderRequest& Request,
	FSeinOnlineError Error)
{
	if (!CompletionSink.IsValid() || !CompletionSink->bAccepting.Load())
	{
		return;
	}
	FSeinOnlineQueuedCompletion Queued;
	Queued.ProviderGeneration = ProviderGeneration;
	Queued.Request = Request;
	Queued.Response.Handle = Request.Handle;
	Queued.Response.Operation = Request.Operation;
	Queued.Response.Error = MoveTemp(Error);
	CompletionSink->Queue.Enqueue(MoveTemp(Queued));
}

void USeinOnlineServicesSubsystem::TrimCompletedRecords()
{
	while (CompletedOrder.Num() > MaxRetainedCompletedRequests)
	{
		const int64 Oldest = CompletedOrder[0];
		CompletedOrder.RemoveAt(0, 1, EAllowShrinking::No);
		const FRequestRecord* Record = Requests.Find(Oldest);
		if (Record && Record->Status != ESeinOnlineRequestStatus::Pending)
		{
			Requests.Remove(Oldest);
		}
	}
}

ESeinOnlineCallerAuthority
USeinOnlineServicesSubsystem::ResolveCallerAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() == NM_Client
		? ESeinOnlineCallerAuthority::Client
		: ESeinOnlineCallerAuthority::TrustedServer;
}

FSeinConnectionAdmissionDecision
USeinOnlineServicesSubsystem::AuthorizeIncomingConnection(
	const FSeinConnectionAdmissionRequest& Request)
{
	FSeinConnectionAdmissionDecision Result;
	Result.ErrorMessage = TEXT("Online admission rejected");
	if (!Provider)
	{
		return Result;
	}

	FSeinOnlineConnectionAdmissionRequest ProviderRequest;
	ProviderRequest.AdmissionID = Request.AdmissionID;
	ResolveTransportIdentity(
		Request.PlatformIdentity,
		ProviderRequest.PlatformIdentityType,
		ProviderRequest.PlatformIdentityValue);
	if (ProviderRequest.AdmissionID.IsEmpty()
		|| ProviderRequest.AdmissionID.Len()
			> SeinOnlineServicesContract::MaxIdentifierLength
		|| ProviderRequest.PlatformIdentityType.Len()
			> SeinOnlineServicesContract::MaxIdentifierLength
		|| ProviderRequest.PlatformIdentityValue.Len()
			> SeinOnlineServicesContract::MaxTextLength
		|| ProviderRequest.PlatformIdentityType.IsEmpty()
			!= ProviderRequest.PlatformIdentityValue.IsEmpty())
	{
		return Result;
	}

	const FSeinOnlineConnectionAdmissionDecision ProviderDecision =
		Provider->AuthorizeConnection(ProviderRequest);
	if (!ProviderDecision.bAccepted
		|| !ProviderDecision.MatchID.IsValid()
		|| !ProviderDecision.ParticipantID.IsValid()
		|| !ProviderDecision.AssignedSlot.IsValid())
	{
		return Result;
	}
	Result.bAccepted = true;
	Result.MatchID = ProviderDecision.MatchID;
	Result.ParticipantID = ProviderDecision.ParticipantID;
	Result.AssignedSlot = ProviderDecision.AssignedSlot;
	Result.ErrorMessage.Reset();
	return Result;
}

#if WITH_DEV_AUTOMATION_TESTS
bool USeinOnlineServicesSubsystem::SetProviderClassForTests(
	TSubclassOf<USeinOnlineServicesProvider> ProviderClass,
	FSeinOnlineError& OutError)
{
	if (bReleased)
	{
		OutError = FSeinOnlineError::Make(
			ESeinOnlineErrorCode::ProviderUnavailable,
			TEXT("The online-services subsystem has been released."));
		return false;
	}
	ResetProvider(ProviderClass, /*bAllowFallback=*/false, OutError);
	return OutError.IsSuccess();
}

void USeinOnlineServicesSubsystem::DrainCompletionsForTests()
{
	DrainCompletions();
}

void USeinOnlineServicesSubsystem::SetRequestLimitsForTests(
	int32 InMaxRequestRecords,
	int32 InMaxRetainedCompletedRequests)
{
	check(IsInGameThread());
	MaxRequestRecords = FMath::Clamp(InMaxRequestRecords, 1, 65536);
	MaxRetainedCompletedRequests = FMath::Clamp(
		InMaxRetainedCompletedRequests, 1, MaxRequestRecords);
	TrimCompletedRecords();
}

bool USeinOnlineServicesSubsystem::RegisterConnectionAdmissionForTests()
{
	check(IsInGameThread());
	USeinNetSubsystem* Net = GetGameInstance()
		? GetGameInstance()->GetSubsystem<USeinNetSubsystem>()
		: nullptr;
	return Net && Net->RegisterConnectionAdmissionAuthorizer(
		OnlineAdmissionOwner,
		FSeinConnectionAdmissionAuthorizer::CreateUObject(
			this,
			&USeinOnlineServicesSubsystem::AuthorizeIncomingConnection));
}

bool USeinOnlineServicesSubsystem::IsDevelopmentLoopbackPermittedForTests(
	bool bSettingEnabled,
	bool bRequireAuthenticatedAdmission,
	bool bDedicatedServer,
	bool bShippingBuild)
{
	return IsDevelopmentLoopbackPermitted(
		bSettingEnabled,
		bRequireAuthenticatedAdmission,
		bDedicatedServer,
		bShippingBuild);
}
#endif

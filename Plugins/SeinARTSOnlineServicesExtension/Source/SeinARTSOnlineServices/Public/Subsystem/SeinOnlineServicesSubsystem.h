/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinOnlineServicesSubsystem.h
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Declares the bounded asynchronous game-instance facade for SOS.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/SeinOnlineServicesTypes.h"
#include "SeinOnlineServicesSubsystem.generated.h"

class USeinOnlineServicesProvider;
struct FSeinOnlineCompletionSink;
struct FSeinConnectionAdmissionRequest;
struct FSeinConnectionAdmissionDecision;

/** Broadcast exactly once when an SOS request reaches a terminal state. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FSeinOnlineOperationCompleted,
	FSeinOnlineRequestHandle, Request,
	ESeinOnlineOperation, Operation,
	FSeinOnlineError, Error);

/**
 * Backend-neutral product-services facade owned by one game instance.
 *
 * Every Begin node returns immediately. Even an inline provider response is
 * published on a later game-thread ticker pass, so callers never receive a
 * re-entrant completion from inside a Begin call.
 */
UCLASS()
class SEINARTSONLINESERVICES_API USeinOnlineServicesSubsystem
	: public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Idempotently releases provider callbacks before this module unloads. */
	void ReleaseModuleOwnedStateForModuleUnload();

	/** Fired once after a request succeeds, fails, or is cancelled. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|Online")
	FSeinOnlineOperationCompleted OnOperationCompleted;

	/** True when a provider initialized successfully for this game instance. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Online",
		meta = (DisplayName = "Is Online Services Ready"))
	bool IsReady() const;

	/** Stable namespace of the active provider, or None when unavailable. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Online",
		meta = (DisplayName = "Get Online Services Provider"))
	FName GetProviderName() const;

	/** Returns current status, error, and typed payload for a retained request. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online",
		meta = (DisplayName = "Get Online Request Result"))
	bool GetRequestResult(
		FSeinOnlineRequestHandle Request,
		ESeinOnlineRequestStatus& Status,
		FSeinOnlineError& Error,
		FInstancedStruct& Result) const;

	/** Returns and removes one completed request result. Pending requests remain. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online",
		meta = (DisplayName = "Consume Online Request Result"))
	bool ConsumeRequestResult(
		FSeinOnlineRequestHandle Request,
		ESeinOnlineRequestStatus& Status,
		FSeinOnlineError& Error,
		FInstancedStruct& Result);

	/** Requests cancellation. A published terminal result cannot be cancelled. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online",
		meta = (DisplayName = "Cancel Online Request"))
	bool CancelRequest(FSeinOnlineRequestHandle Request);

	/** Begins local-user authentication. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Account",
		meta = (DisplayName = "Authenticate"))
	FSeinOnlineRequestHandle Authenticate(FSeinOnlineAuthenticateRequest Request);

	/** Ends one authenticated account session. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Account",
		meta = (DisplayName = "Sign Out"))
	FSeinOnlineRequestHandle SignOut(FSeinOnlineSignOutRequest Request);

	/** Creates a new provider-owned party. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Party",
		meta = (DisplayName = "Create Party"))
	FSeinOnlineRequestHandle CreateParty(FSeinOnlineCreatePartyRequest Request);

	/** Invites an account to an existing party. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Party",
		meta = (DisplayName = "Invite To Party"))
	FSeinOnlineRequestHandle InviteToParty(FSeinOnlineInviteToPartyRequest Request);

	/** Accepts one provider-issued party invitation. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Party",
		meta = (DisplayName = "Join Party"))
	FSeinOnlineRequestHandle JoinParty(FSeinOnlineJoinPartyRequest Request);

	/** Leaves one existing party. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Party",
		meta = (DisplayName = "Leave Party"))
	FSeinOnlineRequestHandle LeaveParty(FSeinOnlineLeavePartyRequest Request);

	/** Reads current state for one party. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Party",
		meta = (DisplayName = "Query Party"))
	FSeinOnlineRequestHandle QueryParty(FSeinOnlineQueryPartyRequest Request);

	/** Creates a provider matchmaking ticket. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Matchmaking",
		meta = (DisplayName = "Start Matchmaking"))
	FSeinOnlineRequestHandle StartMatchmaking(
		FSeinOnlineStartMatchmakingRequest Request);

	/** Reads current state for one matchmaking ticket. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Matchmaking",
		meta = (DisplayName = "Query Matchmaking"))
	FSeinOnlineRequestHandle QueryMatchmaking(FSeinOnlineTicketRequest Request);

	/** Cancels one searching matchmaking ticket. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Matchmaking",
		meta = (DisplayName = "Cancel Matchmaking"))
	FSeinOnlineRequestHandle CancelMatchmaking(FSeinOnlineTicketRequest Request);

	/** Requests a trusted-server allocation for one ticket. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Server",
		meta = (DisplayName = "Allocate Server"))
	FSeinOnlineRequestHandle AllocateServer(FSeinOnlineAllocateServerRequest Request);

	/** Registers a live lockstep match and product roster. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Match",
		meta = (DisplayName = "Register Match"))
	FSeinOnlineRequestHandle RegisterMatch(FSeinOnlineRegisterMatchRequest Request);

	/** Issues a temporary reconnect credential for a registered participant. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Reconnect",
		meta = (DisplayName = "Issue Reconnect Credential"))
	FSeinOnlineRequestHandle IssueReconnectCredential(
		FSeinOnlineIssueReconnectCredentialRequest Request);

	/** Validates a reconnect credential on the trusted server. */
	FSeinOnlineRequestHandle ValidateReconnectCredential(
		FSeinOnlineValidateReconnectCredentialRequest Request);

	/** Submits a final evidence-bound result exactly once. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Result",
		meta = (DisplayName = "Submit Match Result"))
	FSeinOnlineRequestHandle SubmitMatchResult(
		FSeinOnlineSubmitMatchResultRequest Request);

	/** Applies an idempotent batch of account statistic mutations. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Stat",
		meta = (DisplayName = "Write Stats"))
	FSeinOnlineRequestHandle WriteStats(FSeinOnlineWriteStatsRequest Request);

	/** Reads selected or all account statistics. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Stat",
		meta = (DisplayName = "Query Stats"))
	FSeinOnlineRequestHandle QueryStats(FSeinOnlineQueryStatsRequest Request);

	/** Reads an ordered stat-backed leaderboard page. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Leaderboard",
		meta = (DisplayName = "Query Leaderboard"))
	FSeinOnlineRequestHandle QueryLeaderboard(
		FSeinOnlineQueryLeaderboardRequest Request);

	/** Publishes terminal replay evidence exactly once. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Replay",
		meta = (DisplayName = "Publish Replay Evidence"))
	FSeinOnlineRequestHandle PublishReplayEvidence(
		FSeinOnlinePublishReplayEvidenceRequest Request);

	/** Reads one published replay evidence record. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Replay",
		meta = (DisplayName = "Query Replay Evidence"))
	FSeinOnlineRequestHandle QueryReplayEvidence(
		FSeinOnlineQueryReplayEvidenceRequest Request);

	/** Writes one owner-scoped campaign save with revision conflict policy. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Save",
		meta = (DisplayName = "Write Campaign Save"))
	FSeinOnlineRequestHandle WriteCampaignSave(
		FSeinOnlineWriteCampaignSaveRequest Request);

	/** Reads one owner-scoped campaign save. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Save",
		meta = (DisplayName = "Read Campaign Save"))
	FSeinOnlineRequestHandle ReadCampaignSave(
		FSeinOnlineReadCampaignSaveRequest Request);

	/** Lists owner-scoped campaign-save metadata without payload bytes. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Save",
		meta = (DisplayName = "Query Campaign Saves"))
	FSeinOnlineRequestHandle QueryCampaignSaves(
		FSeinOnlineQueryCampaignSavesRequest Request);

	/** Submits one bounded, secret-free telemetry batch exactly once. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Online|Telemetry",
		meta = (DisplayName = "Submit Telemetry"))
	FSeinOnlineRequestHandle SubmitTelemetry(
		FSeinOnlineSubmitTelemetryRequest Request);

	/** Native provider-neutral entry point used by C++ product integrations. */
	FSeinOnlineRequestHandle BeginOperation(
		ESeinOnlineOperation Operation,
		FInstancedStruct Payload,
		ESeinOnlineCallerAuthority Authority);

	/** Native entry point that derives authority from the current world role. */
	FSeinOnlineRequestHandle BeginOperation(
		ESeinOnlineOperation Operation,
		FInstancedStruct Payload);

#if WITH_DEV_AUTOMATION_TESTS
	/** Replaces the configured provider and generation for isolated tests. */
	bool SetProviderClassForTests(
		TSubclassOf<USeinOnlineServicesProvider> ProviderClass,
		FSeinOnlineError& OutError);

	/** Returns the active native provider for focused contract tests. */
	USeinOnlineServicesProvider* GetProviderForTests() const { return Provider; }

	/** Drains queued callbacks without advancing the global ticker. */
	void DrainCompletionsForTests();

	/** Overrides bounded request retention for focused capacity tests. */
	void SetRequestLimitsForTests(
		int32 InMaxRequestRecords,
		int32 InMaxRetainedCompletedRequests);

	/** Registers the real SOS-to-Net admission delegate for focused tests. */
	bool RegisterConnectionAdmissionForTests();

	/** Evaluates the runtime Loopback refusal policy without process-mode changes. */
	static bool IsDevelopmentLoopbackPermittedForTests(
		bool bSettingEnabled,
		bool bRequireAuthenticatedAdmission,
		bool bDedicatedServer,
		bool bShippingBuild);
#endif

private:
	struct FRequestRecord
	{
		FSeinOnlineProviderRequest Request;
		ESeinOnlineRequestStatus Status = ESeinOnlineRequestStatus::Pending;
		FSeinOnlineError Error;
		FInstancedStruct Result;
	};

	bool CreateProvider(
		TSubclassOf<USeinOnlineServicesProvider> RequestedClass,
		bool bAllowFallback,
		FSeinOnlineError& OutError);
	void ResetProvider(
		TSubclassOf<USeinOnlineServicesProvider> RequestedClass,
		bool bAllowFallback,
		FSeinOnlineError& OutError);
	bool TickCompletions(float DeltaSeconds);
	void DrainCompletions();
	void QueueLocalFailure(
		const FSeinOnlineProviderRequest& Request,
		FSeinOnlineError Error);
	void TrimCompletedRecords();
	ESeinOnlineCallerAuthority ResolveCallerAuthority() const;
	FSeinConnectionAdmissionDecision AuthorizeIncomingConnection(
		const FSeinConnectionAdmissionRequest& Request);

	UPROPERTY(Transient)
	TObjectPtr<USeinOnlineServicesProvider> Provider;

	TMap<int64, FRequestRecord> Requests;
	TArray<int64> CompletedOrder;
	TSharedPtr<FSeinOnlineCompletionSink> CompletionSink;
	FTSTicker::FDelegateHandle CompletionTicker;
	int64 NextRequestHandle = 1;
	uint64 ProviderGeneration = 1;
	int32 MaxRequestRecords = 2048;
	int32 MaxRetainedCompletedRequests = 512;
	bool bLoopbackPermitted = false;
	bool bReleased = false;
};

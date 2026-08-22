/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinOnlineServicesTypes.h
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Provider-neutral request, result, identity, and error schemas for SOS.
 *
 *               These values are product-service state. They never enter the
 *               deterministic simulation, lockstep protocol, or replay stream.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "Core/SeinPlayerID.h"
#include "SeinNetProtocolTypes.h"
#include "SeinOnlineServicesTypes.generated.h"

/** Frozen revision of the provider-neutral SOS request envelope. */
namespace SeinOnlineServicesContract
{
	constexpr int32 CurrentRevision = 1;
	constexpr int32 MaxIdentifierLength = 256;
	constexpr int32 MaxDisplayNameLength = 128;
	constexpr int32 MaxTextLength = 1024;
	constexpr int32 MaxIdempotencyKeyLength = 128;
	constexpr int32 MaxAttributes = 32;
	constexpr int32 MaxCollectionEntries = 256;
	constexpr int32 MaxBlobBytes = 4 * 1024 * 1024;
}

/** Process-local handle for one asynchronous SOS operation. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineRequestHandle
{
	GENERATED_BODY()

	/** Positive process-local request sequence. Zero is invalid. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online")
	int64 Value = 0;

	bool IsValid() const { return Value > 0; }
	bool operator==(const FSeinOnlineRequestHandle& Other) const
	{
		return Value == Other.Value;
	}
	bool operator!=(const FSeinOnlineRequestHandle& Other) const
	{
		return !(*this == Other);
	}
};

FORCEINLINE uint32 GetTypeHash(const FSeinOnlineRequestHandle& Handle)
{
	return GetTypeHash(Handle.Value);
}

/** Backend-qualified opaque identifier. Callers must not parse Value. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineOpaqueID
{
	GENERATED_BODY()

	/** Stable provider namespace that issued this identifier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online")
	FName Provider;

	/** Opaque provider value. Do not infer product meaning from its format. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online")
	FString Value;

	bool IsValid() const { return !Provider.IsNone() && !Value.IsEmpty(); }
	FString ToCanonicalString() const;
	bool operator==(const FSeinOnlineOpaqueID& Other) const
	{
		return Provider == Other.Provider && Value == Other.Value;
	}
	bool operator!=(const FSeinOnlineOpaqueID& Other) const
	{
		return !(*this == Other);
	}
};

FORCEINLINE uint32 GetTypeHash(const FSeinOnlineOpaqueID& ID)
{
	return HashCombineFast(GetTypeHash(ID.Provider), GetTypeHash(ID.Value));
}

/** Operation selected by a provider-neutral SOS request. */
UENUM(BlueprintType)
enum class ESeinOnlineOperation : uint8
{
	None,
	Authenticate,
	SignOut,
	CreateParty,
	InviteToParty,
	JoinParty,
	LeaveParty,
	QueryParty,
	StartMatchmaking,
	QueryMatchmaking,
	CancelMatchmaking,
	AllocateServer,
	RegisterMatch,
	IssueReconnectCredential,
	ValidateReconnectCredential,
	SubmitMatchResult,
	WriteStats,
	QueryStats,
	QueryLeaderboard,
	PublishReplayEvidence,
	QueryReplayEvidence,
	WriteCampaignSave,
	ReadCampaignSave,
	QueryCampaignSaves,
	SubmitTelemetry,
};

/** Lifecycle state retained by the SOS subsystem for one request. */
UENUM(BlueprintType)
enum class ESeinOnlineRequestStatus : uint8
{
	Unknown,
	Pending,
	Succeeded,
	Failed,
	Cancelled,
};

/** Stable provider-neutral error classification. */
UENUM(BlueprintType)
enum class ESeinOnlineErrorCode : uint8
{
	None,
	InvalidRequest,
	NotAuthenticated,
	NotFound,
	Conflict,
	Forbidden,
	Unsupported,
	CapacityExceeded,
	Cancelled,
	Expired,
	ProviderUnavailable,
	Internal,
};

/** Safe, provider-neutral failure returned to product code. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineError
{
	GENERATED_BODY()

	/** Stable programmatic error code. None means success. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online")
	ESeinOnlineErrorCode Code = ESeinOnlineErrorCode::None;

	/** User-safe diagnostic text. It must never contain credentials or tokens. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online")
	FString Message;

	/** Whether retrying later may succeed without changing the request. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online")
	bool bRetryable = false;

	bool IsSuccess() const { return Code == ESeinOnlineErrorCode::None; }
	static FSeinOnlineError Make(
		ESeinOnlineErrorCode InCode,
		FString InMessage,
		bool bInRetryable = false);
};

/** Process role that routed a request to a provider. */
UENUM(BlueprintType)
enum class ESeinOnlineCallerAuthority : uint8
{
	Client,
	TrustedServer,
};

/** Exact provider request envelope validated before dispatch. */
USTRUCT()
struct SEINARTSONLINESERVICES_API FSeinOnlineProviderRequest
{
	GENERATED_BODY()

	UPROPERTY()
	int32 ContractRevision = SeinOnlineServicesContract::CurrentRevision;

	UPROPERTY()
	FSeinOnlineRequestHandle Handle;

	UPROPERTY()
	ESeinOnlineOperation Operation = ESeinOnlineOperation::None;

	UPROPERTY()
	ESeinOnlineCallerAuthority Authority = ESeinOnlineCallerAuthority::Client;

	UPROPERTY()
	FInstancedStruct Payload;
};

/** Exact provider response envelope validated before publication. */
USTRUCT()
struct SEINARTSONLINESERVICES_API FSeinOnlineProviderResponse
{
	GENERATED_BODY()

	UPROPERTY()
	FSeinOnlineRequestHandle Handle;

	UPROPERTY()
	ESeinOnlineOperation Operation = ESeinOnlineOperation::None;

	UPROPERTY()
	FSeinOnlineError Error;

	UPROPERTY()
	FInstancedStruct Payload;
};

/** String attribute used by queue, telemetry, and metadata contracts. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineAttribute
{
	GENERATED_BODY()

	/** Case-sensitive attribute key. Duplicate keys are rejected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online")
	FName Key;

	/** Provider-neutral string value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online")
	FString Value;
};

/** Durable mutation receipt returned unchanged for an idempotent retry. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineMutationReceipt
{
	GENERATED_BODY()

	/** Provider-issued immutable receipt identity. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online")
	FSeinOnlineOpaqueID ReceiptID;
};

/** Product account returned by authentication. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineAccountRecord
{
	GENERATED_BODY()

	/** Provider-qualified product account identity. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Account")
	FSeinOnlineOpaqueID AccountID;

	/** Display name suitable for lobby and social presentation. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Account")
	FString DisplayName;

	/** Local user index that owns this authenticated session. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Account")
	int32 LocalUserIndex = INDEX_NONE;
};

/** Authenticates one local user with the selected provider. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineAuthenticateRequest
{
	GENERATED_BODY()

	/** Platform-local user index. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Account")
	int32 LocalUserIndex = 0;

	/** Provider-defined credential kind, or Loopback for local development. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Account")
	FName CredentialType = TEXT("Loopback");

	/** Secret credential supplied to the provider. Never log or persist it. */
	UPROPERTY()
	FString Credential;

	/** Optional display name used by providers that create local accounts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Account")
	FString DisplayNameHint;
};

/** Authentication result for one local user. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineAuthenticateResult
{
	GENERATED_BODY()

	/** Authenticated account session. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Account")
	FSeinOnlineAccountRecord Account;
};

/** Signs one authenticated account out of the provider. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineSignOutRequest
{
	GENERATED_BODY()

	/** Account whose local authenticated session should end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Account")
	FSeinOnlineOpaqueID AccountID;
};

/** One member of a product party. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlinePartyMember
{
	GENERATED_BODY()

	/** Member account identity. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Party")
	FSeinOnlineOpaqueID AccountID;

	/** Whether this member currently owns party administration. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Party")
	bool bLeader = false;
};

/** Current provider-owned party state. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlinePartyRecord
{
	GENERATED_BODY()

	/** Provider-qualified party identity. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Party")
	FSeinOnlineOpaqueID PartyID;

	/** Current party leader. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Party")
	FSeinOnlineOpaqueID LeaderAccountID;

	/** Current party members. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Party")
	TArray<FSeinOnlinePartyMember> Members;

	/** Maximum members accepted by this party. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Party")
	int32 MaxMembers = 8;

	/** Monotonic provider revision of the party record. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Party")
	int64 Revision = 1;
};

/** Creates a new party led by an authenticated account. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineCreatePartyRequest
{
	GENERATED_BODY()

	/** Authenticated account that becomes party leader. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Party")
	FSeinOnlineOpaqueID LeaderAccountID;

	/** Maximum party size, including the leader. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Party",
		meta = (ClampMin = "1", ClampMax = "256"))
	int32 MaxMembers = 8;

	/** Stable retry key for this party creation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Party")
	FString IdempotencyKey;
};

/** Result of creating, joining, or querying a party. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlinePartyResult
{
	GENERATED_BODY()

	/** Current authoritative party record. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Party")
	FSeinOnlinePartyRecord Party;
};

/** Invites an account to an existing party. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineInviteToPartyRequest
{
	GENERATED_BODY()

	/** Party receiving the invitation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Party")
	FSeinOnlineOpaqueID PartyID;

	/** Current member sending the invitation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Party")
	FSeinOnlineOpaqueID InviterAccountID;

	/** Account invited to join. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Party")
	FSeinOnlineOpaqueID InviteeAccountID;
};

/** Provider-owned invitation to a party. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlinePartyInviteResult
{
	GENERATED_BODY()

	/** Opaque invitation identity consumed by Join Party. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Party")
	FSeinOnlineOpaqueID InviteID;

	/** Party offered by the invitation. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Party")
	FSeinOnlineOpaqueID PartyID;

	/** Account allowed to consume the invitation. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Party")
	FSeinOnlineOpaqueID InviteeAccountID;
};

/** Accepts a provider-issued party invitation. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineJoinPartyRequest
{
	GENERATED_BODY()

	/** Invitation returned by Invite To Party. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Party")
	FSeinOnlineOpaqueID InviteID;

	/** Authenticated account accepting the invitation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Party")
	FSeinOnlineOpaqueID AccountID;
};

/** Removes one member from an existing party. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineLeavePartyRequest
{
	GENERATED_BODY()

	/** Party to leave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Party")
	FSeinOnlineOpaqueID PartyID;

	/** Current member leaving the party. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Party")
	FSeinOnlineOpaqueID AccountID;
};

/** Queries current state for one party. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineQueryPartyRequest
{
	GENERATED_BODY()

	/** Party whose current state should be returned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Party")
	FSeinOnlineOpaqueID PartyID;
};

/** Lifecycle of a matchmaking ticket. */
UENUM(BlueprintType)
enum class ESeinOnlineMatchmakingState : uint8
{
	Searching,
	Matched,
	Cancelled,
	Failed,
};

/** Provider-owned matchmaking ticket state. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineMatchmakingTicket
{
	GENERATED_BODY()

	/** Provider-qualified ticket identity. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Matchmaking")
	FSeinOnlineOpaqueID TicketID;

	/** Current ticket lifecycle state. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Matchmaking")
	ESeinOnlineMatchmakingState State = ESeinOnlineMatchmakingState::Searching;

	/** Queue selected when the ticket was created. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Matchmaking")
	FName Queue;

	/** Preferred region supplied by the caller. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Matchmaking")
	FName Region;
};

/** Starts matchmaking for an account or party. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineStartMatchmakingRequest
{
	GENERATED_BODY()

	/** Authenticated account requesting matchmaking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Matchmaking")
	FSeinOnlineOpaqueID AccountID;

	/** Optional party to match as one roster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Matchmaking")
	FSeinOnlineOpaqueID PartyID;

	/** Product queue name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Matchmaking")
	FName Queue;

	/** Preferred deployment region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Matchmaking")
	FName Region;

	/** Bounded queue-specific search attributes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Matchmaking")
	TArray<FSeinOnlineAttribute> Attributes;

	/** Stable retry key for this ticket creation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Matchmaking")
	FString IdempotencyKey;
};

/** Matchmaking ticket result. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineMatchmakingResult
{
	GENERATED_BODY()

	/** Current provider ticket state. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Matchmaking")
	FSeinOnlineMatchmakingTicket Ticket;
};

/** Queries or cancels one matchmaking ticket. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineTicketRequest
{
	GENERATED_BODY()

	/** Provider ticket returned by Start Matchmaking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Matchmaking")
	FSeinOnlineOpaqueID TicketID;
};

/** Requests a server allocation for a matched ticket. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineAllocateServerRequest
{
	GENERATED_BODY()

	/** Matched or locally accepted ticket to allocate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Server")
	FSeinOnlineOpaqueID TicketID;

	/** Build identifier required by the connecting clients. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Server")
	FString BuildID;

	/** Stable retry key for this server allocation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Server")
	FString IdempotencyKey;
};

/** Provider-owned server connection assignment. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineServerAllocationResult
{
	GENERATED_BODY()

	/** Provider allocation identity. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Server")
	FSeinOnlineOpaqueID AllocationID;

	/** Host name or address selected by the provider. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Server")
	FString Host;

	/** Connection port selected by the provider. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Server")
	int32 Port = 0;

	/** Secret proving server-allocation ownership. Never expose to clients. */
	UPROPERTY()
	FString LeaseSecret;
};

/** Product classification attached to one registered match. */
UENUM(BlueprintType)
enum class ESeinOnlineMatchClassification : uint8
{
	Unranked,
	Ranked,
};

/** Account-to-lockstep identity binding registered for one match. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineMatchRosterEntry
{
	GENERATED_BODY()

	/** Product account participating in the match. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Match")
	FSeinOnlineOpaqueID AccountID;

	/** Stable process identity used by the lockstep transport. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Match")
	FSeinNetworkParticipantID ParticipantID;

	/** Gameplay slot controlled by this roster entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Match")
	FSeinPlayerID PlayerID;
};

/** Registers a live lockstep match and its product roster. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineRegisterMatchRequest
{
	GENERATED_BODY()

	/** Existing lockstep match identity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Match")
	FSeinMatchInstanceID MatchID;

	/** Server allocation that owns admission for this match. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Match")
	FSeinOnlineOpaqueID AllocationID;

	/** Ranked or unranked product classification. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Match")
	ESeinOnlineMatchClassification Classification =
		ESeinOnlineMatchClassification::Unranked;

	/** Product accounts bound to lockstep participants and gameplay slots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Match")
	TArray<FSeinOnlineMatchRosterEntry> Roster;

	/** Stable retry key for this match registration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Match")
	FString IdempotencyKey;
};

/** Registered match state returned by SOS. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineMatchRecord
{
	GENERATED_BODY()

	/** Lockstep match identity owned by the framework Net module. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Match")
	FSeinMatchInstanceID MatchID;

	/** Server allocation bound to this registered match. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Match")
	FSeinOnlineOpaqueID AllocationID;

	/** Product classification frozen at registration. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Match")
	ESeinOnlineMatchClassification Classification =
		ESeinOnlineMatchClassification::Unranked;

	/** Registered product and lockstep roster. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Match")
	TArray<FSeinOnlineMatchRosterEntry> Roster;
};

/** Match registration result. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineRegisterMatchResult
{
	GENERATED_BODY()

	/** Provider's registered match record. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Match")
	FSeinOnlineMatchRecord Match;
};

/** Requests a temporary credential for one registered participant. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineIssueReconnectCredentialRequest
{
	GENERATED_BODY()

	/** Match the participant is allowed to rejoin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Reconnect")
	FSeinMatchInstanceID MatchID;

	/** Registered participant receiving the credential. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Reconnect")
	FSeinNetworkParticipantID ParticipantID;

	/** Credential lifetime in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Reconnect",
		meta = (ClampMin = "1", ClampMax = "86400"))
	int32 LifetimeSeconds = 120;

	/** Optional transport-provider namespace expected at connection time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Reconnect")
	FString PlatformIdentityType;

	/**
	 * Optional authenticated transport identity expected at connection time.
	 * Leave both identity fields empty only when the provider binds it out of band.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Reconnect")
	FString PlatformIdentityValue;

	/** Stable retry key for this credential issuance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Reconnect")
	FString IdempotencyKey;
};

/** Temporary reconnect credential returned to trusted connection flow. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineReconnectCredentialResult
{
	GENERATED_BODY()

	/**
	 * Non-secret correlation identity for the SeinAdmission connection option.
	 * It is single-use but is not accepted without provider-side identity state.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Reconnect")
	FString AdmissionID;

	/**
	 * Secret credential for provider-specific secure redemption or validation.
	 * Never place it in a connection URL, log, replay, persistence, or telemetry.
	 */
	UPROPERTY()
	FString Credential;

	/** UTC Unix time in milliseconds after which validation must fail. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Reconnect")
	int64 ExpiresAtUnixMilliseconds = 0;
};

/** Validates a reconnect credential against its exact match participant. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineValidateReconnectCredentialRequest
{
	GENERATED_BODY()

	/** Match being rejoined. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Reconnect")
	FSeinMatchInstanceID MatchID;

	/** Participant identity being reclaimed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Reconnect")
	FSeinNetworkParticipantID ParticipantID;

	/** Secret credential previously issued by SOS. */
	UPROPERTY()
	FString Credential;
};

/** Successful reconnect credential validation. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineValidateReconnectCredentialResult
{
	GENERATED_BODY()

	/** Registered product account bound to the reconnecting participant. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Reconnect")
	FSeinOnlineOpaqueID AccountID;
};

/** Final placement for one registered match account. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineMatchPlacement
{
	GENERATED_BODY()

	/** Registered account receiving this placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Result")
	FSeinOnlineOpaqueID AccountID;

	/** One-based finish position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Result",
		meta = (ClampMin = "1"))
	int32 Placement = 1;
};

/** Idempotently submits a final product result for one registered match. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineSubmitMatchResultRequest
{
	GENERATED_BODY()

	/** Registered match receiving the final result. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Result")
	FSeinMatchInstanceID MatchID;

	/** Final placements, with one unique registered account per entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Result")
	TArray<FSeinOnlineMatchPlacement> Placements;

	/** Final deterministic tick committed by the trusted match authority. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Result")
	int32 TerminalTick = INDEX_NONE;

	/** Canonical world-state root captured at Terminal Tick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Result")
	FGuid FinalWorldRoot;

	/** Final digest of the replay journal evidence chain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Result")
	FGuid ReplayFinalDigest;

	/** Previously published replay evidence bound to this terminal state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Result")
	FSeinOnlineOpaqueID ReplayEvidenceID;

	/** Stable retry key for this durable mutation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Result")
	FString IdempotencyKey;
};

/** Supported atomic stat mutation. */
UENUM(BlueprintType)
enum class ESeinOnlineStatMutationKind : uint8
{
	Set,
	Add,
	Max,
	Min,
};

/** One named integer statistic mutation. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineStatMutation
{
	GENERATED_BODY()

	/** Stable statistic name. Duplicate names in one write are rejected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Stat")
	FName Stat;

	/** Atomic operation applied by the provider. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Stat")
	ESeinOnlineStatMutationKind Kind = ESeinOnlineStatMutationKind::Add;

	/** Signed integer operand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Stat")
	int64 Value = 0;
};

/** Idempotently mutates product statistics after a registered match. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineWriteStatsRequest
{
	GENERATED_BODY()

	/** Account receiving the mutations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Stat")
	FSeinOnlineOpaqueID AccountID;

	/** Registered match authorizing this write. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Stat")
	FSeinMatchInstanceID MatchID;

	/** Unique named mutations applied atomically. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Stat")
	TArray<FSeinOnlineStatMutation> Mutations;

	/** Stable retry key for this durable mutation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Stat")
	FString IdempotencyKey;
};

/** Queries selected or all product statistics for an account. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineQueryStatsRequest
{
	GENERATED_BODY()

	/** Account whose statistics should be returned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Stat")
	FSeinOnlineOpaqueID AccountID;

	/** Optional stat filter. Empty returns all stored stats. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Stat")
	TArray<FName> Stats;
};

/** One current product statistic value. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineStatValue
{
	GENERATED_BODY()

	/** Stable statistic name. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Stat")
	FName Stat;

	/** Current signed integer value. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Stat")
	int64 Value = 0;
};

/** Current statistics for one account. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineQueryStatsResult
{
	GENERATED_BODY()

	/** Account whose statistics were read. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Stat")
	FSeinOnlineOpaqueID AccountID;

	/** Canonically name-sorted statistic values. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Stat")
	TArray<FSeinOnlineStatValue> Stats;
};

/** Queries a stat-backed leaderboard. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineQueryLeaderboardRequest
{
	GENERATED_BODY()

	/** Statistic used as the leaderboard score. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Leaderboard")
	FName Stat;

	/** Zero-based first row. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Leaderboard",
		meta = (ClampMin = "0"))
	int32 Offset = 0;

	/** Maximum rows to return. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Leaderboard",
		meta = (ClampMin = "1", ClampMax = "256"))
	int32 Limit = 100;
};

/** One ordered leaderboard row. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineLeaderboardEntry
{
	GENERATED_BODY()

	/** One-based global rank. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Leaderboard")
	int32 Rank = 0;

	/** Ranked product account. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Leaderboard")
	FSeinOnlineOpaqueID AccountID;

	/** Current score for the queried statistic. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Leaderboard")
	int64 Score = 0;
};

/** Ordered leaderboard query result. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineQueryLeaderboardResult
{
	GENERATED_BODY()

	/** Statistic used to rank these entries. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Leaderboard")
	FName Stat;

	/** Score-descending rows with account identity as the stable tie-break. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Leaderboard")
	TArray<FSeinOnlineLeaderboardEntry> Entries;
};

/** Idempotently publishes trusted-server replay evidence outside the simulation replay stream. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlinePublishReplayEvidenceRequest
{
	GENERATED_BODY()

	/** Match whose replay evidence is being published. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Replay")
	FSeinMatchInstanceID MatchID;

	/** Final canonical world-state root associated with the evidence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Replay")
	FGuid FinalWorldRoot;

	/** Final deterministic tick covered by the replay evidence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Replay")
	int32 TerminalTick = INDEX_NONE;

	/** Final digest of the replay journal evidence chain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Replay")
	FGuid ReplayFinalDigest;

	/** Opaque compressed replay or verification evidence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Replay")
	TArray<uint8> Evidence;

	/** Stable retry key for this durable mutation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Replay")
	FString IdempotencyKey;
};

/** Stored replay evidence metadata and bytes. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineReplayEvidenceRecord
{
	GENERATED_BODY()

	/** Provider-qualified replay evidence identity. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Replay")
	FSeinOnlineOpaqueID ReplayID;

	/** Match associated with this evidence. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Replay")
	FSeinMatchInstanceID MatchID;

	/** Final canonical world-state root supplied by the publisher. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Replay")
	FGuid FinalWorldRoot;

	/** Final deterministic tick covered by this evidence. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Replay")
	int32 TerminalTick = INDEX_NONE;

	/** Final digest of the replay journal evidence chain. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Replay")
	FGuid ReplayFinalDigest;

	/** Opaque compressed replay or verification evidence. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Replay")
	TArray<uint8> Evidence;
};

/** Replay evidence publication result. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlinePublishReplayEvidenceResult
{
	GENERATED_BODY()

	/** Stored evidence record. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Replay")
	FSeinOnlineReplayEvidenceRecord Replay;
};

/** Queries one stored replay evidence record. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineQueryReplayEvidenceRequest
{
	GENERATED_BODY()

	/** Provider replay identity returned by publication. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Replay")
	FSeinOnlineOpaqueID ReplayID;
};

/** Ownership scope for one campaign save. */
UENUM(BlueprintType)
enum class ESeinOnlineSaveOwnerKind : uint8
{
	Account,
	Party,
};

/** Provider-qualified campaign-save owner. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineSaveOwner
{
	GENERATED_BODY()

	/** Whether the save belongs to one account or one party. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Save")
	ESeinOnlineSaveOwnerKind Kind = ESeinOnlineSaveOwnerKind::Account;

	/** Account or party identity selected by Kind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Save")
	FSeinOnlineOpaqueID OwnerID;
};

/** Conflict behavior for a campaign-save write. */
UENUM(BlueprintType)
enum class ESeinOnlineSaveWriteMode : uint8
{
	CreateOnly,
	IfRevision,
	Overwrite,
};

/** Idempotently writes one revisioned campaign save. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineWriteCampaignSaveRequest
{
	GENERATED_BODY()

	/** Authenticated account authorizing this owner-scoped write. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Save")
	FSeinOnlineOpaqueID RequestingAccountID;

	/** Account or party that owns the save. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Save")
	FSeinOnlineSaveOwner Owner;

	/** Stable product slot name within the owner scope. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Save")
	FString Slot;

	/** Conflict policy for the write. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Save")
	ESeinOnlineSaveWriteMode Mode = ESeinOnlineSaveWriteMode::IfRevision;

	/** Required current revision when Mode is If Revision. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Save")
	int64 ExpectedRevision = 0;

	/** Opaque game-owned campaign-save payload. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Save")
	TArray<uint8> Data;

	/** Stable retry key for this durable mutation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Save")
	FString IdempotencyKey;
};

/** One revisioned campaign save returned by the provider. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineCampaignSaveRecord
{
	GENERATED_BODY()

	/** Account or party that owns this save. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Save")
	FSeinOnlineSaveOwner Owner;

	/** Product slot name within the owner scope. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Save")
	FString Slot;

	/** Monotonic revision assigned by the provider. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Save")
	int64 Revision = 0;

	/** Opaque game-owned campaign-save payload. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Save")
	TArray<uint8> Data;
};

/** Campaign-save write or read result. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineCampaignSaveResult
{
	GENERATED_BODY()

	/** Current stored campaign-save record. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Save")
	FSeinOnlineCampaignSaveRecord Save;
};

/** Reads one owner-scoped campaign-save slot. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineReadCampaignSaveRequest
{
	GENERATED_BODY()

	/** Authenticated account authorizing this owner-scoped read. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Save")
	FSeinOnlineOpaqueID RequestingAccountID;

	/** Account or party that owns the save. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Save")
	FSeinOnlineSaveOwner Owner;

	/** Product slot name within the owner scope. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Save")
	FString Slot;
};

/** Lists campaign-save metadata for one owner without returning save bytes. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineQueryCampaignSavesRequest
{
	GENERATED_BODY()

	/** Authenticated account authorizing this owner-scoped query. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Save")
	FSeinOnlineOpaqueID RequestingAccountID;

	/** Account or party whose save slots should be listed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Save")
	FSeinOnlineSaveOwner Owner;
};

/** Campaign-save metadata without its potentially large payload. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineCampaignSaveSummary
{
	GENERATED_BODY()

	/** Product slot name within the owner scope. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Save")
	FString Slot;

	/** Current stored revision. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Save")
	int64 Revision = 0;

	/** Current payload size in bytes. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Save")
	int32 Bytes = 0;
};

/** Owner-scoped list of campaign-save slots. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineQueryCampaignSavesResult
{
	GENERATED_BODY()

	/** Account or party whose saves were queried. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Save")
	FSeinOnlineSaveOwner Owner;

	/** Slot-name-sorted save metadata. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Online|Save")
	TArray<FSeinOnlineCampaignSaveSummary> Saves;
};

/** One bounded product telemetry event. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineTelemetryEvent
{
	GENERATED_BODY()

	/** Stable event name understood by product analytics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Telemetry")
	FName Event;

	/** UTC Unix time in milliseconds captured by the caller. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Telemetry")
	int64 TimestampUnixMilliseconds = 0;

	/** Bounded event attributes. Never include secrets or reconnect tokens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Telemetry")
	TArray<FSeinOnlineAttribute> Attributes;
};

/** Idempotently submits a bounded telemetry batch. */
USTRUCT(BlueprintType)
struct SEINARTSONLINESERVICES_API FSeinOnlineSubmitTelemetryRequest
{
	GENERATED_BODY()

	/** Optional authenticated account associated with the events. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Telemetry")
	FSeinOnlineOpaqueID AccountID;

	/** Ordered event batch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Telemetry")
	TArray<FSeinOnlineTelemetryEvent> Events;

	/** Stable retry key for this durable mutation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Online|Telemetry")
	FString IdempotencyKey;
};

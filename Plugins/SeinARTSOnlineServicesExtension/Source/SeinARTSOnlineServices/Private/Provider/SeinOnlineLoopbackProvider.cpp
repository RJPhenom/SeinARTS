/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinOnlineLoopbackProvider.cpp
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Implements the SOS in-process reference backend and provider.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Provider/SeinOnlineLoopbackProvider.h"

#include "Contract/SeinOnlineServicesContract.h"
#include "HAL/CriticalSection.h"
#include "Misc/Base64.h"
#include "Misc/DateTime.h"
#include "Misc/ScopeLock.h"

namespace
{
	const FName LoopbackProviderName(TEXT("Loopback"));
	FCriticalSection GLoopbackBackendLifecycleMutex;
	TWeakPtr<FSeinOnlineLoopbackBackend> GLoopbackBackend;

	struct FLoopbackInvite
	{
		FSeinOnlineOpaqueID InviteID;
		FSeinOnlineOpaqueID PartyID;
		FSeinOnlineOpaqueID InviteeAccountID;
	};

	struct FLoopbackTicket
	{
		FSeinOnlineMatchmakingTicket Ticket;
		FSeinOnlineOpaqueID AccountID;
		FSeinOnlineOpaqueID PartyID;
	};

	struct FLoopbackAllocation
	{
		FSeinOnlineOpaqueID AllocationID;
		FSeinOnlineOpaqueID TicketID;
		FString LeaseSecret;
	};

	struct FLoopbackReconnectCredential
	{
		FSeinMatchInstanceID MatchID;
		FSeinNetworkParticipantID ParticipantID;
		FSeinOnlineOpaqueID AccountID;
		FSeinPlayerID PlayerID;
		FString AdmissionID;
		FString Credential;
		FString PlatformIdentityType;
		FString PlatformIdentityValue;
		int64 ExpiresAtUnixMilliseconds = 0;
	};

	struct FLoopbackIdempotencyRecord
	{
		FString Fingerprint;
		FInstancedStruct Result;
	};

	FString MatchKey(const FSeinMatchInstanceID& MatchID)
	{
		return MatchID.Value.ToString(EGuidFormats::DigitsWithHyphensLower);
	}

	FString ParticipantKey(const FSeinNetworkParticipantID& ParticipantID)
	{
		return ParticipantID.Value.ToString(EGuidFormats::DigitsWithHyphensLower);
	}

	void AppendField(FString& Out, const FString& Value)
	{
		Out.Appendf(TEXT("%d:"), Value.Len());
		Out.Append(Value);
		Out.AppendChar(TEXT('|'));
	}

	void AppendID(FString& Out, const FSeinOnlineOpaqueID& ID)
	{
		AppendField(Out, ID.ToCanonicalString());
	}

	void AppendGuid(FString& Out, const FGuid& Guid)
	{
		AppendField(Out, Guid.ToString(EGuidFormats::DigitsWithHyphensLower));
	}

	void AppendBytes(FString& Out, const TArray<uint8>& Bytes)
	{
		AppendField(Out, FBase64::Encode(Bytes));
	}

	FString CanonicalDurablePayload(const FSeinOnlineProviderRequest& Request)
	{
		FString Result;
		switch (Request.Operation)
		{
		case ESeinOnlineOperation::CreateParty:
		{
			const auto& Value = Request.Payload.Get<FSeinOnlineCreatePartyRequest>();
			AppendID(Result, Value.LeaderAccountID);
			Result.Appendf(TEXT("%d|"), Value.MaxMembers);
			break;
		}
		case ESeinOnlineOperation::StartMatchmaking:
		{
			const auto& Value =
				Request.Payload.Get<FSeinOnlineStartMatchmakingRequest>();
			AppendID(Result, Value.AccountID);
			AppendID(Result, Value.PartyID);
			AppendField(Result, Value.Queue.ToString());
			AppendField(Result, Value.Region.ToString());
			TArray<FSeinOnlineAttribute> Attributes = Value.Attributes;
			Attributes.Sort([](const auto& A, const auto& B)
			{
				return A.Key.LexicalLess(B.Key);
			});
			for (const FSeinOnlineAttribute& Attribute : Attributes)
			{
				AppendField(Result, Attribute.Key.ToString());
				AppendField(Result, Attribute.Value);
			}
			break;
		}
		case ESeinOnlineOperation::AllocateServer:
		{
			const auto& Value = Request.Payload.Get<FSeinOnlineAllocateServerRequest>();
			AppendID(Result, Value.TicketID);
			AppendField(Result, Value.BuildID);
			break;
		}
		case ESeinOnlineOperation::RegisterMatch:
		{
			const auto& Value = Request.Payload.Get<FSeinOnlineRegisterMatchRequest>();
			AppendGuid(Result, Value.MatchID.Value);
			AppendID(Result, Value.AllocationID);
			Result.Appendf(TEXT("%d|"), static_cast<int32>(Value.Classification));
			TArray<FSeinOnlineMatchRosterEntry> Roster = Value.Roster;
			Roster.Sort([](const auto& A, const auto& B)
			{
				return A.AccountID.ToCanonicalString()
					< B.AccountID.ToCanonicalString();
			});
			for (const FSeinOnlineMatchRosterEntry& Entry : Roster)
			{
				AppendID(Result, Entry.AccountID);
				AppendGuid(Result, Entry.ParticipantID.Value);
				Result.Appendf(TEXT("%d|"), Entry.PlayerID.Value);
			}
			break;
		}
		case ESeinOnlineOperation::IssueReconnectCredential:
		{
			const auto& Value = Request.Payload.Get<
				FSeinOnlineIssueReconnectCredentialRequest>();
			AppendGuid(Result, Value.MatchID.Value);
			AppendGuid(Result, Value.ParticipantID.Value);
			Result.Appendf(TEXT("%d|"), Value.LifetimeSeconds);
			AppendField(Result, Value.PlatformIdentityType);
			AppendField(Result, Value.PlatformIdentityValue);
			break;
		}
		case ESeinOnlineOperation::SubmitMatchResult:
		{
			const auto& Value = Request.Payload.Get<FSeinOnlineSubmitMatchResultRequest>();
			AppendGuid(Result, Value.MatchID.Value);
			Result.Appendf(TEXT("%d|"), Value.TerminalTick);
			AppendGuid(Result, Value.FinalWorldRoot);
			AppendGuid(Result, Value.ReplayFinalDigest);
			AppendID(Result, Value.ReplayEvidenceID);
			TArray<FSeinOnlineMatchPlacement> Placements = Value.Placements;
			Placements.Sort([](const auto& A, const auto& B)
			{
				return A.AccountID.ToCanonicalString()
					< B.AccountID.ToCanonicalString();
			});
			for (const FSeinOnlineMatchPlacement& Placement : Placements)
			{
				AppendID(Result, Placement.AccountID);
				Result.Appendf(TEXT("%d|"), Placement.Placement);
			}
			break;
		}
		case ESeinOnlineOperation::WriteStats:
		{
			const auto& Value = Request.Payload.Get<FSeinOnlineWriteStatsRequest>();
			AppendID(Result, Value.AccountID);
			AppendGuid(Result, Value.MatchID.Value);
			TArray<FSeinOnlineStatMutation> Mutations = Value.Mutations;
			Mutations.Sort([](const auto& A, const auto& B)
			{
				return A.Stat.LexicalLess(B.Stat);
			});
			for (const FSeinOnlineStatMutation& Mutation : Mutations)
			{
				AppendField(Result, Mutation.Stat.ToString());
				Result.Appendf(
					TEXT("%d|%lld|"),
					static_cast<int32>(Mutation.Kind),
					Mutation.Value);
			}
			break;
		}
		case ESeinOnlineOperation::PublishReplayEvidence:
		{
			const auto& Value =
				Request.Payload.Get<FSeinOnlinePublishReplayEvidenceRequest>();
			AppendGuid(Result, Value.MatchID.Value);
			AppendGuid(Result, Value.FinalWorldRoot);
			Result.Appendf(TEXT("%d|"), Value.TerminalTick);
			AppendGuid(Result, Value.ReplayFinalDigest);
			AppendBytes(Result, Value.Evidence);
			break;
		}
		case ESeinOnlineOperation::WriteCampaignSave:
		{
			const auto& Value =
				Request.Payload.Get<FSeinOnlineWriteCampaignSaveRequest>();
			AppendID(Result, Value.RequestingAccountID);
			Result.Appendf(TEXT("%d|"), static_cast<int32>(Value.Owner.Kind));
			AppendID(Result, Value.Owner.OwnerID);
			AppendField(Result, Value.Slot);
			Result.Appendf(
				TEXT("%d|%lld|"),
				static_cast<int32>(Value.Mode),
				Value.ExpectedRevision);
			AppendBytes(Result, Value.Data);
			break;
		}
		case ESeinOnlineOperation::SubmitTelemetry:
		{
			const auto& Value = Request.Payload.Get<FSeinOnlineSubmitTelemetryRequest>();
			AppendID(Result, Value.AccountID);
			for (const FSeinOnlineTelemetryEvent& Event : Value.Events)
			{
				AppendField(Result, Event.Event.ToString());
				Result.Appendf(TEXT("%lld|"), Event.TimestampUnixMilliseconds);
				TArray<FSeinOnlineAttribute> Attributes = Event.Attributes;
				Attributes.Sort([](const auto& A, const auto& B)
				{
					return A.Key.LexicalLess(B.Key);
				});
				for (const FSeinOnlineAttribute& Attribute : Attributes)
				{
					AppendField(Result, Attribute.Key.ToString());
					AppendField(Result, Attribute.Value);
				}
			}
			break;
		}
		default:
			break;
		}
		return Result;
	}

	bool IsSensitiveTelemetryKey(FName Key)
	{
		const FString Lower = Key.ToString().ToLower();
		return Lower.Contains(TEXT("token"))
			|| Lower.Contains(TEXT("credential"))
			|| Lower.Contains(TEXT("secret"))
			|| Lower.Contains(TEXT("password"));
	}

	bool CheckedAdd(int64 A, int64 B, int64& Out)
	{
		if ((B > 0 && A > TNumericLimits<int64>::Max() - B)
			|| (B < 0 && A < TNumericLimits<int64>::Lowest() - B))
		{
			return false;
		}
		Out = A + B;
		return true;
	}
}

struct FSeinOnlineLoopbackBackend
{
	FCriticalSection Mutex;
	int64 NextIdentity = 1;
	TMap<FString, FSeinOnlineOpaqueID> AccountsByCredential;
	TMap<FString, FSeinOnlineAccountRecord> Accounts;
	TMap<FString, FSeinOnlinePartyRecord> Parties;
	TMap<FString, FLoopbackInvite> Invites;
	TMap<FString, FLoopbackTicket> Tickets;
	TMap<FString, FLoopbackAllocation> Allocations;
	TMap<FString, FSeinOnlineMatchRecord> Matches;
	TMap<FString, FLoopbackReconnectCredential> ReconnectCredentials;
	TMap<FString, FLoopbackReconnectCredential> Admissions;
	TMap<FString, FSeinOnlineReplayEvidenceRecord> Replays;
	TMap<FString, TMap<FName, int64>> Stats;
	TMap<FString, FSeinOnlineCampaignSaveRecord> Saves;
	TMap<FString, FLoopbackIdempotencyRecord> Idempotency;
	TMap<FString, FLoopbackIdempotencyRecord> AllocationResultsByTicket;
	TMap<FString, FLoopbackIdempotencyRecord> RegisteredMatchResults;
	TMap<FString, FLoopbackIdempotencyRecord> RegisteredMatchResultsByAllocation;
	TMap<FString, FLoopbackIdempotencyRecord> TerminalMatchResults;
	int64 AcceptedTelemetryEvents = 0;

	FSeinOnlineOpaqueID NewID(const TCHAR* Prefix)
	{
		FSeinOnlineOpaqueID Result;
		Result.Provider = LoopbackProviderName;
		Result.Value = FString::Printf(TEXT("%s-%lld"), Prefix, NextIdentity++);
		return Result;
	}
};

namespace
{
	FSeinOnlineProviderResponse Failure(
		const FSeinOnlineProviderRequest& Request,
		ESeinOnlineErrorCode Code,
		const TCHAR* Message,
		bool bRetryable = false)
	{
		FSeinOnlineProviderResponse Response;
		Response.Handle = Request.Handle;
		Response.Operation = Request.Operation;
		Response.Error = FSeinOnlineError::Make(Code, Message, bRetryable);
		return Response;
	}

	template <typename TResult>
	FSeinOnlineProviderResponse Success(
		const FSeinOnlineProviderRequest& Request,
		TResult Result)
	{
		FSeinOnlineProviderResponse Response;
		Response.Handle = Request.Handle;
		Response.Operation = Request.Operation;
		Response.Payload = FInstancedStruct::Make(MoveTemp(Result));
		return Response;
	}

	bool IsAuthenticated(
		const TSet<FString>& AuthenticatedAccounts,
		const FSeinOnlineOpaqueID& AccountID)
	{
		return AuthenticatedAccounts.Contains(AccountID.ToCanonicalString());
	}

	bool PartyContains(
		const FSeinOnlinePartyRecord& Party,
		const FSeinOnlineOpaqueID& AccountID)
	{
		return Party.Members.ContainsByPredicate([&](const auto& Member)
		{
			return Member.AccountID == AccountID;
		});
	}

	void RefreshPartyLeaders(FSeinOnlinePartyRecord& Party)
	{
		for (FSeinOnlinePartyMember& Member : Party.Members)
		{
			Member.bLeader = Member.AccountID == Party.LeaderAccountID;
		}
	}

	FString SaveKey(
		const FSeinOnlineSaveOwner& Owner,
		const FString& Slot)
	{
		return FString::Printf(TEXT("%d|"), static_cast<int32>(Owner.Kind))
			+ Owner.OwnerID.ToCanonicalString() + TEXT("|") + Slot;
	}

	bool CanAccessSave(
		const FSeinOnlineLoopbackBackend& Backend,
		const TSet<FString>& AuthenticatedAccounts,
		const FSeinOnlineOpaqueID& RequestingAccountID,
		const FSeinOnlineSaveOwner& Owner)
	{
		if (!IsAuthenticated(AuthenticatedAccounts, RequestingAccountID))
		{
			return false;
		}
		if (Owner.Kind == ESeinOnlineSaveOwnerKind::Account)
		{
			return Owner.OwnerID == RequestingAccountID;
		}
		const FSeinOnlinePartyRecord* Party =
			Backend.Parties.Find(Owner.OwnerID.ToCanonicalString());
		return Party && PartyContains(*Party, RequestingAccountID);
	}

	FString IdempotencyRecordKey(
		ESeinOnlineOperation Operation,
		const FString& Scope,
		const FString& Key)
	{
		return FString::Printf(
			TEXT("%d|%d:%s|%s"),
			static_cast<int32>(Operation),
			Scope.Len(),
			*Scope,
			*Key);
	}

	bool ReplayIdempotentResult(
		FSeinOnlineLoopbackBackend& Backend,
		const FSeinOnlineProviderRequest& Request,
		const FString& Scope,
		const FString& Key,
		const FString& Fingerprint,
		FSeinOnlineProviderResponse& OutResponse)
	{
		const FLoopbackIdempotencyRecord* Existing =
			Backend.Idempotency.Find(
				IdempotencyRecordKey(Request.Operation, Scope, Key));
		if (!Existing)
		{
			return false;
		}
		if (Existing->Fingerprint != Fingerprint)
		{
			OutResponse = Failure(
				Request,
				ESeinOnlineErrorCode::Conflict,
				TEXT("The idempotency key was already used for a different payload."));
			return true;
		}
		OutResponse.Handle = Request.Handle;
		OutResponse.Operation = Request.Operation;
		OutResponse.Payload = Existing->Result;
		return true;
	}

	void StoreIdempotentResult(
		FSeinOnlineLoopbackBackend& Backend,
		const FSeinOnlineProviderRequest& Request,
		const FString& Scope,
		const FString& Key,
		FString Fingerprint,
		const FInstancedStruct& Result)
	{
		FLoopbackIdempotencyRecord Record;
		Record.Fingerprint = MoveTemp(Fingerprint);
		Record.Result = Result;
		Backend.Idempotency.Add(
			IdempotencyRecordKey(Request.Operation, Scope, Key),
			MoveTemp(Record));
	}
}

bool USeinOnlineLoopbackProvider::InitializeProvider(FSeinOnlineError& OutError)
{
	FScopeLock Lock(&GLoopbackBackendLifecycleMutex);
	Backend = GLoopbackBackend.Pin();
	if (!Backend.IsValid())
	{
		Backend = MakeShared<FSeinOnlineLoopbackBackend>();
		GLoopbackBackend = Backend;
	}
	OutError = FSeinOnlineError();
	return true;
}

void USeinOnlineLoopbackProvider::ShutdownProvider()
{
	LocalAccountsByUser.Reset();
	AuthenticatedAccounts.Reset();
	Backend.Reset();
}

FName USeinOnlineLoopbackProvider::GetProviderName() const
{
	return LoopbackProviderName;
}

bool USeinOnlineLoopbackProvider::SupportsOperation(
	ESeinOnlineOperation Operation) const
{
	return SeinOnlineContract::GetRequestStruct(Operation) != nullptr;
}

void USeinOnlineLoopbackProvider::BeginRequestProvider(
	const FSeinOnlineProviderRequest& Request,
	FSeinOnlineProviderCompletion&& Completion)
{
	if (!Completion)
	{
		return;
	}
	Completion(ProcessRequest(Request));
}

bool USeinOnlineLoopbackProvider::IsTrustedServerRequestAuthenticated(
	const FSeinOnlineProviderRequest& Request) const
{
	// Loopback accepts the local process role so tests can exercise trusted
	// paths. Production adapters must validate real backend credentials.
	return Request.Authority == ESeinOnlineCallerAuthority::TrustedServer;
}

FSeinOnlineConnectionAdmissionDecision
USeinOnlineLoopbackProvider::AuthorizeConnection(
	const FSeinOnlineConnectionAdmissionRequest& Request)
{
	FSeinOnlineConnectionAdmissionDecision Decision;
	Decision.Error = FSeinOnlineError::Make(
		ESeinOnlineErrorCode::Forbidden,
		TEXT("Online admission rejected."));
	if (!Backend.IsValid() || Request.AdmissionID.IsEmpty()
		|| Request.AdmissionID.Len()
			> SeinOnlineServicesContract::MaxIdentifierLength
		|| Request.PlatformIdentityType.Len()
			> SeinOnlineServicesContract::MaxIdentifierLength
		|| Request.PlatformIdentityValue.Len()
			> SeinOnlineServicesContract::MaxTextLength
		|| Request.PlatformIdentityType.IsEmpty()
			!= Request.PlatformIdentityValue.IsEmpty())
	{
		return Decision;
	}

	FScopeLock Lock(&Backend->Mutex);
	const FLoopbackReconnectCredential* Admission =
		Backend->Admissions.Find(Request.AdmissionID);
	if (!Admission)
	{
		return Decision;
	}
	const int64 Now = GetUtcNowUnixMilliseconds();
	if (Now >= Admission->ExpiresAtUnixMilliseconds)
	{
		Backend->ReconnectCredentials.Remove(Admission->Credential);
		Backend->Admissions.Remove(Request.AdmissionID);
		return Decision;
	}
	if (!Admission->PlatformIdentityType.IsEmpty()
		&& (Admission->PlatformIdentityType != Request.PlatformIdentityType
			|| Admission->PlatformIdentityValue
				!= Request.PlatformIdentityValue))
	{
		return Decision;
	}
	const FSeinOnlineMatchRecord* Match =
		Backend->Matches.Find(MatchKey(Admission->MatchID));
	if (!Match)
	{
		return Decision;
	}
	const FSeinOnlineMatchRosterEntry* RosterEntry =
		Match->Roster.FindByPredicate([&](const auto& Entry)
		{
			return Entry.AccountID == Admission->AccountID
				&& Entry.ParticipantID == Admission->ParticipantID
				&& Entry.PlayerID == Admission->PlayerID;
		});
	if (!RosterEntry)
	{
		return Decision;
	}
	const FSeinMatchInstanceID MatchID = Admission->MatchID;
	const FSeinNetworkParticipantID ParticipantID = Admission->ParticipantID;
	const FSeinPlayerID PlayerID = Admission->PlayerID;
	const FString Credential = Admission->Credential;
	Backend->ReconnectCredentials.Remove(Credential);
	Backend->Admissions.Remove(Request.AdmissionID);

	Decision.bAccepted = true;
	Decision.MatchID = MatchID;
	Decision.ParticipantID = ParticipantID;
	Decision.AssignedSlot = PlayerID;
	Decision.Error = FSeinOnlineError();
	return Decision;
}

int64 USeinOnlineLoopbackProvider::GetUtcNowUnixMilliseconds() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (bHasUtcNowOverride)
	{
		return UtcNowOverride;
	}
#endif
	return FDateTime::UtcNow().ToUnixTimestamp() * 1000;
}

#if WITH_DEV_AUTOMATION_TESTS
void USeinOnlineLoopbackProvider::SetUtcNowUnixMillisecondsForTests(
	int64 UnixMilliseconds)
{
	bHasUtcNowOverride = true;
	UtcNowOverride = UnixMilliseconds;
}

void USeinOnlineLoopbackProvider::ClearUtcNowOverrideForTests()
{
	bHasUtcNowOverride = false;
	UtcNowOverride = 0;
}
#endif

FSeinOnlineProviderResponse USeinOnlineLoopbackProvider::ProcessRequest(
	const FSeinOnlineProviderRequest& Request)
{
	FSeinOnlineError ValidationError;
	if (!SeinOnlineContract::ValidateRequest(Request, ValidationError))
	{
		FSeinOnlineProviderResponse Response;
		Response.Handle = Request.Handle;
		Response.Operation = Request.Operation;
		Response.Error = MoveTemp(ValidationError);
		return Response;
	}
	if (!SupportsOperation(Request.Operation) || !Backend.IsValid())
	{
		return Failure(
			Request,
			ESeinOnlineErrorCode::ProviderUnavailable,
			TEXT("The loopback provider is unavailable."),
			true);
	}

	FScopeLock Lock(&Backend->Mutex);
	switch (Request.Operation)
	{
	case ESeinOnlineOperation::Authenticate:
	{
		const auto& Value = Request.Payload.Get<FSeinOnlineAuthenticateRequest>();
		FSeinOnlineOpaqueID AccountID;
		const FSeinOnlineOpaqueID* Existing = Value.Credential.IsEmpty()
			? LocalAccountsByUser.Find(Value.LocalUserIndex)
			: Backend->AccountsByCredential.Find(Value.Credential);
		if (Existing)
		{
			AccountID = *Existing;
		}
		else
		{
			AccountID = Backend->NewID(TEXT("account"));
			if (Value.Credential.IsEmpty())
			{
				LocalAccountsByUser.Add(Value.LocalUserIndex, AccountID);
			}
			else
			{
				Backend->AccountsByCredential.Add(Value.Credential, AccountID);
			}
			FSeinOnlineAccountRecord Account;
			Account.AccountID = AccountID;
			Account.LocalUserIndex = INDEX_NONE;
			Account.DisplayName = Value.DisplayNameHint.IsEmpty()
				? FString::Printf(TEXT("Player %d"), Value.LocalUserIndex + 1)
				: Value.DisplayNameHint;
			Backend->Accounts.Add(AccountID.ToCanonicalString(), MoveTemp(Account));
		}
		AuthenticatedAccounts.Add(AccountID.ToCanonicalString());
		FSeinOnlineAuthenticateResult Result;
		Result.Account = Backend->Accounts.FindChecked(AccountID.ToCanonicalString());
		Result.Account.LocalUserIndex = Value.LocalUserIndex;
		return Success(Request, MoveTemp(Result));
	}
	case ESeinOnlineOperation::SignOut:
	{
		const auto& Value = Request.Payload.Get<FSeinOnlineSignOutRequest>();
		if (!IsAuthenticated(AuthenticatedAccounts, Value.AccountID))
		{
			return Failure(Request, ESeinOnlineErrorCode::NotAuthenticated,
				TEXT("The account is not authenticated."));
		}
		AuthenticatedAccounts.Remove(Value.AccountID.ToCanonicalString());
		FSeinOnlineMutationReceipt Result;
		Result.ReceiptID = Backend->NewID(TEXT("signout"));
		return Success(Request, MoveTemp(Result));
	}
	case ESeinOnlineOperation::CreateParty:
	{
		const auto& Value = Request.Payload.Get<FSeinOnlineCreatePartyRequest>();
		if (!IsAuthenticated(AuthenticatedAccounts, Value.LeaderAccountID))
		{
			return Failure(Request, ESeinOnlineErrorCode::NotAuthenticated,
				TEXT("The party leader is not authenticated."));
		}
		const FString Fingerprint = CanonicalDurablePayload(Request);
		const FString IdempotencyScope =
			Value.LeaderAccountID.ToCanonicalString();
		FSeinOnlineProviderResponse Replayed;
		if (ReplayIdempotentResult(*Backend, Request, IdempotencyScope,
			Value.IdempotencyKey, Fingerprint, Replayed))
		{
			return Replayed;
		}
		FSeinOnlinePartyRecord Party;
		Party.PartyID = Backend->NewID(TEXT("party"));
		Party.LeaderAccountID = Value.LeaderAccountID;
		Party.MaxMembers = Value.MaxMembers;
		FSeinOnlinePartyMember Member;
		Member.AccountID = Value.LeaderAccountID;
		Member.bLeader = true;
		Party.Members.Add(Member);
		Backend->Parties.Add(Party.PartyID.ToCanonicalString(), Party);
		FSeinOnlinePartyResult Result;
		Result.Party = MoveTemp(Party);
		FSeinOnlineProviderResponse Response = Success(Request, MoveTemp(Result));
		StoreIdempotentResult(*Backend, Request, IdempotencyScope,
			Value.IdempotencyKey, Fingerprint, Response.Payload);
		return Response;
	}
	case ESeinOnlineOperation::InviteToParty:
	{
		const auto& Value = Request.Payload.Get<FSeinOnlineInviteToPartyRequest>();
		FSeinOnlinePartyRecord* Party =
			Backend->Parties.Find(Value.PartyID.ToCanonicalString());
		if (!Party)
		{
			return Failure(Request, ESeinOnlineErrorCode::NotFound,
				TEXT("The party was not found."));
		}
		if (!IsAuthenticated(AuthenticatedAccounts, Value.InviterAccountID)
			|| !PartyContains(*Party, Value.InviterAccountID))
		{
			return Failure(Request, ESeinOnlineErrorCode::Forbidden,
				TEXT("Only an authenticated party member may invite."));
		}
		if (!Backend->Accounts.Contains(Value.InviteeAccountID.ToCanonicalString()))
		{
			return Failure(Request, ESeinOnlineErrorCode::NotFound,
				TEXT("The invitee account was not found."));
		}
		if (PartyContains(*Party, Value.InviteeAccountID))
		{
			return Failure(Request, ESeinOnlineErrorCode::Conflict,
				TEXT("The invitee is already a party member."));
		}
		FLoopbackInvite Invite;
		Invite.InviteID = Backend->NewID(TEXT("invite"));
		Invite.PartyID = Value.PartyID;
		Invite.InviteeAccountID = Value.InviteeAccountID;
		Backend->Invites.Add(Invite.InviteID.ToCanonicalString(), Invite);
		FSeinOnlinePartyInviteResult Result;
		Result.InviteID = Invite.InviteID;
		Result.PartyID = Invite.PartyID;
		Result.InviteeAccountID = Invite.InviteeAccountID;
		return Success(Request, MoveTemp(Result));
	}
	case ESeinOnlineOperation::JoinParty:
	{
		const auto& Value = Request.Payload.Get<FSeinOnlineJoinPartyRequest>();
		const FLoopbackInvite* Invite =
			Backend->Invites.Find(Value.InviteID.ToCanonicalString());
		if (!Invite || Invite->InviteeAccountID != Value.AccountID)
		{
			return Failure(Request, ESeinOnlineErrorCode::NotFound,
				TEXT("The party invitation was not found."));
		}
		if (!IsAuthenticated(AuthenticatedAccounts, Value.AccountID))
		{
			return Failure(Request, ESeinOnlineErrorCode::NotAuthenticated,
				TEXT("The joining account is not authenticated."));
		}
		FSeinOnlinePartyRecord* Party =
			Backend->Parties.Find(Invite->PartyID.ToCanonicalString());
		if (!Party)
		{
			return Failure(Request, ESeinOnlineErrorCode::NotFound,
				TEXT("The invited party no longer exists."));
		}
		if (Party->Members.Num() >= Party->MaxMembers)
		{
			return Failure(Request, ESeinOnlineErrorCode::CapacityExceeded,
				TEXT("The party is full."));
		}
		FSeinOnlinePartyMember Member;
		Member.AccountID = Value.AccountID;
		Party->Members.Add(Member);
		++Party->Revision;
		Backend->Invites.Remove(Value.InviteID.ToCanonicalString());
		FSeinOnlinePartyResult Result;
		Result.Party = *Party;
		return Success(Request, MoveTemp(Result));
	}
	case ESeinOnlineOperation::LeaveParty:
	{
		const auto& Value = Request.Payload.Get<FSeinOnlineLeavePartyRequest>();
		if (!IsAuthenticated(AuthenticatedAccounts, Value.AccountID))
		{
			return Failure(Request, ESeinOnlineErrorCode::NotAuthenticated,
				TEXT("The leaving account is not authenticated."));
		}
		FSeinOnlinePartyRecord* Party =
			Backend->Parties.Find(Value.PartyID.ToCanonicalString());
		if (!Party || !PartyContains(*Party, Value.AccountID))
		{
			return Failure(Request, ESeinOnlineErrorCode::NotFound,
				TEXT("The party membership was not found."));
		}
		Party->Members.RemoveAll([&](const auto& Member)
		{
			return Member.AccountID == Value.AccountID;
		});
		if (Party->Members.IsEmpty())
		{
			Backend->Parties.Remove(Value.PartyID.ToCanonicalString());
		}
		else
		{
			if (Party->LeaderAccountID == Value.AccountID)
			{
				Party->Members.Sort([](const auto& A, const auto& B)
				{
					return A.AccountID.ToCanonicalString()
						< B.AccountID.ToCanonicalString();
				});
				Party->LeaderAccountID = Party->Members[0].AccountID;
			}
			++Party->Revision;
			RefreshPartyLeaders(*Party);
		}
		FSeinOnlineMutationReceipt Result;
		Result.ReceiptID = Backend->NewID(TEXT("leave"));
		return Success(Request, MoveTemp(Result));
	}
	case ESeinOnlineOperation::QueryParty:
	{
		const auto& Value = Request.Payload.Get<FSeinOnlineQueryPartyRequest>();
		const FSeinOnlinePartyRecord* Party =
			Backend->Parties.Find(Value.PartyID.ToCanonicalString());
		if (!Party)
		{
			return Failure(Request, ESeinOnlineErrorCode::NotFound,
				TEXT("The party was not found."));
		}
		FSeinOnlinePartyResult Result;
		Result.Party = *Party;
		return Success(Request, MoveTemp(Result));
	}
	case ESeinOnlineOperation::StartMatchmaking:
	{
		const auto& Value = Request.Payload.Get<FSeinOnlineStartMatchmakingRequest>();
		if (!IsAuthenticated(AuthenticatedAccounts, Value.AccountID))
		{
			return Failure(Request, ESeinOnlineErrorCode::NotAuthenticated,
				TEXT("The matchmaking account is not authenticated."));
		}
		if (Value.PartyID.IsValid())
		{
			const FSeinOnlinePartyRecord* Party =
				Backend->Parties.Find(Value.PartyID.ToCanonicalString());
			if (!Party || !PartyContains(*Party, Value.AccountID))
			{
				return Failure(Request, ESeinOnlineErrorCode::Forbidden,
					TEXT("The account is not a member of the matchmaking party."));
			}
		}
		const FString Fingerprint = CanonicalDurablePayload(Request);
		const FString IdempotencyScope = Value.AccountID.ToCanonicalString();
		FSeinOnlineProviderResponse Replayed;
		if (ReplayIdempotentResult(*Backend, Request, IdempotencyScope,
			Value.IdempotencyKey, Fingerprint, Replayed))
		{
			return Replayed;
		}
		FLoopbackTicket Ticket;
		Ticket.Ticket.TicketID = Backend->NewID(TEXT("ticket"));
		Ticket.Ticket.Queue = Value.Queue;
		Ticket.Ticket.Region = Value.Region;
		Ticket.AccountID = Value.AccountID;
		Ticket.PartyID = Value.PartyID;
		Backend->Tickets.Add(Ticket.Ticket.TicketID.ToCanonicalString(), Ticket);
		FSeinOnlineMatchmakingResult Result;
		Result.Ticket = Ticket.Ticket;
		FSeinOnlineProviderResponse Response = Success(Request, MoveTemp(Result));
		StoreIdempotentResult(*Backend, Request, IdempotencyScope,
			Value.IdempotencyKey, Fingerprint, Response.Payload);
		return Response;
	}
	case ESeinOnlineOperation::QueryMatchmaking:
	{
		const auto& Value = Request.Payload.Get<FSeinOnlineTicketRequest>();
		const FLoopbackTicket* Ticket =
			Backend->Tickets.Find(Value.TicketID.ToCanonicalString());
		if (!Ticket)
		{
			return Failure(Request, ESeinOnlineErrorCode::NotFound,
				TEXT("The matchmaking ticket was not found."));
		}
		FSeinOnlineMatchmakingResult Result;
		Result.Ticket = Ticket->Ticket;
		return Success(Request, MoveTemp(Result));
	}
	case ESeinOnlineOperation::CancelMatchmaking:
	{
		const auto& Value = Request.Payload.Get<FSeinOnlineTicketRequest>();
		FLoopbackTicket* Ticket =
			Backend->Tickets.Find(Value.TicketID.ToCanonicalString());
		if (!Ticket)
		{
			return Failure(Request, ESeinOnlineErrorCode::NotFound,
				TEXT("The matchmaking ticket was not found."));
		}
		if (Ticket->Ticket.State == ESeinOnlineMatchmakingState::Matched)
		{
			return Failure(Request, ESeinOnlineErrorCode::Conflict,
				TEXT("A matched ticket cannot be cancelled."));
		}
		Ticket->Ticket.State = ESeinOnlineMatchmakingState::Cancelled;
		FSeinOnlineMutationReceipt Result;
		Result.ReceiptID = Backend->NewID(TEXT("cancel"));
		return Success(Request, MoveTemp(Result));
	}
	case ESeinOnlineOperation::AllocateServer:
	{
		if (!IsTrustedServerRequestAuthenticated(Request))
		{
			return Failure(Request, ESeinOnlineErrorCode::Forbidden,
				TEXT("Server allocation requires authenticated server authority."));
		}
		const auto& Value = Request.Payload.Get<FSeinOnlineAllocateServerRequest>();
		FLoopbackTicket* Ticket =
			Backend->Tickets.Find(Value.TicketID.ToCanonicalString());
		if (!Ticket || Ticket->Ticket.State == ESeinOnlineMatchmakingState::Cancelled)
		{
			return Failure(Request, ESeinOnlineErrorCode::NotFound,
				TEXT("An active matchmaking ticket is required."));
		}
		const FString Fingerprint = CanonicalDurablePayload(Request);
		const FString IdempotencyScope = Value.TicketID.ToCanonicalString();
		FSeinOnlineProviderResponse Replayed;
		if (ReplayIdempotentResult(*Backend, Request, IdempotencyScope,
			Value.IdempotencyKey, Fingerprint, Replayed))
		{
			return Replayed;
		}
		if (const FLoopbackIdempotencyRecord* Existing =
			Backend->AllocationResultsByTicket.Find(IdempotencyScope))
		{
			if (Existing->Fingerprint != Fingerprint)
			{
				return Failure(Request, ESeinOnlineErrorCode::Conflict,
					TEXT("The matchmaking ticket is already allocated differently."));
			}
			FSeinOnlineProviderResponse Response;
			Response.Handle = Request.Handle;
			Response.Operation = Request.Operation;
			Response.Payload = Existing->Result;
			StoreIdempotentResult(*Backend, Request, IdempotencyScope,
				Value.IdempotencyKey, Fingerprint, Response.Payload);
			return Response;
		}
		if (Ticket->Ticket.State == ESeinOnlineMatchmakingState::Matched)
		{
			return Failure(Request, ESeinOnlineErrorCode::Conflict,
				TEXT("The matched ticket has no reusable allocation record."));
		}
		Ticket->Ticket.State = ESeinOnlineMatchmakingState::Matched;
		FSeinOnlineServerAllocationResult Result;
		Result.AllocationID = Backend->NewID(TEXT("allocation"));
		Result.Host = TEXT("127.0.0.1");
		Result.Port = 7777;
		Result.LeaseSecret = Backend->NewID(TEXT("lease")).Value;
		FLoopbackAllocation Allocation;
		Allocation.AllocationID = Result.AllocationID;
		Allocation.TicketID = Value.TicketID;
		Allocation.LeaseSecret = Result.LeaseSecret;
		Backend->Allocations.Add(
			Allocation.AllocationID.ToCanonicalString(), Allocation);
		FSeinOnlineProviderResponse Response = Success(Request, MoveTemp(Result));
		FLoopbackIdempotencyRecord SemanticAllocation;
		SemanticAllocation.Fingerprint = Fingerprint;
		SemanticAllocation.Result = Response.Payload;
		Backend->AllocationResultsByTicket.Add(
			IdempotencyScope, MoveTemp(SemanticAllocation));
		StoreIdempotentResult(*Backend, Request, IdempotencyScope,
			Value.IdempotencyKey, Fingerprint, Response.Payload);
		return Response;
	}
	case ESeinOnlineOperation::RegisterMatch:
	{
		if (!IsTrustedServerRequestAuthenticated(Request))
		{
			return Failure(Request, ESeinOnlineErrorCode::Forbidden,
				TEXT("Match registration requires authenticated server authority."));
		}
		const auto& Value = Request.Payload.Get<FSeinOnlineRegisterMatchRequest>();
		const FString Key = MatchKey(Value.MatchID);
		const FString Fingerprint = CanonicalDurablePayload(Request);
		FSeinOnlineProviderResponse Replayed;
		if (ReplayIdempotentResult(*Backend, Request, Key,
			Value.IdempotencyKey, Fingerprint, Replayed))
		{
			return Replayed;
		}
		if (const FLoopbackIdempotencyRecord* Existing =
			Backend->RegisteredMatchResults.Find(Key))
		{
			if (Existing->Fingerprint != Fingerprint)
			{
				return Failure(Request, ESeinOnlineErrorCode::Conflict,
					TEXT("The match is already registered differently."));
			}
			FSeinOnlineProviderResponse Response;
			Response.Handle = Request.Handle;
			Response.Operation = Request.Operation;
			Response.Payload = Existing->Result;
			StoreIdempotentResult(*Backend, Request, Key,
				Value.IdempotencyKey, Fingerprint, Response.Payload);
			return Response;
		}
		const FString AllocationKey = Value.AllocationID.ToCanonicalString();
		if (const FLoopbackIdempotencyRecord* Existing =
			Backend->RegisteredMatchResultsByAllocation.Find(AllocationKey))
		{
			if (Existing->Fingerprint != Fingerprint)
			{
				return Failure(Request, ESeinOnlineErrorCode::Conflict,
					TEXT("The server allocation is already bound to another match."));
			}
			FSeinOnlineProviderResponse Response;
			Response.Handle = Request.Handle;
			Response.Operation = Request.Operation;
			Response.Payload = Existing->Result;
			StoreIdempotentResult(*Backend, Request, Key,
				Value.IdempotencyKey, Fingerprint, Response.Payload);
			return Response;
		}
		const FLoopbackAllocation* Allocation =
			Backend->Allocations.Find(AllocationKey);
		const FLoopbackTicket* Ticket = Allocation
			? Backend->Tickets.Find(Allocation->TicketID.ToCanonicalString())
			: nullptr;
		if (!Allocation || !Ticket)
		{
			return Failure(Request, ESeinOnlineErrorCode::NotFound,
				TEXT("The server allocation was not found."));
		}
		for (const FSeinOnlineMatchRosterEntry& Entry : Value.Roster)
		{
			if (!Backend->Accounts.Contains(Entry.AccountID.ToCanonicalString()))
			{
				return Failure(Request, ESeinOnlineErrorCode::NotFound,
					TEXT("A roster account was not found."));
			}
			const bool bAllowedByTicket = Ticket->AccountID == Entry.AccountID
				|| (Ticket->PartyID.IsValid()
					&& [&]()
					{
						const FSeinOnlinePartyRecord* Party = Backend->Parties.Find(
							Ticket->PartyID.ToCanonicalString());
						return Party && PartyContains(*Party, Entry.AccountID);
					}());
			if (!bAllowedByTicket)
			{
				return Failure(Request, ESeinOnlineErrorCode::Forbidden,
					TEXT("A roster account is not covered by the server allocation."));
			}
		}
		FSeinOnlineMatchRecord Match;
		Match.MatchID = Value.MatchID;
		Match.AllocationID = Value.AllocationID;
		Match.Classification = Value.Classification;
		Match.Roster = Value.Roster;
		Backend->Matches.Add(Key, Match);
		FSeinOnlineRegisterMatchResult Result;
		Result.Match = MoveTemp(Match);
		FSeinOnlineProviderResponse Response = Success(Request, MoveTemp(Result));
		FLoopbackIdempotencyRecord Registered;
		Registered.Fingerprint = Fingerprint;
		Registered.Result = Response.Payload;
		Backend->RegisteredMatchResults.Add(Key, Registered);
		Backend->RegisteredMatchResultsByAllocation.Add(
			AllocationKey, MoveTemp(Registered));
		StoreIdempotentResult(*Backend, Request, Key,
			Value.IdempotencyKey, Fingerprint, Response.Payload);
		return Response;
	}
	case ESeinOnlineOperation::IssueReconnectCredential:
	{
		if (!IsTrustedServerRequestAuthenticated(Request))
		{
			return Failure(Request, ESeinOnlineErrorCode::Forbidden,
				TEXT("Reconnect issuance requires authenticated server authority."));
		}
		const auto& Value =
			Request.Payload.Get<FSeinOnlineIssueReconnectCredentialRequest>();
		const FSeinOnlineMatchRecord* Match = Backend->Matches.Find(MatchKey(Value.MatchID));
		if (!Match)
		{
			return Failure(Request, ESeinOnlineErrorCode::NotFound,
				TEXT("The registered match was not found."));
		}
		const FSeinOnlineMatchRosterEntry* RosterEntry =
			Match->Roster.FindByPredicate([&](const auto& Entry)
			{
				return Entry.ParticipantID == Value.ParticipantID;
			});
		if (!RosterEntry)
		{
			return Failure(Request, ESeinOnlineErrorCode::Forbidden,
				TEXT("The participant is not registered for this match."));
		}
		const FString Fingerprint = CanonicalDurablePayload(Request);
		const FString IdempotencyScope = MatchKey(Value.MatchID)
			+ TEXT("|") + ParticipantKey(Value.ParticipantID);
		FSeinOnlineProviderResponse Replayed;
		if (ReplayIdempotentResult(*Backend, Request, IdempotencyScope,
			Value.IdempotencyKey, Fingerprint, Replayed))
		{
			return Replayed;
		}
		const FString Credential = Backend->NewID(TEXT("reconnect")).Value;
		const FString AdmissionID = Backend->NewID(TEXT("admission")).Value;
		FLoopbackReconnectCredential Stored;
		Stored.MatchID = Value.MatchID;
		Stored.ParticipantID = Value.ParticipantID;
		Stored.AccountID = RosterEntry->AccountID;
		Stored.PlayerID = RosterEntry->PlayerID;
		Stored.AdmissionID = AdmissionID;
		Stored.Credential = Credential;
		Stored.PlatformIdentityType = Value.PlatformIdentityType;
		Stored.PlatformIdentityValue = Value.PlatformIdentityValue;
		Stored.ExpiresAtUnixMilliseconds = GetUtcNowUnixMilliseconds()
			+ static_cast<int64>(Value.LifetimeSeconds) * 1000;
		Backend->ReconnectCredentials.Add(Credential, Stored);
		Backend->Admissions.Add(AdmissionID, Stored);
		FSeinOnlineReconnectCredentialResult Result;
		Result.AdmissionID = AdmissionID;
		Result.Credential = Credential;
		Result.ExpiresAtUnixMilliseconds = Stored.ExpiresAtUnixMilliseconds;
		FSeinOnlineProviderResponse Response = Success(Request, MoveTemp(Result));
		StoreIdempotentResult(*Backend, Request, IdempotencyScope,
			Value.IdempotencyKey, Fingerprint, Response.Payload);
		return Response;
	}
	case ESeinOnlineOperation::ValidateReconnectCredential:
	{
		if (!IsTrustedServerRequestAuthenticated(Request))
		{
			return Failure(Request, ESeinOnlineErrorCode::Forbidden,
				TEXT("Reconnect validation requires authenticated server authority."));
		}
		const auto& Value =
			Request.Payload.Get<FSeinOnlineValidateReconnectCredentialRequest>();
		const FLoopbackReconnectCredential* Stored =
			Backend->ReconnectCredentials.Find(Value.Credential);
		if (!Stored || Stored->MatchID != Value.MatchID
			|| Stored->ParticipantID != Value.ParticipantID)
		{
			return Failure(Request, ESeinOnlineErrorCode::Forbidden,
				TEXT("The reconnect credential is invalid."));
		}
		if (GetUtcNowUnixMilliseconds() >= Stored->ExpiresAtUnixMilliseconds)
		{
			Backend->Admissions.Remove(Stored->AdmissionID);
			Backend->ReconnectCredentials.Remove(Value.Credential);
			return Failure(Request, ESeinOnlineErrorCode::Expired,
				TEXT("The reconnect credential has expired."));
		}
		FSeinOnlineValidateReconnectCredentialResult Result;
		Result.AccountID = Stored->AccountID;
		return Success(Request, MoveTemp(Result));
	}
	case ESeinOnlineOperation::SubmitMatchResult:
	{
		const auto& Value = Request.Payload.Get<FSeinOnlineSubmitMatchResultRequest>();
		const FSeinOnlineMatchRecord* Match = Backend->Matches.Find(MatchKey(Value.MatchID));
		if (!Match)
		{
			return Failure(Request, ESeinOnlineErrorCode::NotFound,
				TEXT("The registered match was not found."));
		}
		if (Match->Classification == ESeinOnlineMatchClassification::Ranked
			&& !IsTrustedServerRequestAuthenticated(Request))
		{
			return Failure(Request, ESeinOnlineErrorCode::Forbidden,
				TEXT("Ranked results require authenticated server authority."));
		}
		const FString Fingerprint = CanonicalDurablePayload(Request);
		const FString IdempotencyScope = MatchKey(Value.MatchID);
		FSeinOnlineProviderResponse Replayed;
		if (ReplayIdempotentResult(*Backend, Request, IdempotencyScope,
			Value.IdempotencyKey, Fingerprint, Replayed))
		{
			return Replayed;
		}
		if (const FLoopbackIdempotencyRecord* Existing =
			Backend->TerminalMatchResults.Find(IdempotencyScope))
		{
			if (Existing->Fingerprint != Fingerprint)
			{
				return Failure(Request, ESeinOnlineErrorCode::Conflict,
					TEXT("The match already has a different terminal result."));
			}
			FSeinOnlineProviderResponse Response;
			Response.Handle = Request.Handle;
			Response.Operation = Request.Operation;
			Response.Payload = Existing->Result;
			StoreIdempotentResult(*Backend, Request, IdempotencyScope,
				Value.IdempotencyKey, Fingerprint, Response.Payload);
			return Response;
		}
		const FSeinOnlineReplayEvidenceRecord* Replay =
			Backend->Replays.Find(Value.ReplayEvidenceID.ToCanonicalString());
		if (!Replay || Replay->MatchID != Value.MatchID
			|| Replay->TerminalTick != Value.TerminalTick
			|| Replay->FinalWorldRoot != Value.FinalWorldRoot
			|| Replay->ReplayFinalDigest != Value.ReplayFinalDigest)
		{
			return Failure(Request, ESeinOnlineErrorCode::Conflict,
				TEXT("The terminal result does not match its replay evidence."));
		}
		if (Value.Placements.Num() != Match->Roster.Num())
		{
			return Failure(Request, ESeinOnlineErrorCode::Conflict,
				TEXT("The result does not cover the registered roster."));
		}
		for (const FSeinOnlineMatchPlacement& Placement : Value.Placements)
		{
			if (!Match->Roster.ContainsByPredicate([&](const auto& Entry)
				{ return Entry.AccountID == Placement.AccountID; }))
			{
				return Failure(Request, ESeinOnlineErrorCode::Conflict,
					TEXT("The result contains an unregistered account."));
			}
		}
		FSeinOnlineMutationReceipt Receipt;
		Receipt.ReceiptID = Backend->NewID(TEXT("result"));
		FSeinOnlineProviderResponse Response = Success(Request, Receipt);
		FLoopbackIdempotencyRecord Terminal;
		Terminal.Fingerprint = Fingerprint;
		Terminal.Result = Response.Payload;
		Backend->TerminalMatchResults.Add(
			IdempotencyScope, MoveTemp(Terminal));
		StoreIdempotentResult(*Backend, Request, IdempotencyScope,
			Value.IdempotencyKey, Fingerprint, Response.Payload);
		return Response;
	}
	case ESeinOnlineOperation::WriteStats:
	{
		const auto& Value = Request.Payload.Get<FSeinOnlineWriteStatsRequest>();
		const FSeinOnlineMatchRecord* Match = Backend->Matches.Find(MatchKey(Value.MatchID));
		if (!Match || !Match->Roster.ContainsByPredicate([&](const auto& Entry)
			{ return Entry.AccountID == Value.AccountID; }))
		{
			return Failure(Request, ESeinOnlineErrorCode::Forbidden,
				TEXT("The account is not registered for this match."));
		}
		if (Match->Classification == ESeinOnlineMatchClassification::Ranked
			&& !IsTrustedServerRequestAuthenticated(Request))
		{
			return Failure(Request, ESeinOnlineErrorCode::Forbidden,
				TEXT("Ranked stat writes require authenticated server authority."));
		}
		const FString Fingerprint = CanonicalDurablePayload(Request);
		const FString IdempotencyScope = Value.AccountID.ToCanonicalString();
		FSeinOnlineProviderResponse Replayed;
		if (ReplayIdempotentResult(*Backend, Request, IdempotencyScope,
			Value.IdempotencyKey, Fingerprint, Replayed))
		{
			return Replayed;
		}
		TMap<FName, int64> Updated =
			Backend->Stats.FindRef(Value.AccountID.ToCanonicalString());
		for (const FSeinOnlineStatMutation& Mutation : Value.Mutations)
		{
			const int64 Current = Updated.FindRef(Mutation.Stat);
			int64 Next = Mutation.Value;
			switch (Mutation.Kind)
			{
			case ESeinOnlineStatMutationKind::Add:
				if (!CheckedAdd(Current, Mutation.Value, Next))
				{
					return Failure(Request, ESeinOnlineErrorCode::InvalidRequest,
						TEXT("A statistic mutation overflowed its integer range."));
				}
				break;
			case ESeinOnlineStatMutationKind::Max: Next = FMath::Max(Current, Mutation.Value); break;
			case ESeinOnlineStatMutationKind::Min: Next = FMath::Min(Current, Mutation.Value); break;
			default: break;
			}
			Updated.Add(Mutation.Stat, Next);
		}
		Backend->Stats.Add(Value.AccountID.ToCanonicalString(), MoveTemp(Updated));
		FSeinOnlineMutationReceipt Receipt;
		Receipt.ReceiptID = Backend->NewID(TEXT("stats"));
		FSeinOnlineProviderResponse Response = Success(Request, Receipt);
		StoreIdempotentResult(*Backend, Request, IdempotencyScope,
			Value.IdempotencyKey, Fingerprint, Response.Payload);
		return Response;
	}
	case ESeinOnlineOperation::QueryStats:
	{
		const auto& Value = Request.Payload.Get<FSeinOnlineQueryStatsRequest>();
		if (!Backend->Accounts.Contains(Value.AccountID.ToCanonicalString()))
		{
			return Failure(Request, ESeinOnlineErrorCode::NotFound,
				TEXT("The account was not found."));
		}
		const TMap<FName, int64> Stored =
			Backend->Stats.FindRef(Value.AccountID.ToCanonicalString());
		FSeinOnlineQueryStatsResult Result;
		Result.AccountID = Value.AccountID;
		for (const TPair<FName, int64>& Pair : Stored)
		{
			if (!Value.Stats.IsEmpty() && !Value.Stats.Contains(Pair.Key))
			{
				continue;
			}
			FSeinOnlineStatValue& Stat = Result.Stats.AddDefaulted_GetRef();
			Stat.Stat = Pair.Key;
			Stat.Value = Pair.Value;
		}
		Result.Stats.Sort([](const auto& A, const auto& B)
		{
			return A.Stat.LexicalLess(B.Stat);
		});
		return Success(Request, MoveTemp(Result));
	}
	case ESeinOnlineOperation::QueryLeaderboard:
	{
		const auto& Value = Request.Payload.Get<FSeinOnlineQueryLeaderboardRequest>();
		TArray<FSeinOnlineLeaderboardEntry> All;
		for (const TPair<FString, TMap<FName, int64>>& AccountStats : Backend->Stats)
		{
			const int64* Score = AccountStats.Value.Find(Value.Stat);
			const FSeinOnlineAccountRecord* Account = Backend->Accounts.Find(AccountStats.Key);
			if (!Score || !Account)
			{
				continue;
			}
			FSeinOnlineLeaderboardEntry& Entry = All.AddDefaulted_GetRef();
			Entry.AccountID = Account->AccountID;
			Entry.Score = *Score;
		}
		All.Sort([](const auto& A, const auto& B)
		{
			return A.Score != B.Score
				? A.Score > B.Score
				: A.AccountID.ToCanonicalString() < B.AccountID.ToCanonicalString();
		});
		for (int32 Index = 0; Index < All.Num(); ++Index)
		{
			All[Index].Rank = Index + 1;
		}
		FSeinOnlineQueryLeaderboardResult Result;
		Result.Stat = Value.Stat;
		const int32 End = FMath::Min(All.Num(), Value.Offset + Value.Limit);
		for (int32 Index = FMath::Min(Value.Offset, All.Num()); Index < End; ++Index)
		{
			Result.Entries.Add(All[Index]);
		}
		return Success(Request, MoveTemp(Result));
	}
	case ESeinOnlineOperation::PublishReplayEvidence:
	{
		const auto& Value =
			Request.Payload.Get<FSeinOnlinePublishReplayEvidenceRequest>();
		if (!IsTrustedServerRequestAuthenticated(Request))
		{
			return Failure(Request, ESeinOnlineErrorCode::Forbidden,
				TEXT("Replay evidence publication requires authenticated server authority."));
		}
		if (!Backend->Matches.Contains(MatchKey(Value.MatchID)))
		{
			return Failure(Request, ESeinOnlineErrorCode::NotFound,
				TEXT("The registered match was not found."));
		}
		const FString Fingerprint = CanonicalDurablePayload(Request);
		const FString IdempotencyScope = MatchKey(Value.MatchID);
		FSeinOnlineProviderResponse Replayed;
		if (ReplayIdempotentResult(*Backend, Request, IdempotencyScope,
			Value.IdempotencyKey, Fingerprint, Replayed))
		{
			return Replayed;
		}
		FSeinOnlineReplayEvidenceRecord Record;
		Record.ReplayID = Backend->NewID(TEXT("replay"));
		Record.MatchID = Value.MatchID;
		Record.FinalWorldRoot = Value.FinalWorldRoot;
		Record.TerminalTick = Value.TerminalTick;
		Record.ReplayFinalDigest = Value.ReplayFinalDigest;
		Record.Evidence = Value.Evidence;
		Backend->Replays.Add(Record.ReplayID.ToCanonicalString(), Record);
		FSeinOnlinePublishReplayEvidenceResult Result;
		Result.Replay = MoveTemp(Record);
		FSeinOnlineProviderResponse Response = Success(Request, MoveTemp(Result));
		StoreIdempotentResult(*Backend, Request, IdempotencyScope,
			Value.IdempotencyKey, Fingerprint, Response.Payload);
		return Response;
	}
	case ESeinOnlineOperation::QueryReplayEvidence:
	{
		const auto& Value = Request.Payload.Get<FSeinOnlineQueryReplayEvidenceRequest>();
		const FSeinOnlineReplayEvidenceRecord* Replay =
			Backend->Replays.Find(Value.ReplayID.ToCanonicalString());
		if (!Replay)
		{
			return Failure(Request, ESeinOnlineErrorCode::NotFound,
				TEXT("The replay evidence was not found."));
		}
		return Success(Request, *Replay);
	}
	case ESeinOnlineOperation::WriteCampaignSave:
	{
		const auto& Value = Request.Payload.Get<FSeinOnlineWriteCampaignSaveRequest>();
		if (!CanAccessSave(
				*Backend, AuthenticatedAccounts,
				Value.RequestingAccountID, Value.Owner))
		{
			return Failure(Request, ESeinOnlineErrorCode::Forbidden,
				TEXT("The account cannot write this save owner."));
		}
		const FString Fingerprint = CanonicalDurablePayload(Request);
		const FString IdempotencyScope =
			Value.RequestingAccountID.ToCanonicalString();
		FSeinOnlineProviderResponse Replayed;
		if (ReplayIdempotentResult(*Backend, Request, IdempotencyScope,
			Value.IdempotencyKey, Fingerprint, Replayed))
		{
			return Replayed;
		}
		const FString Key = SaveKey(Value.Owner, Value.Slot);
		const FSeinOnlineCampaignSaveRecord* Existing = Backend->Saves.Find(Key);
		if ((Value.Mode == ESeinOnlineSaveWriteMode::CreateOnly && Existing)
			|| (Value.Mode == ESeinOnlineSaveWriteMode::IfRevision
				&& (!Existing || Existing->Revision != Value.ExpectedRevision)))
		{
			return Failure(Request, ESeinOnlineErrorCode::Conflict,
				TEXT("The campaign save revision changed."));
		}
		FSeinOnlineCampaignSaveRecord Record;
		Record.Owner = Value.Owner;
		Record.Slot = Value.Slot;
		Record.Revision = Existing ? Existing->Revision + 1 : 1;
		Record.Data = Value.Data;
		Backend->Saves.Add(Key, Record);
		FSeinOnlineCampaignSaveResult Result;
		Result.Save = MoveTemp(Record);
		FSeinOnlineProviderResponse Response = Success(Request, MoveTemp(Result));
		StoreIdempotentResult(*Backend, Request, IdempotencyScope,
			Value.IdempotencyKey, Fingerprint, Response.Payload);
		return Response;
	}
	case ESeinOnlineOperation::ReadCampaignSave:
	{
		const auto& Value = Request.Payload.Get<FSeinOnlineReadCampaignSaveRequest>();
		if (!CanAccessSave(
				*Backend, AuthenticatedAccounts,
				Value.RequestingAccountID, Value.Owner))
		{
			return Failure(Request, ESeinOnlineErrorCode::Forbidden,
				TEXT("The account cannot read this save owner."));
		}
		const FSeinOnlineCampaignSaveRecord* Record =
			Backend->Saves.Find(SaveKey(Value.Owner, Value.Slot));
		if (!Record)
		{
			return Failure(Request, ESeinOnlineErrorCode::NotFound,
				TEXT("The campaign save was not found."));
		}
		FSeinOnlineCampaignSaveResult Result;
		Result.Save = *Record;
		return Success(Request, MoveTemp(Result));
	}
	case ESeinOnlineOperation::QueryCampaignSaves:
	{
		const auto& Value = Request.Payload.Get<FSeinOnlineQueryCampaignSavesRequest>();
		if (!CanAccessSave(
				*Backend, AuthenticatedAccounts,
				Value.RequestingAccountID, Value.Owner))
		{
			return Failure(Request, ESeinOnlineErrorCode::Forbidden,
				TEXT("The account cannot query this save owner."));
		}
		FSeinOnlineQueryCampaignSavesResult Result;
		Result.Owner = Value.Owner;
		for (const TPair<FString, FSeinOnlineCampaignSaveRecord>& Pair : Backend->Saves)
		{
			if (Pair.Value.Owner.Kind != Value.Owner.Kind
				|| Pair.Value.Owner.OwnerID != Value.Owner.OwnerID)
			{
				continue;
			}
			FSeinOnlineCampaignSaveSummary& Summary =
				Result.Saves.AddDefaulted_GetRef();
			Summary.Slot = Pair.Value.Slot;
			Summary.Revision = Pair.Value.Revision;
			Summary.Bytes = Pair.Value.Data.Num();
		}
		Result.Saves.Sort([](const auto& A, const auto& B)
		{
			return A.Slot < B.Slot;
		});
		return Success(Request, MoveTemp(Result));
	}
	case ESeinOnlineOperation::SubmitTelemetry:
	{
		const auto& Value = Request.Payload.Get<FSeinOnlineSubmitTelemetryRequest>();
		if (Value.AccountID.IsValid()
			&& !IsAuthenticated(AuthenticatedAccounts, Value.AccountID))
		{
			return Failure(Request, ESeinOnlineErrorCode::NotAuthenticated,
				TEXT("The telemetry account is not authenticated."));
		}
		const FString Fingerprint = CanonicalDurablePayload(Request);
		const FString IdempotencyScope = Value.AccountID.IsValid()
			? Value.AccountID.ToCanonicalString()
			: TEXT("anonymous");
		FSeinOnlineProviderResponse Replayed;
		if (ReplayIdempotentResult(*Backend, Request, IdempotencyScope,
			Value.IdempotencyKey, Fingerprint, Replayed))
		{
			return Replayed;
		}
		for (const FSeinOnlineTelemetryEvent& Event : Value.Events)
		{
			for (const FSeinOnlineAttribute& Attribute : Event.Attributes)
			{
				if (IsSensitiveTelemetryKey(Attribute.Key))
				{
					return Failure(Request, ESeinOnlineErrorCode::InvalidRequest,
						TEXT("Telemetry attributes may not contain secret fields."));
				}
			}
		}
		Backend->AcceptedTelemetryEvents += Value.Events.Num();
		FSeinOnlineMutationReceipt Receipt;
		Receipt.ReceiptID = Backend->NewID(TEXT("telemetry"));
		FSeinOnlineProviderResponse Response = Success(Request, Receipt);
		StoreIdempotentResult(*Backend, Request, IdempotencyScope,
			Value.IdempotencyKey, Fingerprint, Response.Payload);
		return Response;
	}
	default:
		return Failure(Request, ESeinOnlineErrorCode::Unsupported,
			TEXT("The online operation is unsupported."));
	}
}

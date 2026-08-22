/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinOnlineServicesContract.cpp
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Validates every provider-neutral SOS request and response schema.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Contract/SeinOnlineServicesContract.h"

namespace
{
	using namespace SeinOnlineServicesContract;

	bool Fail(FSeinOnlineError& OutError, const TCHAR* Message)
	{
		OutError = FSeinOnlineError::Make(
			ESeinOnlineErrorCode::InvalidRequest, Message);
		return false;
	}

	bool IsBounded(const FString& Value, int32 MaxLength, bool bAllowEmpty = false)
	{
		return (bAllowEmpty || !Value.IsEmpty()) && Value.Len() <= MaxLength;
	}

	bool IsValidID(const FSeinOnlineOpaqueID& ID)
	{
		return ID.IsValid()
			&& ID.Provider.ToString().Len() <= MaxIdentifierLength
			&& ID.Value.Len() <= MaxIdentifierLength;
	}

	bool IsValidOptionalID(const FSeinOnlineOpaqueID& ID)
	{
		return !ID.IsValid() || IsValidID(ID);
	}

	bool ValidateAttributes(const TArray<FSeinOnlineAttribute>& Attributes)
	{
		if (Attributes.Num() > MaxAttributes)
		{
			return false;
		}
		TSet<FName> Keys;
		for (const FSeinOnlineAttribute& Attribute : Attributes)
		{
			if (Attribute.Key.IsNone()
				|| Attribute.Key.ToString().Len() > MaxIdentifierLength
				|| !IsBounded(Attribute.Value, MaxTextLength, true)
				|| Keys.Contains(Attribute.Key))
			{
				return false;
			}
			Keys.Add(Attribute.Key);
		}
		return true;
	}

	bool ValidateIdempotencyKey(const FString& Key)
	{
		return IsBounded(Key, MaxIdempotencyKeyLength);
	}

	bool IsValidErrorCode(ESeinOnlineErrorCode Code)
	{
		return Code == ESeinOnlineErrorCode::None
			|| Code == ESeinOnlineErrorCode::InvalidRequest
			|| Code == ESeinOnlineErrorCode::NotAuthenticated
			|| Code == ESeinOnlineErrorCode::NotFound
			|| Code == ESeinOnlineErrorCode::Conflict
			|| Code == ESeinOnlineErrorCode::Forbidden
			|| Code == ESeinOnlineErrorCode::Unsupported
			|| Code == ESeinOnlineErrorCode::CapacityExceeded
			|| Code == ESeinOnlineErrorCode::Cancelled
			|| Code == ESeinOnlineErrorCode::Expired
			|| Code == ESeinOnlineErrorCode::ProviderUnavailable
			|| Code == ESeinOnlineErrorCode::Internal;
	}

	bool ValidateOwner(const FSeinOnlineSaveOwner& Owner)
	{
		return (Owner.Kind == ESeinOnlineSaveOwnerKind::Account
				|| Owner.Kind == ESeinOnlineSaveOwnerKind::Party)
			&& IsValidID(Owner.OwnerID);
	}

	bool ValidateRoster(const TArray<FSeinOnlineMatchRosterEntry>& Roster)
	{
		if (Roster.IsEmpty() || Roster.Num() > MaxCollectionEntries)
		{
			return false;
		}
		TSet<FSeinOnlineOpaqueID> Accounts;
		TSet<FSeinNetworkParticipantID> Participants;
		TSet<FSeinPlayerID> Players;
		for (const FSeinOnlineMatchRosterEntry& Entry : Roster)
		{
			if (!IsValidID(Entry.AccountID)
				|| !Entry.ParticipantID.IsValid()
				|| !Entry.PlayerID.IsValid()
				|| Accounts.Contains(Entry.AccountID)
				|| Participants.Contains(Entry.ParticipantID)
				|| Players.Contains(Entry.PlayerID))
			{
				return false;
			}
			Accounts.Add(Entry.AccountID);
			Participants.Add(Entry.ParticipantID);
			Players.Add(Entry.PlayerID);
		}
		return true;
	}

	bool ValidatePartyRecord(const FSeinOnlinePartyRecord& Party)
	{
		if (!IsValidID(Party.PartyID) || !IsValidID(Party.LeaderAccountID)
			|| Party.Members.IsEmpty()
			|| Party.Members.Num() > MaxCollectionEntries
			|| Party.MaxMembers < Party.Members.Num()
			|| Party.MaxMembers > MaxCollectionEntries
			|| Party.Revision < 1)
		{
			return false;
		}
		TSet<FSeinOnlineOpaqueID> Members;
		int32 LeaderFlags = 0;
		bool bLeaderPresent = false;
		for (const FSeinOnlinePartyMember& Member : Party.Members)
		{
			if (!IsValidID(Member.AccountID)
				|| Members.Contains(Member.AccountID))
			{
				return false;
			}
			Members.Add(Member.AccountID);
			bLeaderPresent |= Member.AccountID == Party.LeaderAccountID;
			LeaderFlags += Member.bLeader ? 1 : 0;
			if (Member.bLeader && Member.AccountID != Party.LeaderAccountID)
			{
				return false;
			}
		}
		return bLeaderPresent && LeaderFlags == 1;
	}

	bool ValidateReplayRecord(const FSeinOnlineReplayEvidenceRecord& Replay)
	{
		return IsValidID(Replay.ReplayID) && Replay.MatchID.IsValid()
			&& Replay.FinalWorldRoot.IsValid() && Replay.TerminalTick >= 0
			&& Replay.ReplayFinalDigest.IsValid()
			&& !Replay.Evidence.IsEmpty()
			&& Replay.Evidence.Num() <= MaxBlobBytes;
	}

	bool ValidateSaveRecord(const FSeinOnlineCampaignSaveRecord& Save)
	{
		return ValidateOwner(Save.Owner)
			&& IsBounded(Save.Slot, MaxIdentifierLength)
			&& Save.Revision >= 1 && Save.Data.Num() <= MaxBlobBytes;
	}

	bool ValidateResultPayload(
		const FSeinOnlineProviderRequest& Request,
		const FInstancedStruct& Payload)
	{
		switch (Request.Operation)
		{
		case ESeinOnlineOperation::Authenticate:
		{
			const auto& Value = Payload.Get<FSeinOnlineAuthenticateResult>().Account;
			return IsValidID(Value.AccountID)
				&& IsBounded(Value.DisplayName, MaxDisplayNameLength, true)
				&& Value.LocalUserIndex
					== Request.Payload.Get<FSeinOnlineAuthenticateRequest>().LocalUserIndex;
		}
		case ESeinOnlineOperation::SignOut:
		case ESeinOnlineOperation::LeaveParty:
		case ESeinOnlineOperation::CancelMatchmaking:
		case ESeinOnlineOperation::SubmitMatchResult:
		case ESeinOnlineOperation::WriteStats:
		case ESeinOnlineOperation::SubmitTelemetry:
			return IsValidID(
				Payload.Get<FSeinOnlineMutationReceipt>().ReceiptID);
		case ESeinOnlineOperation::CreateParty:
		case ESeinOnlineOperation::JoinParty:
		case ESeinOnlineOperation::QueryParty:
		{
			const FSeinOnlinePartyRecord& Party =
				Payload.Get<FSeinOnlinePartyResult>().Party;
			if (!ValidatePartyRecord(Party))
			{
				return false;
			}
			if (Request.Operation == ESeinOnlineOperation::CreateParty)
			{
				const auto& Input =
					Request.Payload.Get<FSeinOnlineCreatePartyRequest>();
				return Party.LeaderAccountID
					== Input.LeaderAccountID
					&& Party.MaxMembers == Input.MaxMembers;
			}
			if (Request.Operation == ESeinOnlineOperation::JoinParty)
			{
				const FSeinOnlineOpaqueID& AccountID =
					Request.Payload.Get<FSeinOnlineJoinPartyRequest>().AccountID;
				return Party.Members.ContainsByPredicate(
					[&](const auto& Member)
					{
						return Member.AccountID == AccountID;
					});
			}
			return Party.PartyID
				== Request.Payload.Get<FSeinOnlineQueryPartyRequest>().PartyID;
		}
		case ESeinOnlineOperation::InviteToParty:
		{
			const auto& Result = Payload.Get<FSeinOnlinePartyInviteResult>();
			const auto& Input = Request.Payload.Get<FSeinOnlineInviteToPartyRequest>();
			return IsValidID(Result.InviteID) && IsValidID(Result.PartyID)
				&& IsValidID(Result.InviteeAccountID)
				&& Result.PartyID == Input.PartyID
				&& Result.InviteeAccountID == Input.InviteeAccountID;
		}
		case ESeinOnlineOperation::StartMatchmaking:
		case ESeinOnlineOperation::QueryMatchmaking:
		{
			const FSeinOnlineMatchmakingTicket& Ticket =
				Payload.Get<FSeinOnlineMatchmakingResult>().Ticket;
			if (!IsValidID(Ticket.TicketID) || Ticket.Queue.IsNone()
				|| Ticket.Queue.ToString().Len() > MaxIdentifierLength
				|| Ticket.Region.ToString().Len() > MaxIdentifierLength
				|| (Ticket.State != ESeinOnlineMatchmakingState::Searching
					&& Ticket.State != ESeinOnlineMatchmakingState::Matched
					&& Ticket.State != ESeinOnlineMatchmakingState::Cancelled
					&& Ticket.State != ESeinOnlineMatchmakingState::Failed))
			{
				return false;
			}
			if (Request.Operation == ESeinOnlineOperation::StartMatchmaking)
			{
				const auto& Input =
					Request.Payload.Get<FSeinOnlineStartMatchmakingRequest>();
				return Ticket.Queue == Input.Queue && Ticket.Region == Input.Region;
			}
			return Ticket.TicketID
				== Request.Payload.Get<FSeinOnlineTicketRequest>().TicketID;
		}
		case ESeinOnlineOperation::AllocateServer:
		{
			const auto& Value = Payload.Get<FSeinOnlineServerAllocationResult>();
			return IsValidID(Value.AllocationID)
				&& IsBounded(Value.Host, MaxTextLength)
				&& Value.Port >= 1 && Value.Port <= 65535
				&& IsBounded(Value.LeaseSecret, MaxTextLength);
		}
		case ESeinOnlineOperation::RegisterMatch:
		{
			const FSeinOnlineMatchRecord& Result =
				Payload.Get<FSeinOnlineRegisterMatchResult>().Match;
			const auto& Input = Request.Payload.Get<FSeinOnlineRegisterMatchRequest>();
			if (!Result.MatchID.IsValid() || !IsValidID(Result.AllocationID)
				|| (Result.Classification != ESeinOnlineMatchClassification::Unranked
					&& Result.Classification != ESeinOnlineMatchClassification::Ranked)
				|| !ValidateRoster(Result.Roster)
				|| Result.MatchID != Input.MatchID
				|| Result.AllocationID != Input.AllocationID
				|| Result.Classification != Input.Classification
				|| Result.Roster.Num() != Input.Roster.Num())
			{
				return false;
			}
			for (const FSeinOnlineMatchRosterEntry& Entry : Input.Roster)
			{
				if (!Result.Roster.ContainsByPredicate([&](const auto& Candidate)
					{
						return Candidate.AccountID == Entry.AccountID
							&& Candidate.ParticipantID == Entry.ParticipantID
							&& Candidate.PlayerID == Entry.PlayerID;
					}))
				{
					return false;
				}
			}
			return true;
		}
		case ESeinOnlineOperation::IssueReconnectCredential:
		{
			const auto& Value = Payload.Get<FSeinOnlineReconnectCredentialResult>();
			return IsBounded(Value.AdmissionID, MaxIdentifierLength)
				&& IsBounded(Value.Credential, MaxTextLength)
				&& Value.ExpiresAtUnixMilliseconds > 0;
		}
		case ESeinOnlineOperation::ValidateReconnectCredential:
			return IsValidID(Payload.Get<
				FSeinOnlineValidateReconnectCredentialResult>().AccountID);
		case ESeinOnlineOperation::QueryStats:
		{
			const auto& Value = Payload.Get<FSeinOnlineQueryStatsResult>();
			if (!IsValidID(Value.AccountID)
				|| Value.AccountID
					!= Request.Payload.Get<FSeinOnlineQueryStatsRequest>().AccountID
				|| Value.Stats.Num() > MaxAttributes)
			{
				return false;
			}
			FName Previous;
			const TArray<FName>& Requested =
				Request.Payload.Get<FSeinOnlineQueryStatsRequest>().Stats;
			for (const FSeinOnlineStatValue& Stat : Value.Stats)
			{
				if (Stat.Stat.IsNone()
					|| Stat.Stat.ToString().Len() > MaxIdentifierLength
					|| (!Requested.IsEmpty() && !Requested.Contains(Stat.Stat))
					|| (!Previous.IsNone() && !Previous.LexicalLess(Stat.Stat)))
				{
					return false;
				}
				Previous = Stat.Stat;
			}
			return true;
		}
		case ESeinOnlineOperation::QueryLeaderboard:
		{
			const auto& Value = Payload.Get<FSeinOnlineQueryLeaderboardResult>();
			const auto& Input = Request.Payload.Get<FSeinOnlineQueryLeaderboardRequest>();
			if (Value.Stat != Input.Stat || Value.Entries.Num() > Input.Limit)
			{
				return false;
			}
			TSet<FSeinOnlineOpaqueID> Accounts;
			for (int32 Index = 0; Index < Value.Entries.Num(); ++Index)
			{
				const auto& Entry = Value.Entries[Index];
				if (!IsValidID(Entry.AccountID)
					|| Accounts.Contains(Entry.AccountID)
					|| Entry.Rank != Input.Offset + Index + 1)
				{
					return false;
				}
				if (Index > 0)
				{
					const auto& Previous = Value.Entries[Index - 1];
					if (Previous.Score < Entry.Score
						|| (Previous.Score == Entry.Score
							&& !(Previous.AccountID.ToCanonicalString()
								< Entry.AccountID.ToCanonicalString())))
					{
						return false;
					}
				}
				Accounts.Add(Entry.AccountID);
			}
			return true;
		}
		case ESeinOnlineOperation::PublishReplayEvidence:
		{
			const FSeinOnlineReplayEvidenceRecord& Replay =
				Payload.Get<FSeinOnlinePublishReplayEvidenceResult>().Replay;
			const auto& Input =
				Request.Payload.Get<FSeinOnlinePublishReplayEvidenceRequest>();
			return ValidateReplayRecord(Replay)
				&& Replay.MatchID == Input.MatchID
				&& Replay.FinalWorldRoot == Input.FinalWorldRoot
				&& Replay.TerminalTick == Input.TerminalTick
				&& Replay.ReplayFinalDigest == Input.ReplayFinalDigest
				&& Replay.Evidence == Input.Evidence;
		}
		case ESeinOnlineOperation::QueryReplayEvidence:
		{
			const auto& Replay = Payload.Get<FSeinOnlineReplayEvidenceRecord>();
			return ValidateReplayRecord(Replay)
				&& Replay.ReplayID
					== Request.Payload.Get<
						FSeinOnlineQueryReplayEvidenceRequest>().ReplayID;
		}
		case ESeinOnlineOperation::WriteCampaignSave:
		case ESeinOnlineOperation::ReadCampaignSave:
		{
			const FSeinOnlineCampaignSaveRecord& Save =
				Payload.Get<FSeinOnlineCampaignSaveResult>().Save;
			if (!ValidateSaveRecord(Save))
			{
				return false;
			}
			if (Request.Operation == ESeinOnlineOperation::WriteCampaignSave)
			{
				const auto& Input =
					Request.Payload.Get<FSeinOnlineWriteCampaignSaveRequest>();
				const bool bRevisionMatches =
					(Input.Mode == ESeinOnlineSaveWriteMode::CreateOnly
						&& Save.Revision == 1)
					|| (Input.Mode == ESeinOnlineSaveWriteMode::IfRevision
						&& Save.Revision == Input.ExpectedRevision + 1)
					|| Input.Mode == ESeinOnlineSaveWriteMode::Overwrite;
				return bRevisionMatches
					&& Save.Owner.Kind == Input.Owner.Kind
					&& Save.Owner.OwnerID == Input.Owner.OwnerID
					&& Save.Slot == Input.Slot && Save.Data == Input.Data;
			}
			const auto& Input =
				Request.Payload.Get<FSeinOnlineReadCampaignSaveRequest>();
			return Save.Owner.Kind == Input.Owner.Kind
				&& Save.Owner.OwnerID == Input.Owner.OwnerID
				&& Save.Slot == Input.Slot;
		}
		case ESeinOnlineOperation::QueryCampaignSaves:
		{
			const auto& Value = Payload.Get<FSeinOnlineQueryCampaignSavesResult>();
			const auto& Input =
				Request.Payload.Get<FSeinOnlineQueryCampaignSavesRequest>();
			if (!ValidateOwner(Value.Owner)
				|| Value.Owner.Kind != Input.Owner.Kind
				|| Value.Owner.OwnerID != Input.Owner.OwnerID
				|| Value.Saves.Num() > MaxCollectionEntries)
			{
				return false;
			}
			FString Previous;
			for (const FSeinOnlineCampaignSaveSummary& Save : Value.Saves)
			{
				if (!IsBounded(Save.Slot, MaxIdentifierLength)
					|| Save.Revision < 1 || Save.Bytes < 0
					|| Save.Bytes > MaxBlobBytes
					|| (!Previous.IsEmpty() && !(Previous < Save.Slot)))
				{
					return false;
				}
				Previous = Save.Slot;
			}
			return true;
		}
		default:
			return false;
		}
	}

	bool ValidateRequestPayload(
		ESeinOnlineOperation Operation,
		const FInstancedStruct& Payload)
	{
		switch (Operation)
		{
		case ESeinOnlineOperation::Authenticate:
		{
			const auto& Value = Payload.Get<FSeinOnlineAuthenticateRequest>();
			return Value.LocalUserIndex >= 0
				&& !Value.CredentialType.IsNone()
				&& Value.CredentialType.ToString().Len() <= MaxIdentifierLength
				&& IsBounded(Value.Credential, MaxTextLength, true)
				&& IsBounded(Value.DisplayNameHint, MaxDisplayNameLength, true);
		}
		case ESeinOnlineOperation::SignOut:
			return IsValidID(Payload.Get<FSeinOnlineSignOutRequest>().AccountID);
		case ESeinOnlineOperation::CreateParty:
		{
			const auto& Value = Payload.Get<FSeinOnlineCreatePartyRequest>();
			return IsValidID(Value.LeaderAccountID)
				&& Value.MaxMembers >= 1
				&& Value.MaxMembers <= MaxCollectionEntries
				&& ValidateIdempotencyKey(Value.IdempotencyKey);
		}
		case ESeinOnlineOperation::InviteToParty:
		{
			const auto& Value = Payload.Get<FSeinOnlineInviteToPartyRequest>();
			return IsValidID(Value.PartyID)
				&& IsValidID(Value.InviterAccountID)
				&& IsValidID(Value.InviteeAccountID);
		}
		case ESeinOnlineOperation::JoinParty:
		{
			const auto& Value = Payload.Get<FSeinOnlineJoinPartyRequest>();
			return IsValidID(Value.InviteID) && IsValidID(Value.AccountID);
		}
		case ESeinOnlineOperation::LeaveParty:
		{
			const auto& Value = Payload.Get<FSeinOnlineLeavePartyRequest>();
			return IsValidID(Value.PartyID) && IsValidID(Value.AccountID);
		}
		case ESeinOnlineOperation::QueryParty:
			return IsValidID(Payload.Get<FSeinOnlineQueryPartyRequest>().PartyID);
		case ESeinOnlineOperation::StartMatchmaking:
		{
			const auto& Value = Payload.Get<FSeinOnlineStartMatchmakingRequest>();
			return IsValidID(Value.AccountID)
				&& IsValidOptionalID(Value.PartyID)
				&& !Value.Queue.IsNone()
				&& Value.Queue.ToString().Len() <= MaxIdentifierLength
				&& Value.Region.ToString().Len() <= MaxIdentifierLength
				&& ValidateAttributes(Value.Attributes)
				&& ValidateIdempotencyKey(Value.IdempotencyKey);
		}
		case ESeinOnlineOperation::QueryMatchmaking:
		case ESeinOnlineOperation::CancelMatchmaking:
			return IsValidID(Payload.Get<FSeinOnlineTicketRequest>().TicketID);
		case ESeinOnlineOperation::AllocateServer:
		{
			const auto& Value = Payload.Get<FSeinOnlineAllocateServerRequest>();
			return IsValidID(Value.TicketID)
				&& IsBounded(Value.BuildID, MaxIdentifierLength)
				&& ValidateIdempotencyKey(Value.IdempotencyKey);
		}
		case ESeinOnlineOperation::RegisterMatch:
		{
			const auto& Value = Payload.Get<FSeinOnlineRegisterMatchRequest>();
			if (!Value.MatchID.IsValid() || !IsValidID(Value.AllocationID)
				|| (Value.Classification
						!= ESeinOnlineMatchClassification::Unranked
					&& Value.Classification
							!= ESeinOnlineMatchClassification::Ranked)
				|| !ValidateRoster(Value.Roster)
				|| !ValidateIdempotencyKey(Value.IdempotencyKey))
			{
				return false;
			}
			return true;
		}
		case ESeinOnlineOperation::IssueReconnectCredential:
		{
			const auto& Value =
				Payload.Get<FSeinOnlineIssueReconnectCredentialRequest>();
			return Value.MatchID.IsValid()
				&& Value.ParticipantID.IsValid()
				&& Value.LifetimeSeconds >= 1
				&& Value.LifetimeSeconds <= 86400
				&& IsBounded(
					Value.PlatformIdentityType, MaxIdentifierLength, true)
				&& IsBounded(
					Value.PlatformIdentityValue, MaxTextLength, true)
				&& Value.PlatformIdentityType.IsEmpty()
					== Value.PlatformIdentityValue.IsEmpty()
				&& ValidateIdempotencyKey(Value.IdempotencyKey);
		}
		case ESeinOnlineOperation::ValidateReconnectCredential:
		{
			const auto& Value =
				Payload.Get<FSeinOnlineValidateReconnectCredentialRequest>();
			return Value.MatchID.IsValid()
				&& Value.ParticipantID.IsValid()
				&& IsBounded(Value.Credential, MaxTextLength);
		}
		case ESeinOnlineOperation::SubmitMatchResult:
		{
			const auto& Value = Payload.Get<FSeinOnlineSubmitMatchResultRequest>();
			if (!Value.MatchID.IsValid() || Value.Placements.IsEmpty()
				|| Value.Placements.Num() > MaxCollectionEntries
				|| Value.TerminalTick < 0
				|| !Value.FinalWorldRoot.IsValid()
				|| !Value.ReplayFinalDigest.IsValid()
				|| !IsValidID(Value.ReplayEvidenceID)
				|| !ValidateIdempotencyKey(Value.IdempotencyKey))
			{
				return false;
			}
			TSet<FSeinOnlineOpaqueID> Accounts;
			TSet<int32> Placements;
			for (const FSeinOnlineMatchPlacement& Entry : Value.Placements)
			{
				if (!IsValidID(Entry.AccountID) || Entry.Placement < 1
					|| Entry.Placement > Value.Placements.Num()
					|| Accounts.Contains(Entry.AccountID)
					|| Placements.Contains(Entry.Placement))
				{
					return false;
				}
				Accounts.Add(Entry.AccountID);
				Placements.Add(Entry.Placement);
			}
			return true;
		}
		case ESeinOnlineOperation::WriteStats:
		{
			const auto& Value = Payload.Get<FSeinOnlineWriteStatsRequest>();
			if (!IsValidID(Value.AccountID) || !Value.MatchID.IsValid()
				|| Value.Mutations.IsEmpty()
				|| Value.Mutations.Num() > MaxAttributes
				|| !ValidateIdempotencyKey(Value.IdempotencyKey))
			{
				return false;
			}
			TSet<FName> Stats;
			for (const FSeinOnlineStatMutation& Mutation : Value.Mutations)
			{
				if (Mutation.Stat.IsNone()
					|| Mutation.Stat.ToString().Len() > MaxIdentifierLength
					|| (Mutation.Kind != ESeinOnlineStatMutationKind::Set
						&& Mutation.Kind != ESeinOnlineStatMutationKind::Add
						&& Mutation.Kind != ESeinOnlineStatMutationKind::Max
						&& Mutation.Kind != ESeinOnlineStatMutationKind::Min)
					|| Stats.Contains(Mutation.Stat))
				{
					return false;
				}
				Stats.Add(Mutation.Stat);
			}
			return true;
		}
		case ESeinOnlineOperation::QueryStats:
		{
			const auto& Value = Payload.Get<FSeinOnlineQueryStatsRequest>();
			if (!IsValidID(Value.AccountID) || Value.Stats.Num() > MaxAttributes)
			{
				return false;
			}
			TSet<FName> Stats;
			for (FName Stat : Value.Stats)
			{
				if (Stat.IsNone() || Stat.ToString().Len() > MaxIdentifierLength
					|| Stats.Contains(Stat))
				{
					return false;
				}
				Stats.Add(Stat);
			}
			return true;
		}
		case ESeinOnlineOperation::QueryLeaderboard:
		{
			const auto& Value = Payload.Get<FSeinOnlineQueryLeaderboardRequest>();
			return !Value.Stat.IsNone()
				&& Value.Stat.ToString().Len() <= MaxIdentifierLength
				&& Value.Offset >= 0
				&& Value.Limit >= 1
				&& Value.Limit <= MaxCollectionEntries;
		}
		case ESeinOnlineOperation::PublishReplayEvidence:
		{
			const auto& Value =
				Payload.Get<FSeinOnlinePublishReplayEvidenceRequest>();
			return Value.MatchID.IsValid()
				&& Value.FinalWorldRoot.IsValid()
				&& Value.TerminalTick >= 0
				&& Value.ReplayFinalDigest.IsValid()
				&& !Value.Evidence.IsEmpty()
				&& Value.Evidence.Num() <= MaxBlobBytes
				&& ValidateIdempotencyKey(Value.IdempotencyKey);
		}
		case ESeinOnlineOperation::QueryReplayEvidence:
			return IsValidID(
				Payload.Get<FSeinOnlineQueryReplayEvidenceRequest>().ReplayID);
		case ESeinOnlineOperation::WriteCampaignSave:
		{
			const auto& Value = Payload.Get<FSeinOnlineWriteCampaignSaveRequest>();
			const bool bRevisionValid =
				(Value.Mode == ESeinOnlineSaveWriteMode::IfRevision
					&& Value.ExpectedRevision >= 1)
				|| ((Value.Mode == ESeinOnlineSaveWriteMode::CreateOnly
						|| Value.Mode == ESeinOnlineSaveWriteMode::Overwrite)
					&& Value.ExpectedRevision == 0);
			return IsValidID(Value.RequestingAccountID)
				&& ValidateOwner(Value.Owner)
				&& IsBounded(Value.Slot, MaxIdentifierLength)
				&& bRevisionValid
				&& Value.Data.Num() <= MaxBlobBytes
				&& ValidateIdempotencyKey(Value.IdempotencyKey);
		}
		case ESeinOnlineOperation::ReadCampaignSave:
		{
			const auto& Value = Payload.Get<FSeinOnlineReadCampaignSaveRequest>();
			return IsValidID(Value.RequestingAccountID)
				&& ValidateOwner(Value.Owner)
				&& IsBounded(Value.Slot, MaxIdentifierLength);
		}
		case ESeinOnlineOperation::QueryCampaignSaves:
		{
			const auto& Value =
				Payload.Get<FSeinOnlineQueryCampaignSavesRequest>();
			return IsValidID(Value.RequestingAccountID)
				&& ValidateOwner(Value.Owner);
		}
		case ESeinOnlineOperation::SubmitTelemetry:
		{
			const auto& Value = Payload.Get<FSeinOnlineSubmitTelemetryRequest>();
			if (!IsValidOptionalID(Value.AccountID) || Value.Events.IsEmpty()
				|| Value.Events.Num() > MaxCollectionEntries
				|| !ValidateIdempotencyKey(Value.IdempotencyKey))
			{
				return false;
			}
			for (const FSeinOnlineTelemetryEvent& Event : Value.Events)
			{
				if (Event.Event.IsNone()
					|| Event.Event.ToString().Len() > MaxIdentifierLength
					|| Event.TimestampUnixMilliseconds < 0
					|| !ValidateAttributes(Event.Attributes))
				{
					return false;
				}
			}
			return true;
		}
		default:
			return false;
		}
	}
}

namespace SeinOnlineContract
{
	const UScriptStruct* GetRequestStruct(ESeinOnlineOperation Operation)
	{
		switch (Operation)
		{
		case ESeinOnlineOperation::Authenticate: return FSeinOnlineAuthenticateRequest::StaticStruct();
		case ESeinOnlineOperation::SignOut: return FSeinOnlineSignOutRequest::StaticStruct();
		case ESeinOnlineOperation::CreateParty: return FSeinOnlineCreatePartyRequest::StaticStruct();
		case ESeinOnlineOperation::InviteToParty: return FSeinOnlineInviteToPartyRequest::StaticStruct();
		case ESeinOnlineOperation::JoinParty: return FSeinOnlineJoinPartyRequest::StaticStruct();
		case ESeinOnlineOperation::LeaveParty: return FSeinOnlineLeavePartyRequest::StaticStruct();
		case ESeinOnlineOperation::QueryParty: return FSeinOnlineQueryPartyRequest::StaticStruct();
		case ESeinOnlineOperation::StartMatchmaking: return FSeinOnlineStartMatchmakingRequest::StaticStruct();
		case ESeinOnlineOperation::QueryMatchmaking:
		case ESeinOnlineOperation::CancelMatchmaking: return FSeinOnlineTicketRequest::StaticStruct();
		case ESeinOnlineOperation::AllocateServer: return FSeinOnlineAllocateServerRequest::StaticStruct();
		case ESeinOnlineOperation::RegisterMatch: return FSeinOnlineRegisterMatchRequest::StaticStruct();
		case ESeinOnlineOperation::IssueReconnectCredential: return FSeinOnlineIssueReconnectCredentialRequest::StaticStruct();
		case ESeinOnlineOperation::ValidateReconnectCredential: return FSeinOnlineValidateReconnectCredentialRequest::StaticStruct();
		case ESeinOnlineOperation::SubmitMatchResult: return FSeinOnlineSubmitMatchResultRequest::StaticStruct();
		case ESeinOnlineOperation::WriteStats: return FSeinOnlineWriteStatsRequest::StaticStruct();
		case ESeinOnlineOperation::QueryStats: return FSeinOnlineQueryStatsRequest::StaticStruct();
		case ESeinOnlineOperation::QueryLeaderboard: return FSeinOnlineQueryLeaderboardRequest::StaticStruct();
		case ESeinOnlineOperation::PublishReplayEvidence: return FSeinOnlinePublishReplayEvidenceRequest::StaticStruct();
		case ESeinOnlineOperation::QueryReplayEvidence: return FSeinOnlineQueryReplayEvidenceRequest::StaticStruct();
		case ESeinOnlineOperation::WriteCampaignSave: return FSeinOnlineWriteCampaignSaveRequest::StaticStruct();
		case ESeinOnlineOperation::ReadCampaignSave: return FSeinOnlineReadCampaignSaveRequest::StaticStruct();
		case ESeinOnlineOperation::QueryCampaignSaves: return FSeinOnlineQueryCampaignSavesRequest::StaticStruct();
		case ESeinOnlineOperation::SubmitTelemetry: return FSeinOnlineSubmitTelemetryRequest::StaticStruct();
		default: return nullptr;
		}
	}

	const UScriptStruct* GetResultStruct(ESeinOnlineOperation Operation)
	{
		switch (Operation)
		{
		case ESeinOnlineOperation::Authenticate: return FSeinOnlineAuthenticateResult::StaticStruct();
		case ESeinOnlineOperation::SignOut:
		case ESeinOnlineOperation::LeaveParty:
		case ESeinOnlineOperation::CancelMatchmaking:
		case ESeinOnlineOperation::SubmitMatchResult:
		case ESeinOnlineOperation::WriteStats:
		case ESeinOnlineOperation::SubmitTelemetry: return FSeinOnlineMutationReceipt::StaticStruct();
		case ESeinOnlineOperation::CreateParty:
		case ESeinOnlineOperation::JoinParty:
		case ESeinOnlineOperation::QueryParty: return FSeinOnlinePartyResult::StaticStruct();
		case ESeinOnlineOperation::InviteToParty: return FSeinOnlinePartyInviteResult::StaticStruct();
		case ESeinOnlineOperation::StartMatchmaking:
		case ESeinOnlineOperation::QueryMatchmaking: return FSeinOnlineMatchmakingResult::StaticStruct();
		case ESeinOnlineOperation::AllocateServer: return FSeinOnlineServerAllocationResult::StaticStruct();
		case ESeinOnlineOperation::RegisterMatch: return FSeinOnlineRegisterMatchResult::StaticStruct();
		case ESeinOnlineOperation::IssueReconnectCredential: return FSeinOnlineReconnectCredentialResult::StaticStruct();
		case ESeinOnlineOperation::ValidateReconnectCredential: return FSeinOnlineValidateReconnectCredentialResult::StaticStruct();
		case ESeinOnlineOperation::QueryStats: return FSeinOnlineQueryStatsResult::StaticStruct();
		case ESeinOnlineOperation::QueryLeaderboard: return FSeinOnlineQueryLeaderboardResult::StaticStruct();
		case ESeinOnlineOperation::PublishReplayEvidence: return FSeinOnlinePublishReplayEvidenceResult::StaticStruct();
		case ESeinOnlineOperation::QueryReplayEvidence: return FSeinOnlineReplayEvidenceRecord::StaticStruct();
		case ESeinOnlineOperation::WriteCampaignSave:
		case ESeinOnlineOperation::ReadCampaignSave: return FSeinOnlineCampaignSaveResult::StaticStruct();
		case ESeinOnlineOperation::QueryCampaignSaves: return FSeinOnlineQueryCampaignSavesResult::StaticStruct();
		default: return nullptr;
		}
	}

	FString GetOperationName(ESeinOnlineOperation Operation)
	{
		if (const UEnum* Enum = StaticEnum<ESeinOnlineOperation>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(Operation));
		}
		return TEXT("Unknown");
	}

	bool ValidateRequest(
		const FSeinOnlineProviderRequest& Request,
		FSeinOnlineError& OutError)
	{
		OutError = FSeinOnlineError();
		if (Request.ContractRevision != SeinOnlineServicesContract::CurrentRevision)
		{
			return Fail(OutError, TEXT("Unsupported online-services contract revision."));
		}
		if (!Request.Handle.IsValid())
		{
			return Fail(OutError, TEXT("The online request handle is invalid."));
		}
		if (Request.Authority != ESeinOnlineCallerAuthority::Client
			&& Request.Authority != ESeinOnlineCallerAuthority::TrustedServer)
		{
			return Fail(OutError, TEXT("The online caller authority is invalid."));
		}
		const UScriptStruct* Expected = GetRequestStruct(Request.Operation);
		if (!Expected || Request.Payload.GetScriptStruct() != Expected)
		{
			return Fail(OutError, TEXT("The online request payload schema is invalid."));
		}
		if (!ValidateRequestPayload(Request.Operation, Request.Payload))
		{
			return Fail(OutError, TEXT("The online request contains invalid or oversized values."));
		}
		return true;
	}

	bool ValidateResponse(
		const FSeinOnlineProviderRequest& Request,
		const FSeinOnlineProviderResponse& Response,
		FSeinOnlineError& OutError)
	{
		OutError = FSeinOnlineError();
		if (Response.Handle != Request.Handle || Response.Operation != Request.Operation)
		{
			return Fail(OutError, TEXT("The provider response identity does not match its request."));
		}
		if (!IsValidErrorCode(Response.Error.Code)
			|| Response.Error.Message.Len()
				> SeinOnlineServicesContract::MaxTextLength)
		{
			return Fail(OutError, TEXT("The provider response error is oversized."));
		}
		if (!Response.Error.IsSuccess())
		{
			if (Response.Payload.IsValid())
			{
				return Fail(OutError, TEXT("A failed provider response carried an unexpected payload."));
			}
			return true;
		}
		if (!Response.Error.Message.IsEmpty() || Response.Error.bRetryable)
		{
			return Fail(OutError, TEXT("A successful provider response carried error metadata."));
		}
		const UScriptStruct* Expected = GetResultStruct(Response.Operation);
		if (!Expected || Response.Payload.GetScriptStruct() != Expected)
		{
			return Fail(OutError, TEXT("The provider response payload schema is invalid."));
		}
		if (!ValidateResultPayload(Request, Response.Payload))
		{
			return Fail(OutError, TEXT("The provider response contains invalid or oversized values."));
		}
		return true;
	}

	bool IsDurableMutation(ESeinOnlineOperation Operation)
	{
		return Operation == ESeinOnlineOperation::CreateParty
			|| Operation == ESeinOnlineOperation::StartMatchmaking
			|| Operation == ESeinOnlineOperation::AllocateServer
			|| Operation == ESeinOnlineOperation::RegisterMatch
			|| Operation == ESeinOnlineOperation::IssueReconnectCredential
			|| Operation == ESeinOnlineOperation::SubmitMatchResult
			|| Operation == ESeinOnlineOperation::WriteStats
			|| Operation == ESeinOnlineOperation::PublishReplayEvidence
			|| Operation == ESeinOnlineOperation::WriteCampaignSave
			|| Operation == ESeinOnlineOperation::SubmitTelemetry;
	}
}

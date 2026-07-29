/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file SeinReplayFormat.h
 * @brief Fixed replay-file prefix and bounded v8 journal rules shared by writer, reader, and tests.
 */

#pragma once

#include "CoreMinimal.h"
#include "Data/SeinReplayHeader.h"
#include "Data/SeinReplayTurn.h"
#include "Input/SeinCommandSchemaRegistry.h"
#include "SeinNetProtocolTypes.h"
#include "Hash/Blake3.h"

namespace SeinReplayFormat
{
	constexpr uint32 FileFormatVersion = 8;
	constexpr uint64 MaxBodyBytes = 64ULL * 1024ULL * 1024ULL;
	constexpr int32 MaxCommandsPerTurn =
		SeinNetProtocolLimits::MaxCommandsPerCanonicalTurn;
	constexpr int32 PrefixBytes = 136;
	constexpr uint8 Magic[8] = {'S', 'E', 'I', 'N', 'R', 'P', 'L', '8'};

	struct FPrefix
	{
		FGuid CommandProtocolDigest;
		FGuid MatchSettingsDigest;
		FSeinMatchBootstrapReceipt BootstrapReceipt;
		int32 ConfigFingerprint = 0;
		uint64 BodyBytes = 0;
		FGuid BodyDigest;
	};

	/** Bind replay self-authorization to the exact immutable playback envelope. */
	SEINARTSNET_API bool ComputeBootstrapAuthorizationContextDigest(
		const FSeinReplayHeader& Header,
		FGuid& OutDigest,
		FString& OutError);

	inline void AppendUInt32(TArray<uint8>& Out, uint32 Value)
	{
		Out.Add(static_cast<uint8>(Value >> 24));
		Out.Add(static_cast<uint8>(Value >> 16));
		Out.Add(static_cast<uint8>(Value >> 8));
		Out.Add(static_cast<uint8>(Value));
	}

	inline void AppendUInt64(TArray<uint8>& Out, uint64 Value)
	{
		AppendUInt32(Out, static_cast<uint32>(Value >> 32));
		AppendUInt32(Out, static_cast<uint32>(Value));
	}

	inline void AppendGuid(TArray<uint8>& Out, const FGuid& Value)
	{
		AppendUInt32(Out, Value.A);
		AppendUInt32(Out, Value.B);
		AppendUInt32(Out, Value.C);
		AppendUInt32(Out, Value.D);
	}

	inline uint32 ReadUInt32(const uint8* Bytes)
	{
		return (static_cast<uint32>(Bytes[0]) << 24)
			| (static_cast<uint32>(Bytes[1]) << 16)
			| (static_cast<uint32>(Bytes[2]) << 8)
			| static_cast<uint32>(Bytes[3]);
	}

	inline uint64 ReadUInt64(const uint8* Bytes)
	{
		return (static_cast<uint64>(ReadUInt32(Bytes)) << 32)
			| static_cast<uint64>(ReadUInt32(Bytes + 4));
	}

	inline FGuid ReadGuid(const uint8* Bytes)
	{
		return FGuid(
			ReadUInt32(Bytes),
			ReadUInt32(Bytes + 4),
			ReadUInt32(Bytes + 8),
			ReadUInt32(Bytes + 12));
	}

	inline FGuid ComputeBodyDigest(const uint8* Bytes, int32 NumBytes)
	{
		const FBlake3Hash Hash = FBlake3::HashBuffer(Bytes, NumBytes);
		const uint8* HashBytes = Hash.GetBytes();
		FGuid Result = ReadGuid(HashBytes);
		if (!Result.IsValid()) Result.D = 1;
		return Result;
	}

	inline bool BuildPrefix(
		const FGuid& CommandProtocolDigest,
		const FGuid& MatchSettingsDigest,
		const FSeinMatchBootstrapReceipt& BootstrapReceipt,
		int32 ConfigFingerprint,
		const TArray<uint8>& Body,
		TArray<uint8>& OutPrefix,
		FString& OutError)
	{
		OutPrefix.Reset();
		OutError.Reset();
		if (!CommandProtocolDigest.IsValid() || !MatchSettingsDigest.IsValid()
			|| !BootstrapReceipt.IsValid()
			|| BootstrapReceipt.ContractDigest != MatchSettingsDigest)
		{
			OutError = TEXT("required compatibility/bootstrap receipt is invalid or inconsistent");
			return false;
		}
		if (Body.IsEmpty() || static_cast<uint64>(Body.Num()) > MaxBodyBytes)
		{
			OutError = FString::Printf(
				TEXT("bounded body size %d is outside the supported range 1..%llu"),
				Body.Num(), static_cast<unsigned long long>(MaxBodyBytes));
			return false;
		}

		OutPrefix.Reserve(PrefixBytes);
		OutPrefix.Append(Magic, UE_ARRAY_COUNT(Magic));
		AppendUInt32(OutPrefix, FileFormatVersion);
		AppendGuid(OutPrefix, CommandProtocolDigest);
		AppendGuid(OutPrefix, MatchSettingsDigest);
		AppendGuid(
			OutPrefix,
			BootstrapReceipt.SimulationContentDigest);
		AppendGuid(OutPrefix, BootstrapReceipt.StateContractDigest);
		AppendGuid(OutPrefix, BootstrapReceipt.PlanDigest);
		AppendGuid(OutPrefix, BootstrapReceipt.InitialStateDigest);
		AppendUInt32(OutPrefix, static_cast<uint32>(ConfigFingerprint));
		AppendUInt64(OutPrefix, static_cast<uint64>(Body.Num()));
		AppendGuid(OutPrefix, ComputeBodyDigest(Body.GetData(), Body.Num()));
		check(OutPrefix.Num() == PrefixBytes);
		return true;
	}

	inline bool ParsePrefix(
		const TArray<uint8>& FileBytes,
		FPrefix& OutPrefix,
		FString& OutError)
	{
		OutPrefix = FPrefix();
		OutError.Reset();
		if (FileBytes.Num() < PrefixBytes)
		{
			OutError = TEXT("file is smaller than the v8 prefix");
			return false;
		}
		if (FMemory::Memcmp(FileBytes.GetData(), Magic, UE_ARRAY_COUNT(Magic)) != 0)
		{
			OutError = TEXT("magic mismatch (legacy or non-SeinARTS replay)");
			return false;
		}

		const uint8* Cursor = FileBytes.GetData() + UE_ARRAY_COUNT(Magic);
		const uint32 Version = ReadUInt32(Cursor);
		Cursor += 4;
		if (Version != FileFormatVersion)
		{
			OutError = FString::Printf(
				TEXT("unsupported file-format version %u (expected %u)"),
				Version, FileFormatVersion);
			return false;
		}

		OutPrefix.CommandProtocolDigest = ReadGuid(Cursor);
		Cursor += 16;
		OutPrefix.MatchSettingsDigest = ReadGuid(Cursor);
		Cursor += 16;
		OutPrefix.BootstrapReceipt.FormatVersion =
			FSeinMatchBootstrapReceipt::CurrentFormatVersion;
		OutPrefix.BootstrapReceipt.ContractDigest =
			OutPrefix.MatchSettingsDigest;
		OutPrefix.BootstrapReceipt.SimulationContentDigest =
			ReadGuid(Cursor);
		Cursor += 16;
		OutPrefix.BootstrapReceipt.StateContractDigest =
			ReadGuid(Cursor);
		Cursor += 16;
		OutPrefix.BootstrapReceipt.PlanDigest = ReadGuid(Cursor);
		Cursor += 16;
		OutPrefix.BootstrapReceipt.InitialStateDigest = ReadGuid(Cursor);
		Cursor += 16;
		OutPrefix.ConfigFingerprint = static_cast<int32>(ReadUInt32(Cursor));
		Cursor += 4;
		OutPrefix.BodyBytes = ReadUInt64(Cursor);
		Cursor += 8;
		OutPrefix.BodyDigest = ReadGuid(Cursor);

		if (!OutPrefix.CommandProtocolDigest.IsValid()
			|| !OutPrefix.MatchSettingsDigest.IsValid()
			|| !OutPrefix.BootstrapReceipt.IsValid()
			|| !OutPrefix.BodyDigest.IsValid())
		{
			OutError = TEXT("prefix contains an invalid required digest");
			return false;
		}
		if (OutPrefix.BodyBytes == 0 || OutPrefix.BodyBytes > MaxBodyBytes)
		{
			OutError = FString::Printf(
				TEXT("declared body size %llu is outside the supported range"),
				static_cast<unsigned long long>(OutPrefix.BodyBytes));
			return false;
		}
		if (OutPrefix.BodyBytes
			!= static_cast<uint64>(FileBytes.Num() - PrefixBytes))
		{
			OutError = TEXT("declared body size does not exactly match file length");
			return false;
		}

		const FGuid ActualBodyDigest = ComputeBodyDigest(
			FileBytes.GetData() + PrefixBytes,
			static_cast<int32>(OutPrefix.BodyBytes));
		if (ActualBodyDigest != OutPrefix.BodyDigest)
		{
			OutError = TEXT("bounded body checksum mismatch");
			return false;
		}
		return true;
	}

	/** Validate the authoritative fields that the coordinator stamped before recording. */
	inline bool ValidateTurnEnvelope(
		const FSeinReplayHeader& Header,
		const FSeinReplayTurnRecord& Turn,
		int32 TicksPerTurn,
		int32 EarliestRecordableTurn,
		FString& OutError)
	{
		OutError.Reset();
		if (TicksPerTurn <= 0 || EarliestRecordableTurn < 0)
		{
			OutError = TEXT("invalid local turn timing configuration");
			return false;
		}
		if (Turn.TurnId < EarliestRecordableTurn)
		{
			OutError = FString::Printf(
				TEXT("turn %d precedes the first recordable turn %d"),
				Turn.TurnId, EarliestRecordableTurn);
			return false;
		}
		if (Turn.Commands.Num() > MaxCommandsPerTurn)
		{
			OutError = FString::Printf(
				TEXT("turn %d contains %d commands (maximum %d)"),
				Turn.TurnId, Turn.Commands.Num(), MaxCommandsPerTurn);
			return false;
		}

		const int64 CanonicalTick =
			static_cast<int64>(Turn.TurnId) * static_cast<int64>(TicksPerTurn);
		if (CanonicalTick < 0 || CanonicalTick > MAX_int32)
		{
			OutError = FString::Printf(
				TEXT("turn %d maps outside the int32 simulation tick range"),
				Turn.TurnId);
			return false;
		}

		const auto IsActiveMatchSlot = [&Header](FSeinPlayerID Player)
		{
			if (!Player.IsValid()) return false;
			for (const FSeinMatchSlot& Slot : Header.SettingsSnapshot.Slots)
			{
				if (Slot.SlotIndex == Player.Value
					&& (Slot.State == ESeinSlotState::Human
						|| Slot.State == ESeinSlotState::AI))
				{
					return true;
				}
			}
			return false;
		};

		for (const FSeinCommand& Command : Turn.Commands)
		{
			if (Command.IssuerKind != ESeinCommandIssuerKind::Player
				&& Command.IssuerKind != ESeinCommandIssuerKind::MatchAdministrator)
			{
				OutError = FString::Printf(
					TEXT("command '%s' has non-recordable issuer kind %d"),
					*Command.CommandType.ToString(),
					static_cast<int32>(Command.IssuerKind));
				return false;
			}
			if (Command.DerivedResourcePayer.IsValid())
			{
				OutError = FString::Printf(
					TEXT("external command '%s' carries a derived resource payer"),
					*Command.CommandType.ToString());
				return false;
			}
			if (!IsActiveMatchSlot(Command.PlayerID))
			{
				OutError = FString::Printf(
					TEXT("command '%s' names inactive player slot %u"),
					*Command.CommandType.ToString(), Command.PlayerID.Value);
				return false;
			}
			if (Command.Tick != static_cast<int32>(CanonicalTick))
			{
				OutError = FString::Printf(
					TEXT("command '%s' tick %d does not match canonical tick %lld"),
					*Command.CommandType.ToString(), Command.Tick,
					static_cast<long long>(CanonicalTick));
				return false;
			}
		}
		return true;
	}

	/**
	 * Resolve the exact assembled-turn range needed to reproduce a full tick-0
	 * recording through Header.EndTick. EndTick is inclusive. Grace turns before
	 * EarliestRecordableTurn have no assembled records.
	 */
	inline bool GetRequiredTurnRange(
		const FSeinReplayHeader& Header,
		int32 TicksPerTurn,
		int32 EarliestRecordableTurn,
		int32& OutFirstTurn,
		int32& OutLastTurn,
		FString& OutError)
	{
		OutFirstTurn = INDEX_NONE;
		OutLastTurn = INDEX_NONE;
		OutError.Reset();
		if (Header.StartTick != 0)
		{
			OutError = FString::Printf(
				TEXT("full replay must start at tick 0 (found %d)"),
				Header.StartTick);
			return false;
		}
		if (Header.EndTick < Header.StartTick)
		{
			OutError = FString::Printf(
				TEXT("inclusive EndTick %d precedes StartTick %d"),
				Header.EndTick, Header.StartTick);
			return false;
		}
		if (TicksPerTurn <= 0 || EarliestRecordableTurn < 0)
		{
			OutError = TEXT("invalid local turn timing configuration");
			return false;
		}

		const int32 LastStartedTurn = Header.EndTick / TicksPerTurn;
		if (LastStartedTurn < EarliestRecordableTurn)
		{
			return true;
		}
		OutFirstTurn = EarliestRecordableTurn;
		OutLastTurn = LastStartedTurn;
		return true;
	}

	/**
	 * Validate the complete applied-turn journal. Every non-grace turn whose
	 * first tick is at or before inclusive EndTick must appear exactly once,
	 * including empty heartbeat turns; no later assembled input is executable.
	 */
	inline bool ValidateJournal(
		const FSeinReplayHeader& Header,
		const TArray<FSeinReplayTurnRecord>& Turns,
		int32 TicksPerTurn,
		int32 EarliestRecordableTurn,
		FString& OutError)
	{
		int32 FirstRequiredTurn = INDEX_NONE;
		int32 LastRequiredTurn = INDEX_NONE;
		if (!GetRequiredTurnRange(
				Header,
				TicksPerTurn,
				EarliestRecordableTurn,
				FirstRequiredTurn,
				LastRequiredTurn,
				OutError))
		{
			return false;
		}

		const int64 RequiredCount = FirstRequiredTurn == INDEX_NONE
			? 0
			: static_cast<int64>(LastRequiredTurn) - FirstRequiredTurn + 1;
		if (RequiredCount > MAX_int32)
		{
			OutError = TEXT("required replay turn range exceeds array limits");
			return false;
		}

		const int32 ComparableCount = FMath::Min(
			Turns.Num(), static_cast<int32>(RequiredCount));
		for (int32 Index = 0; Index < ComparableCount; ++Index)
		{
			const int32 ExpectedTurn = FirstRequiredTurn + Index;
			if (Turns[Index].TurnId != ExpectedTurn)
			{
				OutError = FString::Printf(
					TEXT("journal entry %d has turn %d; expected contiguous turn %d"),
					Index, Turns[Index].TurnId, ExpectedTurn);
				return false;
			}
			if (!ValidateTurnEnvelope(
					Header,
					Turns[Index],
					TicksPerTurn,
					EarliestRecordableTurn,
					OutError))
			{
				return false;
			}
		}

		if (Turns.Num() < RequiredCount)
		{
			const int32 MissingTurn = FirstRequiredTurn + Turns.Num();
			OutError = FString::Printf(
				TEXT("journal ends before required turn %d (inclusive EndTick=%d)"),
				MissingTurn, Header.EndTick);
			return false;
		}
		if (Turns.Num() > RequiredCount)
		{
			OutError = FString::Printf(
				TEXT("journal contains non-executable turn %d beyond inclusive EndTick %d"),
				Turns[static_cast<int32>(RequiredCount)].TurnId,
				Header.EndTick);
			return false;
		}
		return true;
	}

	/**
	 * Finalize the server-side journal. Input-delay batching can assemble a
	 * canonical future tail which the sim never reached; validate that tail,
	 * trim it, then require the retained applied range to be exact.
	 */
	inline bool FinalizeRecordedJournal(
		const FSeinReplayHeader& Header,
		TArray<FSeinReplayTurnRecord>& InOutTurns,
		int32 TicksPerTurn,
		int32 EarliestRecordableTurn,
		FString& OutError)
	{
		int32 FirstRequiredTurn = INDEX_NONE;
		int32 LastRequiredTurn = INDEX_NONE;
		if (!GetRequiredTurnRange(
				Header,
				TicksPerTurn,
				EarliestRecordableTurn,
				FirstRequiredTurn,
				LastRequiredTurn,
				OutError))
		{
			return false;
		}

		int32 KeepCount = 0;
		int32 PreviousTurn = INDEX_NONE;
		for (const FSeinReplayTurnRecord& Turn : InOutTurns)
		{
			if (Turn.TurnId <= PreviousTurn)
			{
				OutError = FString::Printf(
					TEXT("recorded turn IDs are not strictly increasing at turn %d"),
					Turn.TurnId);
				return false;
			}
			if (!ValidateTurnEnvelope(
					Header,
					Turn,
					TicksPerTurn,
					EarliestRecordableTurn,
					OutError))
			{
				return false;
			}
			PreviousTurn = Turn.TurnId;
			if (LastRequiredTurn != INDEX_NONE && Turn.TurnId <= LastRequiredTurn)
			{
				++KeepCount;
			}
		}

		if (KeepCount < InOutTurns.Num())
		{
			InOutTurns.SetNum(KeepCount, EAllowShrinking::No);
		}
		return ValidateJournal(
			Header,
			InOutTurns,
			TicksPerTurn,
			EarliestRecordableTurn,
			OutError);
	}

	inline bool ValidateIssuerForSchema(
		ESeinCommandIssuerKind IssuerKind,
		ESeinCommandAuthorityScope AuthorityScope,
		FString& OutError)
	{
		OutError.Reset();
		if (IssuerKind == ESeinCommandIssuerKind::MatchAdministrator
			&& AuthorityScope != ESeinCommandAuthorityScope::MatchControl)
		{
			OutError = TEXT("match-administrator provenance is valid only for MatchControl schemas");
			return false;
		}
		return true;
	}

	inline bool ValidatePlaybackStartState(
		ESeinMatchState MatchState,
		bool bSimulationRunning,
		int32 CurrentTick,
		FString& OutError)
	{
		OutError.Reset();
		if (MatchState != ESeinMatchState::Lobby
			|| bSimulationRunning
			|| CurrentTick != 0)
		{
			OutError = FString::Printf(
				TEXT("playback requires a non-running tick-0 Lobby world (state=%d running=%d tick=%d)"),
				static_cast<int32>(MatchState), bSimulationRunning ? 1 : 0, CurrentTick);
			return false;
		}
		return true;
	}
}

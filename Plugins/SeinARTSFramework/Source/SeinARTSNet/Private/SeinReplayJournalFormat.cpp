#include "SeinReplayJournalFormat.h"

#include "Hash/Blake3.h"

namespace
{
	using namespace SeinReplayJournalFormat;

	void AppendUInt16(TArray<uint8>& Out, uint16 Value)
	{
		Out.Add(static_cast<uint8>(Value >> 8));
		Out.Add(static_cast<uint8>(Value));
	}

	void AppendUInt32(TArray<uint8>& Out, uint32 Value)
	{
		Out.Add(static_cast<uint8>(Value >> 24));
		Out.Add(static_cast<uint8>(Value >> 16));
		Out.Add(static_cast<uint8>(Value >> 8));
		Out.Add(static_cast<uint8>(Value));
	}

	void AppendUInt64(TArray<uint8>& Out, uint64 Value)
	{
		AppendUInt32(Out, static_cast<uint32>(Value >> 32));
		AppendUInt32(Out, static_cast<uint32>(Value));
	}

	void AppendInt32(TArray<uint8>& Out, int32 Value)
	{
		AppendUInt32(Out, static_cast<uint32>(Value));
	}

	void AppendGuid(TArray<uint8>& Out, const FGuid& Value)
	{
		AppendUInt32(Out, Value.A);
		AppendUInt32(Out, Value.B);
		AppendUInt32(Out, Value.C);
		AppendUInt32(Out, Value.D);
	}

	uint16 ReadUInt16(const uint8* Bytes)
	{
		return static_cast<uint16>(
			(static_cast<uint16>(Bytes[0]) << 8)
			| static_cast<uint16>(Bytes[1]));
	}

	uint32 ReadUInt32(const uint8* Bytes)
	{
		return (static_cast<uint32>(Bytes[0]) << 24)
			| (static_cast<uint32>(Bytes[1]) << 16)
			| (static_cast<uint32>(Bytes[2]) << 8)
			| static_cast<uint32>(Bytes[3]);
	}

	uint64 ReadUInt64(const uint8* Bytes)
	{
		return (static_cast<uint64>(ReadUInt32(Bytes)) << 32)
			| static_cast<uint64>(ReadUInt32(Bytes + 4));
	}

	int32 ReadInt32(const uint8* Bytes)
	{
		return static_cast<int32>(ReadUInt32(Bytes));
	}

	FGuid ReadGuid(const uint8* Bytes)
	{
		return FGuid(
			ReadUInt32(Bytes),
			ReadUInt32(Bytes + 4),
			ReadUInt32(Bytes + 8),
			ReadUInt32(Bytes + 12));
	}

	FGuid Digest128(const void* FirstBytes, uint64 FirstNumBytes,
		const void* SecondBytes = nullptr, uint64 SecondNumBytes = 0)
	{
		FBlake3 Hasher;
		if (FirstNumBytes > 0)
		{
			Hasher.Update(FirstBytes, FirstNumBytes);
		}
		if (SecondNumBytes > 0)
		{
			Hasher.Update(SecondBytes, SecondNumBytes);
		}
		const FBlake3Hash Hash = Hasher.Finalize();
		FGuid Result = ReadGuid(Hash.GetBytes());
		// A zero GUID is reserved throughout the replay contract for "no digest".
		if (!Result.IsValid())
		{
			Result.D = 1;
		}
		return Result;
	}

	bool IsKnownFrameType(EFrameType Type)
	{
		switch (Type)
		{
		case EFrameType::Header:
		case EFrameType::TurnBatch:
		case EFrameType::Checkpoint:
		case EFrameType::Progress:
		case EFrameType::Finalize:
			return true;
		default:
			return false;
		}
	}

	bool IsValidTurnRange(int32 FirstTurn, int32 LastTurn)
	{
		return (FirstTurn == INDEX_NONE && LastTurn == INDEX_NONE)
			|| (FirstTurn >= 0 && LastTurn >= FirstTurn);
	}

	bool ValidateFrontierValue(const FFrontier& Frontier, FString& OutError)
	{
		if (Frontier.EndTick < 0)
		{
			OutError = TEXT("replay frontier EndTick must be non-negative");
			return false;
		}
		if (Frontier.AppliedTurnCount == 0)
		{
			if (Frontier.FirstAppliedTurn != INDEX_NONE
				|| Frontier.LastAppliedTurn != INDEX_NONE)
			{
				OutError = TEXT("empty replay frontier must use INDEX_NONE turn bounds");
				return false;
			}
			return true;
		}
		if (Frontier.FirstAppliedTurn < 0
			|| Frontier.LastAppliedTurn < Frontier.FirstAppliedTurn)
		{
			OutError = TEXT("non-empty replay frontier has an invalid turn range");
			return false;
		}
		const uint64 RequiredCount =
			static_cast<uint64>(Frontier.LastAppliedTurn)
			- static_cast<uint64>(Frontier.FirstAppliedTurn) + 1ULL;
		if (RequiredCount != Frontier.AppliedTurnCount)
		{
			OutError = TEXT("replay frontier turn count does not match its contiguous range");
			return false;
		}
		return true;
	}

	bool ValidateFrameMetadata(const FFrameHeader& Header, FString& OutError)
	{
		if (!IsKnownFrameType(Header.Type))
		{
			OutError = TEXT("unknown replay journal frame type");
			return false;
		}
		if (Header.Flags != 0)
		{
			OutError = TEXT("unsupported replay journal frame flags");
			return false;
		}
		if (Header.TimelineTick < 0)
		{
			OutError = TEXT("replay journal timeline tick must be non-negative");
			return false;
		}
		if (!IsValidTurnRange(Header.FirstTurn, Header.LastTurn))
		{
			OutError = TEXT("replay journal frame has an invalid turn range");
			return false;
		}
		if (!Header.PreviousDigest.IsValid())
		{
			OutError = TEXT("replay journal frame has no hash-chain predecessor");
			return false;
		}

		switch (Header.Type)
		{
		case EFrameType::Header:
			if (Header.PayloadBytes == 0
				|| Header.PayloadBytes > MaxHeaderPayloadBytes
				|| Header.FirstTurn != INDEX_NONE
				|| Header.LastTurn != INDEX_NONE
				|| Header.TimelineTick != 0)
			{
				OutError = TEXT("invalid replay journal Header frame metadata");
				return false;
			}
			break;
		case EFrameType::TurnBatch:
			if (Header.PayloadBytes == 0
				|| Header.PayloadBytes > MaxTurnBatchPayloadBytes
				|| Header.FirstTurn < 0
				|| static_cast<int64>(Header.LastTurn) - Header.FirstTurn + 1
					> MaxTurnRecordsPerBatch)
			{
				OutError = TEXT("invalid replay journal TurnBatch frame metadata");
				return false;
			}
			break;
		case EFrameType::Checkpoint:
			if (Header.PayloadBytes
					< static_cast<uint32>(FSeinSnapshotEnvelopeCodec::PrefixBytes)
				|| Header.PayloadBytes > MaxCheckpointPayloadBytes
				|| Header.FirstTurn != INDEX_NONE
				|| Header.LastTurn != INDEX_NONE)
			{
				OutError = TEXT("invalid replay journal Checkpoint payload size or turn bounds");
				return false;
			}
			break;
		case EFrameType::Progress:
		case EFrameType::Finalize:
			if (Header.PayloadBytes != FrontierPayloadBytes)
			{
				OutError = TEXT("invalid replay journal frontier payload size");
				return false;
			}
			break;
		default:
			checkNoEntry();
			return false;
		}
		return true;
	}

	void AppendUnsignedFrameHeader(
		const FFrameHeader& Header,
		TArray<uint8>& OutBytes)
	{
		OutBytes.Append(FrameMagic, UE_ARRAY_COUNT(FrameMagic));
		AppendUInt16(OutBytes, FrameFormatVersion);
		OutBytes.Add(static_cast<uint8>(Header.Type));
		OutBytes.Add(Header.Flags);
		AppendUInt64(OutBytes, Header.Sequence);
		AppendInt32(OutBytes, Header.FirstTurn);
		AppendInt32(OutBytes, Header.LastTurn);
		AppendInt32(OutBytes, Header.TimelineTick);
		AppendUInt32(OutBytes, Header.PayloadBytes);
		AppendGuid(OutBytes, Header.PreviousDigest);
		check(OutBytes.Num() == FrameDigestOffset);
	}

	bool ParseTurnBatch(
		TConstArrayView<uint8> Bytes,
		TArray<FTurnRecord>* OutRecords,
		int32& OutFirstTurn,
		int32& OutLastTurn,
		FString& OutError)
	{
		OutFirstTurn = INDEX_NONE;
		OutLastTurn = INDEX_NONE;
		if (Bytes.Num() < 4
			|| static_cast<uint64>(Bytes.Num()) > MaxTurnBatchPayloadBytes)
		{
			OutError = TEXT("turn-batch payload size is outside the supported range");
			return false;
		}

		const uint32 Count = ReadUInt32(Bytes.GetData());
		if (Count == 0 || Count > MaxTurnRecordsPerBatch
			|| static_cast<uint64>(Count) * 8ULL
				> static_cast<uint64>(Bytes.Num() - 4))
		{
			OutError = TEXT("turn-batch record count is invalid for the bounded payload");
			return false;
		}

		TArray<FTurnRecord> Candidate;
		if (OutRecords)
		{
			Candidate.Reserve(static_cast<int32>(Count));
		}
		int32 Cursor = 4;
		int32 PreviousTurn = INDEX_NONE;
		for (uint32 Index = 0; Index < Count; ++Index)
		{
			if (Bytes.Num() - Cursor < 8)
			{
				OutError = TEXT("turn-batch record header is truncated");
				return false;
			}
			const int32 TurnId = ReadInt32(Bytes.GetData() + Cursor);
			Cursor += 4;
			const uint32 OpaqueBytes = ReadUInt32(Bytes.GetData() + Cursor);
			Cursor += 4;
			if (TurnId < 0
				|| (Index > 0
					&& (PreviousTurn == MAX_int32 || TurnId != PreviousTurn + 1)))
			{
				OutError = TEXT("turn-batch IDs are not a non-negative contiguous range");
				return false;
			}
			if (OpaqueBytes == 0 || OpaqueBytes > FSeinOpaqueCommandBatch::MaxBytes
				|| OpaqueBytes > static_cast<uint32>(Bytes.Num() - Cursor))
			{
				OutError = TEXT("turn-batch opaque command bytes are invalid or truncated");
				return false;
			}

			if (OutRecords)
			{
				FTurnRecord& Record = Candidate.AddDefaulted_GetRef();
				Record.TurnId = TurnId;
				Record.OpaqueCommands.Bytes.Append(
					Bytes.GetData() + Cursor,
					static_cast<int32>(OpaqueBytes));
			}
			Cursor += static_cast<int32>(OpaqueBytes);
			PreviousTurn = TurnId;
			if (Index == 0)
			{
				OutFirstTurn = TurnId;
			}
			OutLastTurn = TurnId;
		}
		if (Cursor != Bytes.Num())
		{
			OutError = TEXT("turn-batch payload has trailing bytes");
			return false;
		}
		if (OutRecords)
		{
			*OutRecords = MoveTemp(Candidate);
		}
		return true;
	}

	bool ValidatePayloadSemantics(
		const FFrameHeader& Header,
		TConstArrayView<uint8> Payload,
		FString& OutError)
	{
		switch (Header.Type)
		{
		case EFrameType::Header:
			return true;
		case EFrameType::TurnBatch:
		{
			int32 FirstTurn = INDEX_NONE;
			int32 LastTurn = INDEX_NONE;
			if (!ParseTurnBatch(
					Payload, nullptr, FirstTurn, LastTurn, OutError))
			{
				return false;
			}
			if (FirstTurn != Header.FirstTurn || LastTurn != Header.LastTurn)
			{
				OutError = TEXT("TurnBatch frame range does not match its payload");
				return false;
			}
			return true;
		}
		case EFrameType::Checkpoint:
		{
			// Recomputing only the outer frame digest must never admit damaged
			// checkpoint bytes. Fully validate the canonical snapshot envelope
			// (body digest, directory, section leaves, and aggregate root) here;
			// trusted-local snapshot-body deserialization remains the reader's
			// separate semantic/adoption gate.
			FSeinSnapshotEnvelope Envelope;
			FSeinSnapshotEnvelopeMetadata Metadata;
			FString SnapshotError;
			if (!FSeinSnapshotEnvelopeCodec::Decode(
					Payload,
					Envelope,
					Metadata,
					SnapshotError))
			{
				OutError = FString::Printf(
					TEXT("Checkpoint snapshot envelope is invalid: %s"),
					*SnapshotError);
				return false;
			}
			const uint64 DeclaredBytes =
				static_cast<uint64>(FSeinSnapshotEnvelopeCodec::PrefixBytes)
				+ Metadata.BodyBytes;
			if (DeclaredBytes != static_cast<uint64>(Payload.Num())
				|| Metadata.SnapshotTick != Header.TimelineTick)
			{
				OutError = TEXT("Checkpoint frame does not match its snapshot length/tick");
				return false;
			}
			return true;
		}
		case EFrameType::Progress:
		case EFrameType::Finalize:
		{
			FFrontier Frontier;
			if (!SeinReplayJournalFormat::DecodeFrontier(
					Payload, Frontier, OutError))
			{
				return false;
			}
			if (Frontier.EndTick != Header.TimelineTick
				|| Frontier.FirstAppliedTurn != Header.FirstTurn
				|| Frontier.LastAppliedTurn != Header.LastTurn)
			{
				OutError = TEXT("frontier frame metadata does not match its payload");
				return false;
			}
			return true;
		}
		default:
			checkNoEntry();
			return false;
		}
	}
}

bool SeinReplayJournalFormat::BuildPrefix(
	const FGuid& CommandProtocolDigest,
	const FGuid& MatchSettingsDigest,
	const FSeinMatchBootstrapReceipt& BootstrapReceipt,
	int32 ConfigFingerprint,
	const FGuid& JournalID,
	TArray<uint8>& OutBytes,
	FPrefix& OutPrefix,
	FString& OutError)
{
	OutError.Reset();
	if (!CommandProtocolDigest.IsValid()
		|| !MatchSettingsDigest.IsValid()
		|| !BootstrapReceipt.IsValid()
		|| BootstrapReceipt.ContractDigest != MatchSettingsDigest
		|| !JournalID.IsValid())
	{
		OutError = TEXT("required replay journal compatibility/identity fields are invalid");
		return false;
	}

	TArray<uint8> CandidateBytes;
	CandidateBytes.Reserve(PrefixBytes);
	CandidateBytes.Append(Magic, UE_ARRAY_COUNT(Magic));
	AppendUInt32(CandidateBytes, FileFormatVersion);
	AppendUInt32(CandidateBytes, PrefixBytes);
	AppendGuid(CandidateBytes, CommandProtocolDigest);
	AppendGuid(CandidateBytes, MatchSettingsDigest);
	AppendInt32(CandidateBytes, BootstrapReceipt.FormatVersion);
	AppendGuid(CandidateBytes, BootstrapReceipt.SimulationContentDigest);
	AppendGuid(CandidateBytes, BootstrapReceipt.StateContractDigest);
	AppendGuid(CandidateBytes, BootstrapReceipt.PlanDigest);
	AppendGuid(CandidateBytes, BootstrapReceipt.InitialStateDigest);
	AppendInt32(CandidateBytes, ConfigFingerprint);
	AppendGuid(CandidateBytes, JournalID);
	check(CandidateBytes.Num() == PrefixBytes - 16);

	FPrefix CandidatePrefix;
	CandidatePrefix.CommandProtocolDigest = CommandProtocolDigest;
	CandidatePrefix.MatchSettingsDigest = MatchSettingsDigest;
	CandidatePrefix.BootstrapReceipt = BootstrapReceipt;
	CandidatePrefix.ConfigFingerprint = ConfigFingerprint;
	CandidatePrefix.JournalID = JournalID;
	CandidatePrefix.PrefixDigest = Digest128(
		CandidateBytes.GetData(), CandidateBytes.Num());
	AppendGuid(CandidateBytes, CandidatePrefix.PrefixDigest);
	check(CandidateBytes.Num() == PrefixBytes);

	OutBytes = MoveTemp(CandidateBytes);
	OutPrefix = MoveTemp(CandidatePrefix);
	return true;
}

bool SeinReplayJournalFormat::ParsePrefix(
	TConstArrayView<uint8> Bytes,
	FPrefix& OutPrefix,
	FString& OutError)
{
	OutError.Reset();
	if (Bytes.Num() != PrefixBytes)
	{
		OutError = TEXT("replay journal prefix must be exactly 152 bytes");
		return false;
	}
	if (FMemory::Memcmp(Bytes.GetData(), Magic, UE_ARRAY_COUNT(Magic)) != 0)
	{
		OutError = TEXT("replay journal magic mismatch");
		return false;
	}
	const uint8* Cursor = Bytes.GetData() + UE_ARRAY_COUNT(Magic);
	const uint32 Version = ReadUInt32(Cursor);
	Cursor += 4;
	const uint32 DeclaredPrefixBytes = ReadUInt32(Cursor);
	Cursor += 4;
	if (Version != FileFormatVersion || DeclaredPrefixBytes != PrefixBytes)
	{
		OutError = TEXT("unsupported replay journal version or prefix size");
		return false;
	}

	FPrefix Candidate;
	Candidate.CommandProtocolDigest = ReadGuid(Cursor);
	Cursor += 16;
	Candidate.MatchSettingsDigest = ReadGuid(Cursor);
	Cursor += 16;
	Candidate.BootstrapReceipt.FormatVersion = ReadInt32(Cursor);
	Cursor += 4;
	Candidate.BootstrapReceipt.ContractDigest = Candidate.MatchSettingsDigest;
	Candidate.BootstrapReceipt.SimulationContentDigest = ReadGuid(Cursor);
	Cursor += 16;
	Candidate.BootstrapReceipt.StateContractDigest = ReadGuid(Cursor);
	Cursor += 16;
	Candidate.BootstrapReceipt.PlanDigest = ReadGuid(Cursor);
	Cursor += 16;
	Candidate.BootstrapReceipt.InitialStateDigest = ReadGuid(Cursor);
	Cursor += 16;
	Candidate.ConfigFingerprint = ReadInt32(Cursor);
	Cursor += 4;
	Candidate.JournalID = ReadGuid(Cursor);
	Cursor += 16;
	Candidate.PrefixDigest = ReadGuid(Cursor);

	if (!Candidate.CommandProtocolDigest.IsValid()
		|| !Candidate.MatchSettingsDigest.IsValid()
		|| !Candidate.BootstrapReceipt.IsValid()
		|| !Candidate.JournalID.IsValid()
		|| !Candidate.PrefixDigest.IsValid())
	{
		OutError = TEXT("replay journal prefix contains an invalid required identity");
		return false;
	}
	const FGuid ActualDigest = Digest128(
		Bytes.GetData(), PrefixBytes - 16);
	if (ActualDigest != Candidate.PrefixDigest)
	{
		OutError = TEXT("replay journal prefix digest mismatch");
		return false;
	}

	OutPrefix = MoveTemp(Candidate);
	return true;
}

bool SeinReplayJournalFormat::BuildFrame(
	EFrameType Type,
	uint8 Flags,
	uint64 Sequence,
	int32 FirstTurn,
	int32 LastTurn,
	int32 TimelineTick,
	const FGuid& PreviousDigest,
	TConstArrayView<uint8> Payload,
	TArray<uint8>& OutFrameBytes,
	FFrameHeader& OutHeader,
	FString& OutError)
{
	OutError.Reset();
	FFrameHeader CandidateHeader;
	CandidateHeader.Type = Type;
	CandidateHeader.Flags = Flags;
	CandidateHeader.Sequence = Sequence;
	CandidateHeader.FirstTurn = FirstTurn;
	CandidateHeader.LastTurn = LastTurn;
	CandidateHeader.TimelineTick = TimelineTick;
	CandidateHeader.PayloadBytes = static_cast<uint32>(Payload.Num());
	CandidateHeader.PreviousDigest = PreviousDigest;
	if (!ValidateFrameMetadata(CandidateHeader, OutError)
		|| !ValidatePayloadSemantics(CandidateHeader, Payload, OutError))
	{
		return false;
	}

	TArray<uint8> CandidateBytes;
	CandidateBytes.Reserve(FrameHeaderBytes + Payload.Num());
	AppendUnsignedFrameHeader(CandidateHeader, CandidateBytes);
	CandidateHeader.CurrentDigest = Digest128(
		CandidateBytes.GetData(), CandidateBytes.Num(),
		Payload.GetData(), Payload.Num());
	AppendGuid(CandidateBytes, CandidateHeader.CurrentDigest);
	check(CandidateBytes.Num() == FrameHeaderBytes);
	CandidateBytes.Append(Payload.GetData(), Payload.Num());

	OutFrameBytes = MoveTemp(CandidateBytes);
	OutHeader = MoveTemp(CandidateHeader);
	return true;
}

bool SeinReplayJournalFormat::ParseFrameHeader(
	TConstArrayView<uint8> Bytes,
	FFrameHeader& OutHeader,
	FString& OutError)
{
	OutError.Reset();
	if (Bytes.Num() != FrameHeaderBytes)
	{
		OutError = TEXT("replay journal frame header must be exactly 64 bytes");
		return false;
	}
	if (FMemory::Memcmp(Bytes.GetData(), FrameMagic, UE_ARRAY_COUNT(FrameMagic)) != 0)
	{
		OutError = TEXT("replay journal frame magic mismatch");
		return false;
	}
	const uint8* Cursor = Bytes.GetData() + UE_ARRAY_COUNT(FrameMagic);
	const uint16 Version = ReadUInt16(Cursor);
	Cursor += 2;
	if (Version != FrameFormatVersion)
	{
		OutError = TEXT("unsupported replay journal frame version");
		return false;
	}

	FFrameHeader Candidate;
	Candidate.Type = static_cast<EFrameType>(*Cursor++);
	Candidate.Flags = *Cursor++;
	Candidate.Sequence = ReadUInt64(Cursor);
	Cursor += 8;
	Candidate.FirstTurn = ReadInt32(Cursor);
	Cursor += 4;
	Candidate.LastTurn = ReadInt32(Cursor);
	Cursor += 4;
	Candidate.TimelineTick = ReadInt32(Cursor);
	Cursor += 4;
	Candidate.PayloadBytes = ReadUInt32(Cursor);
	Cursor += 4;
	Candidate.PreviousDigest = ReadGuid(Cursor);
	Cursor += 16;
	Candidate.CurrentDigest = ReadGuid(Cursor);

	if (!ValidateFrameMetadata(Candidate, OutError)
		|| !Candidate.CurrentDigest.IsValid())
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("replay journal frame has no current digest");
		}
		return false;
	}
	OutHeader = MoveTemp(Candidate);
	return true;
}

bool SeinReplayJournalFormat::ValidateFrame(
	const FFrameHeader& Header,
	TConstArrayView<uint8> Payload,
	FString& OutError)
{
	OutError.Reset();
	if (!ValidateFrameMetadata(Header, OutError)
		|| !Header.CurrentDigest.IsValid())
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("replay journal frame has no current digest");
		}
		return false;
	}
	if (Header.PayloadBytes != static_cast<uint32>(Payload.Num()))
	{
		OutError = TEXT("replay journal frame payload length mismatch");
		return false;
	}
	if (!ValidatePayloadSemantics(Header, Payload, OutError))
	{
		return false;
	}

	TArray<uint8> UnsignedHeader;
	UnsignedHeader.Reserve(FrameDigestOffset);
	AppendUnsignedFrameHeader(Header, UnsignedHeader);
	const FGuid ActualDigest = Digest128(
		UnsignedHeader.GetData(), UnsignedHeader.Num(),
		Payload.GetData(), Payload.Num());
	if (ActualDigest != Header.CurrentDigest)
	{
		OutError = TEXT("replay journal frame digest mismatch");
		return false;
	}
	return true;
}

bool SeinReplayJournalFormat::ValidateFrame(
	TConstArrayView<uint8> FrameBytes,
	FFrameHeader& OutHeader,
	FString& OutError)
{
	OutError.Reset();
	if (FrameBytes.Num() < FrameHeaderBytes)
	{
		OutError = TEXT("replay journal frame is smaller than its fixed header");
		return false;
	}
	FFrameHeader Candidate;
	if (!ParseFrameHeader(
			FrameBytes.Slice(0, FrameHeaderBytes), Candidate, OutError))
	{
		return false;
	}
	const int64 DeclaredFrameBytes =
		static_cast<int64>(FrameHeaderBytes) + Candidate.PayloadBytes;
	if (DeclaredFrameBytes != FrameBytes.Num())
	{
		OutError = TEXT("replay journal frame length does not match its header");
		return false;
	}
	if (!ValidateFrame(
			Candidate,
			FrameBytes.Slice(FrameHeaderBytes, Candidate.PayloadBytes),
			OutError))
	{
		return false;
	}
	OutHeader = MoveTemp(Candidate);
	return true;
}

bool SeinReplayJournalFormat::EncodeTurnBatch(
	TConstArrayView<FTurnRecord> Records,
	TArray<uint8>& OutBytes,
	FString& OutError)
{
	OutError.Reset();
	if (Records.IsEmpty() || Records.Num() > MaxTurnRecordsPerBatch)
	{
		OutError = TEXT("turn batch must contain 1..1024 records");
		return false;
	}

	uint64 RequiredBytes = 4;
	int32 PreviousTurn = INDEX_NONE;
	for (int32 Index = 0; Index < Records.Num(); ++Index)
	{
		const FTurnRecord& Record = Records[Index];
		if (Record.TurnId < 0
			|| (Index > 0
				&& (PreviousTurn == MAX_int32
					|| Record.TurnId != PreviousTurn + 1)))
		{
			OutError = TEXT("turn batch IDs must be a non-negative contiguous range");
			return false;
		}
		if (Record.OpaqueCommands.Bytes.IsEmpty()
			|| Record.OpaqueCommands.Bytes.Num()
				> static_cast<int32>(FSeinOpaqueCommandBatch::MaxBytes))
		{
			OutError = TEXT("turn batch contains invalid opaque command bytes");
			return false;
		}
		const uint64 RecordBytes = 8ULL
			+ static_cast<uint64>(Record.OpaqueCommands.Bytes.Num());
		if (RequiredBytes > MaxTurnBatchPayloadBytes - RecordBytes)
		{
			OutError = TEXT("turn batch exceeds the 64 MiB payload bound");
			return false;
		}
		RequiredBytes += RecordBytes;
		PreviousTurn = Record.TurnId;
	}

	TArray<uint8> Candidate;
	Candidate.Reserve(static_cast<int32>(RequiredBytes));
	AppendUInt32(Candidate, static_cast<uint32>(Records.Num()));
	for (const FTurnRecord& Record : Records)
	{
		AppendInt32(Candidate, Record.TurnId);
		AppendUInt32(
			Candidate,
			static_cast<uint32>(Record.OpaqueCommands.Bytes.Num()));
		Candidate.Append(Record.OpaqueCommands.Bytes);
	}
	check(static_cast<uint64>(Candidate.Num()) == RequiredBytes);
	OutBytes = MoveTemp(Candidate);
	return true;
}

bool SeinReplayJournalFormat::DecodeTurnBatch(
	TConstArrayView<uint8> Bytes,
	TArray<FTurnRecord>& OutRecords,
	FString& OutError)
{
	OutError.Reset();
	TArray<FTurnRecord> Candidate;
	int32 FirstTurn = INDEX_NONE;
	int32 LastTurn = INDEX_NONE;
	if (!ParseTurnBatch(
			Bytes, &Candidate, FirstTurn, LastTurn, OutError))
	{
		return false;
	}
	OutRecords = MoveTemp(Candidate);
	return true;
}

bool SeinReplayJournalFormat::EncodeFrontier(
	const FFrontier& Frontier,
	TArray<uint8>& OutBytes,
	FString& OutError)
{
	OutError.Reset();
	if (!ValidateFrontierValue(Frontier, OutError))
	{
		return false;
	}
	TArray<uint8> Candidate;
	Candidate.Reserve(FrontierPayloadBytes);
	AppendInt32(Candidate, Frontier.EndTick);
	AppendInt32(Candidate, Frontier.FirstAppliedTurn);
	AppendInt32(Candidate, Frontier.LastAppliedTurn);
	AppendUInt32(Candidate, Frontier.AppliedTurnCount);
	check(Candidate.Num() == FrontierPayloadBytes);
	OutBytes = MoveTemp(Candidate);
	return true;
}

bool SeinReplayJournalFormat::DecodeFrontier(
	TConstArrayView<uint8> Bytes,
	FFrontier& OutFrontier,
	FString& OutError)
{
	OutError.Reset();
	if (Bytes.Num() != FrontierPayloadBytes)
	{
		OutError = TEXT("replay frontier payload must be exactly 16 bytes");
		return false;
	}
	FFrontier Candidate;
	Candidate.EndTick = ReadInt32(Bytes.GetData());
	Candidate.FirstAppliedTurn = ReadInt32(Bytes.GetData() + 4);
	Candidate.LastAppliedTurn = ReadInt32(Bytes.GetData() + 8);
	Candidate.AppliedTurnCount = ReadUInt32(Bytes.GetData() + 12);
	if (!ValidateFrontierValue(Candidate, OutError))
	{
		return false;
	}
	OutFrontier = MoveTemp(Candidate);
	return true;
}

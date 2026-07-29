/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file SeinReplayWireCodec.cpp
 */

#include "SeinReplayWireCodec.h"

#include "SeinReplayFormat.h"

namespace
{
	constexpr uint32 BodyMagic = 0x53524236u; // SRB6
	constexpr uint16 BodyVersion = 4;
	constexpr int32 MaxHeaderStringBytes = 1024;
	constexpr int32 MaxReplayPlayers = MAX_uint8;
	constexpr int32 MaxReplayTurns = 1000000;
	constexpr int32 MaxSettingsBytes = 8 * 1024 * 1024;
	constexpr int32 MaxSettingsElements = 65536;

	bool ChargeDecodedAllocation(uint64 Amount, uint64& Used, FString& Error)
	{
		if (Amount > FSeinReplayWireCodec::MaxDecodedAllocationBytes
			|| Used > FSeinReplayWireCodec::MaxDecodedAllocationBytes - Amount)
		{
			if (Error.IsEmpty()) Error = TEXT("replay exceeds its decoded-allocation budget");
			return false;
		}
		Used += Amount;
		return true;
	}

	uint64 NativeDecodedStringBytes(const FString& Text)
	{
		if (Text.IsEmpty()) return 0;
		FTCHARToUTF8 Converted(*Text, Text.Len());
		return (static_cast<uint64>(Converted.Length()) + 1u) * sizeof(TCHAR);
	}

	class FWriter
	{
	public:
		FWriter(TArray<uint8>& InBytes, FString& InError)
			: Bytes(InBytes), Error(InError)
		{
			Bytes.Reset();
			Error.Reset();
		}

		bool Fail(const FString& Message)
		{
			if (Error.IsEmpty()) Error = Message;
			return false;
		}

		bool Raw(const void* Data, int32 Count)
		{
			if (Count < 0
				|| static_cast<uint64>(Count) > SeinReplayFormat::MaxBodyBytes
				|| static_cast<uint64>(Bytes.Num())
					> SeinReplayFormat::MaxBodyBytes - static_cast<uint64>(Count))
			{
				return Fail(TEXT("replay body exceeds the hard byte cap"));
			}
			if (Count > 0) Bytes.Append(static_cast<const uint8*>(Data), Count);
			return true;
		}

		bool UInt(uint64 Value, int32 Width)
		{
			uint8 Encoded[8];
			if (Width != 1 && Width != 2 && Width != 4 && Width != 8)
				return Fail(TEXT("invalid replay integer width"));
			for (int32 Index = 0; Index < Width; ++Index)
				Encoded[Index] = static_cast<uint8>(Value >> ((Width - 1 - Index) * 8));
			return Raw(Encoded, Width);
		}

		bool U8(uint8 Value) { return UInt(Value, 1); }
		bool U16(uint16 Value) { return UInt(Value, 2); }
		bool U32(uint32 Value) { return UInt(Value, 4); }
		bool I32(int32 Value) { return UInt(static_cast<uint32>(Value), 4); }
		bool I64(int64 Value) { return UInt(static_cast<uint64>(Value), 8); }

		bool Guid(const FGuid& Value)
		{
			return U32(Value.A) && U32(Value.B) && U32(Value.C) && U32(Value.D);
		}

		bool Utf8(const FString& Value, int32 MaxBytes)
		{
			if (Value.Len() != FCString::Strlen(*Value))
				return Fail(TEXT("replay string contains an embedded null"));
			FTCHARToUTF8 Converted(*Value, Value.Len());
			if (Converted.Length() < 0 || Converted.Length() > MaxBytes)
				return Fail(TEXT("replay string exceeds its byte cap"));
			return U32(static_cast<uint32>(Converted.Length()))
				&& Raw(Converted.Get(), Converted.Length());
		}

		TArray<uint8>& Bytes;
		FString& Error;
	};

	class FReader
	{
	public:
		FReader(TConstArrayView<uint8> InBytes, FString& InError)
			: Bytes(InBytes), Error(InError)
		{
			Error.Reset();
		}

		bool Fail(const FString& Message)
		{
			if (Error.IsEmpty()) Error = Message;
			return false;
		}

		int32 Remaining() const { return Bytes.Num() - Offset; }
		bool AtEnd() const { return Offset == Bytes.Num(); }

		bool UInt(uint64& Out, int32 Width)
		{
			if ((Width != 1 && Width != 2 && Width != 4 && Width != 8)
				|| Width > Remaining()) return Fail(TEXT("truncated replay integer"));
			Out = 0;
			for (int32 Index = 0; Index < Width; ++Index) Out = (Out << 8) | Bytes[Offset++];
			return true;
		}

		bool U8(uint8& Out)
		{
			uint64 Value = 0; if (!UInt(Value, 1)) return false;
			Out = static_cast<uint8>(Value); return true;
		}
		bool U16(uint16& Out)
		{
			uint64 Value = 0; if (!UInt(Value, 2)) return false;
			Out = static_cast<uint16>(Value); return true;
		}
		bool U32(uint32& Out)
		{
			uint64 Value = 0; if (!UInt(Value, 4)) return false;
			Out = static_cast<uint32>(Value); return true;
		}
		bool I32(int32& Out)
		{
			uint32 Value = 0; if (!U32(Value)) return false;
			Out = BitCast<int32>(Value); return true;
		}
		bool I64(int64& Out)
		{
			uint64 Value = 0; if (!UInt(Value, 8)) return false;
			Out = BitCast<int64>(Value); return true;
		}

		bool Guid(FGuid& Out)
		{
			return U32(Out.A) && U32(Out.B) && U32(Out.C) && U32(Out.D);
		}

		bool Utf8(
			FString& Out,
			int32 MaxBytes,
			TFunctionRef<bool(uint64)> ChargeNativeAllocation)
		{
			uint32 Count = 0;
			if (!U32(Count) || Count > static_cast<uint32>(MaxBytes)
				|| Count > static_cast<uint32>(Remaining()))
				return Fail(TEXT("replay string length exceeds its cap or remaining bytes"));
			if (Count == 0) { Out.Reset(); return true; }
			const uint64 NativeStringBytes =
				(static_cast<uint64>(Count) + 1u) * sizeof(TCHAR);
			if (!ChargeNativeAllocation(NativeStringBytes))
				return Fail(TEXT("replay string exceeds its decoded-allocation budget"));
			const ANSICHAR* Source = reinterpret_cast<const ANSICHAR*>(Bytes.GetData() + Offset);
			bool bContainsNull = false;
			for (uint32 Index = 0; Index < Count; ++Index)
				bContainsNull |= Source[Index] == 0;
			if (bContainsNull)
				return Fail(TEXT("replay string contains an embedded null"));
			FUTF8ToTCHAR Converted(Source, static_cast<int32>(Count));
			Out = FString(Converted.Length(), Converted.Get());
			if (Out.Len() != FCString::Strlen(*Out))
				return Fail(TEXT("replay string contains an embedded null"));
			FTCHARToUTF8 RoundTrip(*Out, Out.Len());
			if (RoundTrip.Length() != static_cast<int32>(Count)
				|| FMemory::Memcmp(RoundTrip.Get(), Source, Count) != 0)
				return Fail(TEXT("replay string is not canonical UTF-8"));
			Offset += static_cast<int32>(Count);
			return true;
		}

		bool Slice(int32 Count, TConstArrayView<uint8>& Out)
		{
			if (Count < 0 || Count > Remaining()) return Fail(TEXT("replay frame exceeds remaining bytes"));
			Out = TConstArrayView<uint8>(Bytes.GetData() + Offset, Count);
			Offset += Count;
			return true;
		}

		TConstArrayView<uint8> Bytes;
		int32 Offset = 0;
		FString& Error;
	};

	bool EncodeHeader(
		const FSeinReplayHeader& Header,
		FSeinStructWireCatalogView Catalog,
		FWriter& Writer,
		uint64& DecodedAllocationBytes,
		FString& Error)
	{
		TArray<uint8> SettingsBytes;
		const FSeinStructWireLimits SettingsLimits{
			MaxSettingsBytes, MaxSettingsElements, MaxHeaderStringBytes, 64 };
		uint64 SettingsAllocationBytes = 0;
		if (!FSeinCanonicalStateCodec::Encode(
			FSeinMatchSettings::StaticStruct(), &Header.SettingsSnapshot,
			Catalog, SettingsLimits, SettingsBytes, Error,
			&SettingsAllocationBytes)
			|| SettingsAllocationBytes > MAX_uint64 / 2u
			|| !ChargeDecodedAllocation(
				SettingsAllocationBytes * 2u, DecodedAllocationBytes, Error)) return false;
		if (Header.Players.Num() > MaxReplayPlayers) return Writer.Fail(TEXT("replay player count exceeds its cap"));
		if (!ChargeDecodedAllocation(
			static_cast<uint64>(Header.Players.Num()) * sizeof(FSeinPlayerRegistration),
			DecodedAllocationBytes, Error)) return false;
		if (!ChargeDecodedAllocation(
				NativeDecodedStringBytes(Header.FrameworkVersion),
				DecodedAllocationBytes, Error)
			|| !ChargeDecodedAllocation(
				NativeDecodedStringBytes(Header.GameVersion),
				DecodedAllocationBytes, Error)
			|| !ChargeDecodedAllocation(
				NativeDecodedStringBytes(Header.MapIdentifier),
				DecodedAllocationBytes, Error)) return false;

		if (!Header.BootstrapReceipt.IsValid()
			|| Header.BootstrapReceipt.ContractDigest
				!= Header.MatchSettingsDigest)
		{
			return Writer.Fail(
				TEXT("replay bootstrap receipt is invalid or disagrees with match settings"));
		}

		return Writer.Guid(Header.CommandProtocolDigest)
			&& Writer.Guid(Header.MatchSettingsDigest)
			&& Writer.I32(Header.BootstrapReceipt.FormatVersion)
			&& Writer.Guid(Header.BootstrapReceipt.ContractDigest)
			&& Writer.Guid(
				Header.BootstrapReceipt.SimulationContentDigest)
			&& Writer.Guid(Header.BootstrapReceipt.StateContractDigest)
			&& Writer.Guid(Header.BootstrapReceipt.PlanDigest)
			&& Writer.Guid(Header.BootstrapReceipt.InitialStateDigest)
			&& Writer.I32(Header.ConfigFingerprint)
			&& Writer.Utf8(Header.FrameworkVersion, MaxHeaderStringBytes)
			&& Writer.Utf8(Header.GameVersion, MaxHeaderStringBytes)
			&& Writer.Utf8(Header.MapIdentifier, MaxHeaderStringBytes)
			&& Writer.I64(Header.RandomSeed)
			&& Writer.U32(static_cast<uint32>(SettingsBytes.Num()))
			&& Writer.Raw(SettingsBytes.GetData(), SettingsBytes.Num())
			&& Writer.U32(static_cast<uint32>(Header.Players.Num()));
	}

	bool DecodeHeader(
		FReader& Reader,
		FSeinStructWireCatalogView Catalog,
		FSeinReplayHeader& Header,
		uint64& DecodedAllocationBytes,
		FString& Error)
	{
		uint32 SettingsBytes = 0;
		TConstArrayView<uint8> SettingsView;
		uint32 PlayerCount = 0;
		auto ChargeHeaderString = [&DecodedAllocationBytes, &Error](uint64 Amount)
		{
			return ChargeDecodedAllocation(Amount, DecodedAllocationBytes, Error);
		};
		if (!Reader.Guid(Header.CommandProtocolDigest)
			|| !Reader.Guid(Header.MatchSettingsDigest)
			|| !Reader.I32(Header.BootstrapReceipt.FormatVersion)
			|| !Reader.Guid(Header.BootstrapReceipt.ContractDigest)
			|| !Reader.Guid(
				Header.BootstrapReceipt.SimulationContentDigest)
			|| !Reader.Guid(Header.BootstrapReceipt.StateContractDigest)
			|| !Reader.Guid(Header.BootstrapReceipt.PlanDigest)
			|| !Reader.Guid(Header.BootstrapReceipt.InitialStateDigest)
			|| !Header.BootstrapReceipt.IsValid()
			|| Header.BootstrapReceipt.ContractDigest
				!= Header.MatchSettingsDigest
			|| !Reader.I32(Header.ConfigFingerprint)
			|| !Reader.Utf8(
				Header.FrameworkVersion, MaxHeaderStringBytes, ChargeHeaderString)
			|| !Reader.Utf8(
				Header.GameVersion, MaxHeaderStringBytes, ChargeHeaderString)
			|| !Reader.Utf8(
				Header.MapIdentifier, MaxHeaderStringBytes, ChargeHeaderString)
			|| !Reader.I64(Header.RandomSeed)
			|| !Reader.U32(SettingsBytes)
			|| SettingsBytes > MaxSettingsBytes
			|| !Reader.Slice(static_cast<int32>(SettingsBytes), SettingsView)
			|| !Reader.U32(PlayerCount)
			|| PlayerCount > MaxReplayPlayers
			|| static_cast<uint64>(PlayerCount) * 5u > static_cast<uint64>(Reader.Remaining()))
		{
			if (Error.IsEmpty()) Error = TEXT("invalid bounded replay header");
			return false;
		}
		const FSeinStructWireLimits SettingsLimits{
			MaxSettingsBytes, MaxSettingsElements, MaxHeaderStringBytes, 64 };
		uint64 SettingsAllocationBytes = 0;
		if (!FSeinCanonicalStateCodec::Decode(
			SettingsView, FSeinMatchSettings::StaticStruct(), &Header.SettingsSnapshot,
			Catalog, SettingsLimits, Error, &SettingsAllocationBytes)
			|| SettingsAllocationBytes > MAX_uint64 / 2u
			|| !ChargeDecodedAllocation(
				SettingsAllocationBytes * 2u, DecodedAllocationBytes, Error)) return false;

		if (!ChargeDecodedAllocation(
			static_cast<uint64>(PlayerCount) * sizeof(FSeinPlayerRegistration),
			DecodedAllocationBytes, Error)) return false;
		Header.Players.SetNum(static_cast<int32>(PlayerCount));
		for (FSeinPlayerRegistration& Player : Header.Players)
		{
			uint8 IsAI = 0, IsSpectator = 0;
			if (!Reader.U8(Player.PlayerID.Value)
				|| !Reader.U8(Player.FactionID.Value)
				|| !Reader.U8(Player.TeamID)
				|| !Reader.U8(IsAI) || IsAI > 1
				|| !Reader.U8(IsSpectator) || IsSpectator > 1)
				return false;
			Player.bIsAI = IsAI != 0;
			Player.bIsSpectator = IsSpectator != 0;
		}
		int64 RecordedTicks = 0;
		if (!Reader.I32(Header.StartTick)
			|| !Reader.I32(Header.EndTick)
			|| !Reader.I64(RecordedTicks)
			|| RecordedTicks < FDateTime::MinValue().GetTicks()
			|| RecordedTicks > FDateTime::MaxValue().GetTicks())
		{
			if (Error.IsEmpty()) Error = TEXT("invalid replay tick/date metadata");
			return false;
		}
		Header.RecordedAt = FDateTime(RecordedTicks);
		return true;
	}
}

bool FSeinReplayWireCodec::Encode(
	const FSeinReplay& Replay,
	FSeinStructWireCatalogView HeaderCatalog,
	FSeinCommandWireSchemaLookup FindSchema,
	TArray<uint8>& OutBody,
	FString& OutError,
	uint64* OutDecodedAllocationBytes)
{
	if (OutDecodedAllocationBytes) *OutDecodedAllocationBytes = 0;
	OutBody.Reset();
	OutError.Reset();
	if (Replay.Turns.Num() > MaxReplayTurns)
	{
		OutBody.Reset();
		OutError = TEXT("replay turn count exceeds its hard cap");
		return false;
	}
	FWriter Writer(OutBody, OutError);
	uint64 DecodedAllocationBytes = 0;
	if (!Writer.U32(BodyMagic) || !Writer.U16(BodyVersion)
		|| !EncodeHeader(
			Replay.Header, HeaderCatalog, Writer,
			DecodedAllocationBytes, OutError)) return false;
	for (const FSeinPlayerRegistration& Player : Replay.Header.Players)
	{
		if (!Writer.U8(Player.PlayerID.Value)
			|| !Writer.U8(Player.FactionID.Value)
			|| !Writer.U8(Player.TeamID)
			|| !Writer.U8(Player.bIsAI ? 1 : 0)
			|| !Writer.U8(Player.bIsSpectator ? 1 : 0)) return false;
	}
	if (!Writer.I32(Replay.Header.StartTick)
		|| !Writer.I32(Replay.Header.EndTick)
		|| !Writer.I64(Replay.Header.RecordedAt.GetTicks())
		|| !ChargeDecodedAllocation(
			static_cast<uint64>(Replay.Turns.Num()) * sizeof(FSeinReplayTurnRecord),
			DecodedAllocationBytes, OutError)
		|| !Writer.U32(static_cast<uint32>(Replay.Turns.Num()))) return false;

	for (const FSeinReplayTurnRecord& Turn : Replay.Turns)
	{
		if (Turn.Commands.Num() > SeinReplayFormat::MaxCommandsPerTurn)
			return Writer.Fail(TEXT("replay turn command count exceeds its cap"));
		if (!ChargeDecodedAllocation(
				static_cast<uint64>(Turn.Commands.Num()) * sizeof(FSeinCommand),
				DecodedAllocationBytes, OutError)
			|| !Writer.I32(Turn.TurnId)
			|| !Writer.U32(static_cast<uint32>(Turn.Commands.Num()))) return false;
		for (const FSeinCommand& Command : Turn.Commands)
		{
			FSeinCommandSchemaDescriptor Schema;
			TArray<uint8> EncodedCommand;
			uint64 CommandAllocationBytes = 0;
			if (!FindSchema(Command.CommandType, Command.SchemaVersion, Schema)
				|| !FSeinCommandWireCodec::Encode(
					Command, Schema, EncodedCommand, OutError,
					&CommandAllocationBytes)
				|| !ChargeDecodedAllocation(
					CommandAllocationBytes, DecodedAllocationBytes, OutError)
				|| !Writer.U32(static_cast<uint32>(EncodedCommand.Num()))
				|| !Writer.Raw(EncodedCommand.GetData(), EncodedCommand.Num())) return false;
		}
	}
	if (OutDecodedAllocationBytes)
		*OutDecodedAllocationBytes = DecodedAllocationBytes;
	return true;
}

bool FSeinReplayWireCodec::Decode(
	TConstArrayView<uint8> Body,
	FSeinStructWireCatalogView HeaderCatalog,
	FSeinCommandWireSchemaLookup FindSchema,
	FSeinReplay& OutReplay,
	FString& OutError,
	uint64* OutDecodedAllocationBytes)
{
	if (OutDecodedAllocationBytes) *OutDecodedAllocationBytes = 0;
	if (Body.IsEmpty() || static_cast<uint64>(Body.Num()) > SeinReplayFormat::MaxBodyBytes)
	{
		OutError = TEXT("replay body is empty or exceeds its hard byte cap");
		return false;
	}
	FReader Reader(Body, OutError);
	uint32 Magic = 0;
	uint16 Version = 0;
	FSeinReplay Candidate;
	uint64 DecodedAllocationBytes = 0;
	if (!Reader.U32(Magic) || Magic != BodyMagic
		|| !Reader.U16(Version) || Version != BodyVersion
		|| !DecodeHeader(
			Reader, HeaderCatalog, Candidate.Header,
			DecodedAllocationBytes, OutError))
	{
		if (OutError.IsEmpty()) OutError = TEXT("invalid replay body prefix/header");
		return false;
	}

	uint32 TurnCount = 0;
	if (!Reader.U32(TurnCount)
		|| TurnCount > MaxReplayTurns
		|| static_cast<uint64>(TurnCount) * 8u > static_cast<uint64>(Reader.Remaining()))
	{
		OutError = TEXT("replay turn count exceeds its cap or remaining-byte bound");
		return false;
	}
	if (!ChargeDecodedAllocation(
		static_cast<uint64>(TurnCount) * sizeof(FSeinReplayTurnRecord),
		DecodedAllocationBytes, OutError)) return false;
	Candidate.Turns.Reserve(static_cast<int32>(TurnCount));
	uint64 TotalCommands = 0;
	for (uint32 TurnIndex = 0; TurnIndex < TurnCount; ++TurnIndex)
	{
		FSeinReplayTurnRecord& Turn = Candidate.Turns.AddDefaulted_GetRef();
		uint32 CommandCount = 0;
		if (!Reader.I32(Turn.TurnId)
			|| !Reader.U32(CommandCount)
			|| CommandCount > SeinReplayFormat::MaxCommandsPerTurn
			|| static_cast<uint64>(CommandCount) * 4u > static_cast<uint64>(Reader.Remaining()))
		{
			OutError = TEXT("replay command count exceeds its cap or remaining-byte bound");
			return false;
		}
		TotalCommands += CommandCount;
		if (TotalCommands > static_cast<uint64>(SeinReplayFormat::MaxBodyBytes / 4u))
		{
			OutError = TEXT("replay aggregate command count exceeds its body-derived cap");
			return false;
		}
		if (!ChargeDecodedAllocation(
			static_cast<uint64>(CommandCount) * sizeof(FSeinCommand),
			DecodedAllocationBytes, OutError)) return false;
		Turn.Commands.Reserve(static_cast<int32>(CommandCount));
		for (uint32 CommandIndex = 0; CommandIndex < CommandCount; ++CommandIndex)
		{
			uint32 CommandBytes = 0;
			TConstArrayView<uint8> CommandView;
			FSeinCommand Command;
			uint64 CommandAllocationBytes = 0;
			if (!Reader.U32(CommandBytes)
				|| CommandBytes > static_cast<uint32>(FSeinCommandWireCodec::MaxWireCommandBytes)
				|| CommandBytes > static_cast<uint32>(Reader.Remaining())
				|| !Reader.Slice(static_cast<int32>(CommandBytes), CommandView)
				|| !FSeinCommandWireCodec::Decode(
					CommandView, FindSchema, Command, OutError,
					static_cast<int32>(FMath::Min<uint64>(
						FSeinReplayWireCodec::MaxDecodedAllocationBytes
							- DecodedAllocationBytes,
						static_cast<uint64>(MAX_int32))),
					&CommandAllocationBytes)
				|| !ChargeDecodedAllocation(
					CommandAllocationBytes, DecodedAllocationBytes, OutError))
				return false;
			Turn.Commands.Add(MoveTemp(Command));
		}
	}
	if (!Reader.AtEnd())
	{
		OutError = TEXT("trailing bytes after replay body");
		return false;
	}
	OutReplay = MoveTemp(Candidate);
	if (OutDecodedAllocationBytes)
		*OutDecodedAllocationBytes = DecodedAllocationBytes;
	return true;
}

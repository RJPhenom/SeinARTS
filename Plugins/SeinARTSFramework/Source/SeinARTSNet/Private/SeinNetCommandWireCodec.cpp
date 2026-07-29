/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file SeinNetCommandWireCodec.cpp
 */

#include "SeinNetCommandWireCodec.h"

namespace
{
	constexpr uint32 BatchMagic = 0x53424154u; // SBAT
	constexpr uint16 BatchVersion = 1;
	constexpr uint8 DraftMode = 1;
	constexpr uint8 CanonicalMode = 2;

	bool ChargeNativeAllocation(
		uint64 Amount, uint64 Limit, uint64& Used, FString& Error)
	{
		if (Limit > FSeinNetCommandWireCodec::MaxNativeAllocationBytes
			|| Amount > Limit || Used > Limit - Amount)
		{
			if (Error.IsEmpty()) Error = TEXT("opaque command batch exceeds its native-allocation budget");
			return false;
		}
		Used += Amount;
		return true;
	}

	bool AddCanonicalSurcharge(
		uint64 Amount, uint64& Used, FString& Error)
	{
		if (Amount > MAX_uint64 - Used)
		{
			if (Error.IsEmpty()) Error = TEXT("opaque command batch canonical-cost overflow");
			return false;
		}
		Used += Amount;
		return true;
	}

	bool FinalizeBatchCost(
		const FSeinOpaqueCommandBatch& Batch,
		int32 CommandCount,
		uint64 NestedCanonicalSurchargeBytes,
		uint64 NativeAllocationBytes,
		FSeinWireCost& OutCost,
		FString& Error)
	{
		OutCost = {};
		if (CommandCount < 0)
		{
			Error = TEXT("opaque command batch has an invalid canonical command count");
			return false;
		}
		uint64 CanonicalCostBytes = static_cast<uint64>(Batch.Bytes.Num());
		const uint64 CommandElementCost = static_cast<uint64>(CommandCount)
			* FSeinWireCost::CanonicalBytesPerLogicalElement;
		if (!AddCanonicalSurcharge(CommandElementCost, CanonicalCostBytes, Error)
			|| !AddCanonicalSurcharge(
				NestedCanonicalSurchargeBytes, CanonicalCostBytes, Error)
			|| CanonicalCostBytes > FSeinNetCommandWireCodec::MaxCanonicalCostBytes)
		{
			if (Error.IsEmpty()) Error = TEXT("opaque command batch exceeds its canonical-cost budget");
			return false;
		}
		OutCost.CanonicalCostBytes = CanonicalCostBytes;
		OutCost.NativeAllocationBytes = NativeAllocationBytes;
		return true;
	}

	void WriteUInt(TArray<uint8>& Bytes, uint64 Value, int32 Width)
	{
		for (int32 Index = 0; Index < Width; ++Index)
		{
			Bytes.Add(static_cast<uint8>(Value >> ((Width - 1 - Index) * 8)));
		}
	}

	bool ReadUInt(
		TConstArrayView<uint8> Bytes,
		int32& Offset,
		int32 Width,
		uint64& Out,
		FString& Error)
	{
		if (Width < 0 || Offset < 0 || Width > Bytes.Num() - Offset)
		{
			Error = TEXT("truncated opaque command batch");
			return false;
		}
		Out = 0;
		for (int32 Index = 0; Index < Width; ++Index)
		{
			Out = (Out << 8) | Bytes[Offset++];
		}
		return true;
	}

	bool BeginEncode(
		int32 Count,
		int32 MaxCommands,
		uint8 Mode,
		FSeinOpaqueCommandBatch& Out,
		FString& Error)
	{
		Out.Bytes.Reset();
		Error.Reset();
		if (Count < 0 || MaxCommands < 0 || Count > MaxCommands)
		{
			Error = TEXT("opaque command batch count exceeds its configured cap");
			return false;
		}
		Out.Bytes.Reserve(11);
		WriteUInt(Out.Bytes, BatchMagic, 4);
		WriteUInt(Out.Bytes, BatchVersion, 2);
		WriteUInt(Out.Bytes, Mode, 1);
		WriteUInt(Out.Bytes, static_cast<uint32>(Count), 4);
		return true;
	}

	bool AppendCommand(
		const FSeinCommand& Command,
		bool bAdminRequest,
		FSeinCommandWireSchemaLookup FindSchema,
		uint64 NativeAllocationLimit,
		uint64& NativeAllocationBytes,
		uint64& CanonicalSurchargeBytes,
		FSeinOpaqueCommandBatch& Out,
		FString& Error)
	{
		FSeinCommandSchemaDescriptor Schema;
		if (!FindSchema(Command.CommandType, Command.SchemaVersion, Schema))
		{
			Error = TEXT("cannot encode a command absent from the frozen schema snapshot");
			return false;
		}
		TArray<uint8> Encoded;
		FSeinWireCost CommandCost;
		if (!FSeinCommandWireCodec::EncodeWithCost(
			Command, Schema, Encoded, Error, CommandCost)
			|| CommandCost.CanonicalCostBytes < static_cast<uint64>(Encoded.Num())
			|| !ChargeNativeAllocation(
				CommandCost.NativeAllocationBytes, NativeAllocationLimit,
				NativeAllocationBytes, Error)
			|| !AddCanonicalSurcharge(
				CommandCost.CanonicalCostBytes - static_cast<uint64>(Encoded.Num()),
				CanonicalSurchargeBytes, Error)) return false;
		const int64 Required = static_cast<int64>(Out.Bytes.Num()) + 1 + 4 + Encoded.Num();
		if (Required > FSeinOpaqueCommandBatch::MaxBytes)
		{
			Error = TEXT("opaque command batch exceeds its hard byte cap");
			return false;
		}
		WriteUInt(Out.Bytes, bAdminRequest ? 1u : 0u, 1);
		WriteUInt(Out.Bytes, static_cast<uint32>(Encoded.Num()), 4);
		Out.Bytes.Append(Encoded);
		return true;
	}

	bool BeginDecode(
		const FSeinOpaqueCommandBatch& Batch,
		int32 MaxCommands,
		uint8 ExpectedMode,
		int32& Offset,
		uint32& Count,
		FString& Error)
	{
		Error.Reset();
		Offset = 0;
		Count = 0;
		if (Batch.Bytes.Num() > static_cast<int32>(FSeinOpaqueCommandBatch::MaxBytes))
		{
			Error = TEXT("opaque command batch exceeds its hard byte cap");
			return false;
		}
		uint64 Magic = 0, Version = 0, Mode = 0, EncodedCount = 0;
		const TConstArrayView<uint8> Bytes(Batch.Bytes);
		if (!ReadUInt(Bytes, Offset, 4, Magic, Error)
			|| !ReadUInt(Bytes, Offset, 2, Version, Error)
			|| !ReadUInt(Bytes, Offset, 1, Mode, Error)
			|| !ReadUInt(Bytes, Offset, 4, EncodedCount, Error)
			|| Magic != BatchMagic || Version != BatchVersion || Mode != ExpectedMode)
		{
			if (Error.IsEmpty()) Error = TEXT("invalid opaque command batch prefix");
			return false;
		}
		if (MaxCommands < 0 || EncodedCount > static_cast<uint64>(MaxCommands)
			|| EncodedCount > static_cast<uint64>(MAX_int32)
			|| EncodedCount * 5u > static_cast<uint64>(Bytes.Num() - Offset))
		{
			Error = TEXT("opaque command batch count exceeds its configured or remaining-byte bound");
			return false;
		}
		Count = static_cast<uint32>(EncodedCount);
		return true;
	}

	bool ReadCommand(
		const FSeinOpaqueCommandBatch& Batch,
		int32& Offset,
		FSeinCommandWireSchemaLookup FindSchema,
		FSeinCommand& OutCommand,
		bool& bOutAdmin,
		uint64 RemainingNativeAllocationBytes,
		FSeinWireCost& OutCost,
		uint64& OutCanonicalSurchargeBytes,
		FString& Error)
	{
		OutCost = {};
		OutCanonicalSurchargeBytes = 0;
		const TConstArrayView<uint8> Bytes(Batch.Bytes);
		uint64 Admin = 0, CommandBytes = 0;
		if (!ReadUInt(Bytes, Offset, 1, Admin, Error)
			|| !ReadUInt(Bytes, Offset, 4, CommandBytes, Error)
			|| Admin > 1 || CommandBytes > static_cast<uint64>(MAX_int32)
			|| CommandBytes > static_cast<uint64>(Bytes.Num() - Offset))
		{
			if (Error.IsEmpty()) Error = TEXT("invalid opaque command frame");
			return false;
		}
		const TConstArrayView<uint8> CommandView(
			Bytes.GetData() + Offset, static_cast<int32>(CommandBytes));
		Offset += static_cast<int32>(CommandBytes);
		bOutAdmin = Admin != 0;
		if (!FSeinCommandWireCodec::DecodeWithCost(
			CommandView, FindSchema, OutCommand, Error,
			static_cast<int32>(FMath::Min<uint64>(
				RemainingNativeAllocationBytes, static_cast<uint64>(MAX_int32))),
			OutCost)
			|| OutCost.CanonicalCostBytes < CommandBytes)
		{
			if (Error.IsEmpty()) Error = TEXT("opaque command canonical cost is smaller than its frame");
			return false;
		}
		OutCanonicalSurchargeBytes = OutCost.CanonicalCostBytes - CommandBytes;
		return true;
	}
}

bool FSeinNetCommandWireCodec::EncodeDraftsWithCost(
	TConstArrayView<FSeinCommandSubmissionDraft> Drafts,
	int32 MaxCommands,
	FSeinCommandWireSchemaLookup FindSchema,
	FSeinOpaqueCommandBatch& OutBatch,
	FString& OutError,
	FSeinWireCost& OutCost)
{
	OutCost = {};
	OutBatch.Bytes.Reset();
	OutError.Reset();
	uint64 NativeAllocationBytes = 0;
	uint64 CanonicalSurchargeBytes = 0;
	if (!ChargeNativeAllocation(
		static_cast<uint64>(Drafts.Num()) * sizeof(FSeinCommandSubmissionDraft),
		FSeinNetCommandWireCodec::MaxNativeAllocationBytes,
		NativeAllocationBytes, OutError)) return false;
	if (!BeginEncode(Drafts.Num(), MaxCommands, DraftMode, OutBatch, OutError)) return false;
	for (const FSeinCommandSubmissionDraft& Draft : Drafts)
	{
		if (!AppendCommand(
			Draft.Command, Draft.bRequestMatchAdministration,
			FindSchema, FSeinNetCommandWireCodec::MaxNativeAllocationBytes,
			NativeAllocationBytes, CanonicalSurchargeBytes,
			OutBatch, OutError))
		{
			OutBatch.Bytes.Reset();
			return false;
		}
	}
	if (!FinalizeBatchCost(
		OutBatch, Drafts.Num(), CanonicalSurchargeBytes,
		NativeAllocationBytes, OutCost, OutError))
	{
		OutBatch.Bytes.Reset();
		return false;
	}
	return true;
}

bool FSeinNetCommandWireCodec::EncodeDrafts(
	TConstArrayView<FSeinCommandSubmissionDraft> Drafts,
	int32 MaxCommands,
	FSeinCommandWireSchemaLookup FindSchema,
	FSeinOpaqueCommandBatch& OutBatch,
	FString& OutError,
	uint64* OutDecodedAllocationBytes)
{
	if (OutDecodedAllocationBytes) *OutDecodedAllocationBytes = 0;
	FSeinWireCost Cost;
	if (!EncodeDraftsWithCost(
		Drafts, MaxCommands, FindSchema, OutBatch, OutError, Cost)) return false;
	if (OutDecodedAllocationBytes)
		*OutDecodedAllocationBytes = Cost.NativeAllocationBytes;
	return true;
}

bool FSeinNetCommandWireCodec::DecodeDraftsWithCost(
	const FSeinOpaqueCommandBatch& Batch,
	int32 MaxCommands,
	FSeinCommandWireSchemaLookup FindSchema,
	TArray<FSeinCommandSubmissionDraft>& OutDrafts,
	FString& OutError,
	FSeinWireCost& OutCost,
	uint64 NativeAllocationLimit)
{
	OutCost = {};
	int32 Offset = 0;
	uint32 Count = 0;
	if (!BeginDecode(Batch, MaxCommands, DraftMode, Offset, Count, OutError)) return false;
	uint64 NativeAllocationBytes = 0;
	uint64 CanonicalSurchargeBytes = 0;
	if (!ChargeNativeAllocation(
		static_cast<uint64>(Count) * sizeof(FSeinCommandSubmissionDraft),
		NativeAllocationLimit, NativeAllocationBytes, OutError)) return false;
	TArray<FSeinCommandSubmissionDraft> Candidate;
	Candidate.Reserve(static_cast<int32>(Count));
	for (uint32 Index = 0; Index < Count; ++Index)
	{
		FSeinCommand Command;
		bool bAdmin = false;
		FSeinWireCost CommandCost;
		uint64 CommandCanonicalSurchargeBytes = 0;
		if (!ReadCommand(
			Batch, Offset, FindSchema, Command, bAdmin,
			NativeAllocationLimit - NativeAllocationBytes,
			CommandCost, CommandCanonicalSurchargeBytes, OutError)
			|| !ChargeNativeAllocation(
				CommandCost.NativeAllocationBytes, NativeAllocationLimit,
				NativeAllocationBytes, OutError)
			|| !AddCanonicalSurcharge(
				CommandCanonicalSurchargeBytes,
				CanonicalSurchargeBytes, OutError)) return false;
		FSeinCommandSubmissionDraft& Draft = Candidate.AddDefaulted_GetRef();
		Draft.Command = MoveTemp(Command);
		Draft.bRequestMatchAdministration = bAdmin;
	}
	if (Offset != Batch.Bytes.Num())
	{
		OutError = TEXT("trailing bytes after opaque command batch");
		return false;
	}
	if (!FinalizeBatchCost(
		Batch, static_cast<int32>(Count), CanonicalSurchargeBytes,
		NativeAllocationBytes, OutCost, OutError)) return false;
	OutDrafts = MoveTemp(Candidate);
	return true;
}

bool FSeinNetCommandWireCodec::DecodeDrafts(
	const FSeinOpaqueCommandBatch& Batch,
	int32 MaxCommands,
	FSeinCommandWireSchemaLookup FindSchema,
	TArray<FSeinCommandSubmissionDraft>& OutDrafts,
	FString& OutError,
	uint64* OutDecodedAllocationBytes,
	uint64 DecodedAllocationLimit)
{
	if (OutDecodedAllocationBytes) *OutDecodedAllocationBytes = 0;
	FSeinWireCost Cost;
	if (!DecodeDraftsWithCost(
		Batch, MaxCommands, FindSchema, OutDrafts, OutError,
		Cost, DecodedAllocationLimit)) return false;
	if (OutDecodedAllocationBytes)
		*OutDecodedAllocationBytes = Cost.NativeAllocationBytes;
	return true;
}

bool FSeinNetCommandWireCodec::EncodeCommandsWithCost(
	TConstArrayView<FSeinCommand> Commands,
	int32 MaxCommands,
	FSeinCommandWireSchemaLookup FindSchema,
	FSeinOpaqueCommandBatch& OutBatch,
	FString& OutError,
	FSeinWireCost& OutCost)
{
	OutCost = {};
	OutBatch.Bytes.Reset();
	OutError.Reset();
	uint64 NativeAllocationBytes = 0;
	uint64 CanonicalSurchargeBytes = 0;
	if (!ChargeNativeAllocation(
		static_cast<uint64>(Commands.Num()) * sizeof(FSeinCommand),
		FSeinNetCommandWireCodec::MaxNativeAllocationBytes,
		NativeAllocationBytes, OutError)) return false;
	if (!BeginEncode(Commands.Num(), MaxCommands, CanonicalMode, OutBatch, OutError)) return false;
	for (const FSeinCommand& Command : Commands)
	{
		if (!AppendCommand(
			Command, false, FindSchema,
			FSeinNetCommandWireCodec::MaxNativeAllocationBytes,
			NativeAllocationBytes, CanonicalSurchargeBytes,
			OutBatch, OutError))
		{
			OutBatch.Bytes.Reset();
			return false;
		}
	}
	if (!FinalizeBatchCost(
		OutBatch, Commands.Num(), CanonicalSurchargeBytes,
		NativeAllocationBytes, OutCost, OutError))
	{
		OutBatch.Bytes.Reset();
		return false;
	}
	return true;
}

bool FSeinNetCommandWireCodec::EncodeCommands(
	TConstArrayView<FSeinCommand> Commands,
	int32 MaxCommands,
	FSeinCommandWireSchemaLookup FindSchema,
	FSeinOpaqueCommandBatch& OutBatch,
	FString& OutError,
	uint64* OutDecodedAllocationBytes)
{
	if (OutDecodedAllocationBytes) *OutDecodedAllocationBytes = 0;
	FSeinWireCost Cost;
	if (!EncodeCommandsWithCost(
		Commands, MaxCommands, FindSchema, OutBatch, OutError, Cost)) return false;
	if (OutDecodedAllocationBytes)
		*OutDecodedAllocationBytes = Cost.NativeAllocationBytes;
	return true;
}

bool FSeinNetCommandWireCodec::DecodeCommandsWithCost(
	const FSeinOpaqueCommandBatch& Batch,
	int32 MaxCommands,
	FSeinCommandWireSchemaLookup FindSchema,
	TArray<FSeinCommand>& OutCommands,
	FString& OutError,
	FSeinWireCost& OutCost,
	uint64 NativeAllocationLimit)
{
	OutCost = {};
	int32 Offset = 0;
	uint32 Count = 0;
	if (!BeginDecode(Batch, MaxCommands, CanonicalMode, Offset, Count, OutError)) return false;
	uint64 NativeAllocationBytes = 0;
	uint64 CanonicalSurchargeBytes = 0;
	if (!ChargeNativeAllocation(
		static_cast<uint64>(Count) * sizeof(FSeinCommand),
		NativeAllocationLimit,
		NativeAllocationBytes, OutError)) return false;
	TArray<FSeinCommand> Candidate;
	Candidate.Reserve(static_cast<int32>(Count));
	for (uint32 Index = 0; Index < Count; ++Index)
	{
		FSeinCommand Command;
		bool bAdmin = false;
		FSeinWireCost CommandCost;
		uint64 CommandCanonicalSurchargeBytes = 0;
		if (!ReadCommand(
			Batch, Offset, FindSchema, Command, bAdmin,
			NativeAllocationLimit - NativeAllocationBytes,
			CommandCost, CommandCanonicalSurchargeBytes, OutError)
			|| !ChargeNativeAllocation(
				CommandCost.NativeAllocationBytes,
				NativeAllocationLimit,
				NativeAllocationBytes, OutError)
			|| !AddCanonicalSurcharge(
				CommandCanonicalSurchargeBytes,
				CanonicalSurchargeBytes, OutError)
			|| bAdmin)
		{
			if (OutError.IsEmpty()) OutError = TEXT("canonical command batch carried a draft-only administration flag");
			return false;
		}
		Candidate.Add(MoveTemp(Command));
	}
	if (Offset != Batch.Bytes.Num())
	{
		OutError = TEXT("trailing bytes after opaque command batch");
		return false;
	}
	if (!FinalizeBatchCost(
		Batch, static_cast<int32>(Count), CanonicalSurchargeBytes,
		NativeAllocationBytes, OutCost, OutError)) return false;
	OutCommands = MoveTemp(Candidate);
	return true;
}

bool FSeinNetCommandWireCodec::DecodeCommands(
	const FSeinOpaqueCommandBatch& Batch,
	int32 MaxCommands,
	FSeinCommandWireSchemaLookup FindSchema,
	TArray<FSeinCommand>& OutCommands,
	FString& OutError,
	uint64* OutDecodedAllocationBytes)
{
	if (OutDecodedAllocationBytes) *OutDecodedAllocationBytes = 0;
	FSeinWireCost Cost;
	if (!DecodeCommandsWithCost(
		Batch, MaxCommands, FindSchema, OutCommands, OutError,
		Cost, MaxNativeAllocationBytes)) return false;
	if (OutDecodedAllocationBytes)
		*OutDecodedAllocationBytes = Cost.NativeAllocationBytes;
	return true;
}

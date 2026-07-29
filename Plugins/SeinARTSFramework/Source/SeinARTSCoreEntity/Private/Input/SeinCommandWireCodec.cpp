/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandWireCodec.cpp
 */

#include "Input/SeinCommandWireCodec.h"

#include "Serialization/SeinCanonicalWirePrimitives.h"

using namespace UE::Sein::CanonicalWirePrivate;

namespace
{
	constexpr uint32 CommandMagic = 0x53434D44u; // SCMD
	constexpr uint16 CommandWireVersion = 3;
	constexpr int32 MaxIdentifierBytes = 1024;

	bool WriteVector(FWireWriter& Writer, const FFixedVector& Vector)
	{
		return Writer.I64(Vector.X.Value) && Writer.I64(Vector.Y.Value) && Writer.I64(Vector.Z.Value);
	}

	bool ReadVector(FWireReader& Reader, FFixedVector& Vector)
	{
		return Reader.I64(Vector.X.Value) && Reader.I64(Vector.Y.Value) && Reader.I64(Vector.Z.Value);
	}

	int32 PayloadWireByteLimit(const FSeinCommandSchemaDescriptor& Schema)
	{
		const int64 FramingAllowance =
			static_cast<int64>(FMath::Max(0, Schema.MaxPayloadAggregateElements))
			* static_cast<int64>(ArrayElementFrameBytes);
		const int64 Limit = static_cast<int64>(FMath::Max(0, Schema.MaxPayloadBytes))
			+ FramingAllowance;
		return static_cast<int32>(FMath::Min<int64>(Limit, FSeinCommandWireCodec::MaxWireCommandBytes));
	}
}

bool FSeinCommandWireCodec::EncodeWithCost(
	const FSeinCommand& Command,
	const FSeinCommandSchemaDescriptor& Schema,
	TArray<uint8>& OutBytes,
	FString& OutError,
	FSeinWireCost& OutCost)
{
	OutCost = {};
	if (!Command.CommandType.IsValid() || Command.SchemaVersion <= 0
		|| Schema.CommandType != Command.CommandType
		|| Schema.SchemaVersion != Command.SchemaVersion)
	{
		OutBytes.Reset();
		OutError = TEXT("command does not match the supplied frozen schema");
		return false;
	}
	if (SeinValidateCommandAgainstSchema(Command, Schema)
		!= ESeinCommandStructureResult::Valid)
	{
		OutBytes.Reset();
		OutError = TEXT("command failed frozen-schema structural validation");
		return false;
	}
	if (Command.EntityList.Num() > Schema.MaxEntityListEntries
		|| Command.TargeterPoints.Num() > Schema.MaxTargeterPoints)
	{
		OutBytes.Reset();
		OutError = TEXT("command common-envelope array exceeds its schema cap");
		return false;
	}
	if ((Schema.PayloadStruct == nullptr) != !Command.Payload.IsValid()
		|| (Schema.PayloadStruct && Command.Payload.GetScriptStruct() != Schema.PayloadStruct))
	{
		OutBytes.Reset();
		OutError = TEXT("command payload does not have the schema's exact top-level type");
		return false;
	}

	uint64 NativeAllocationBytes =
		static_cast<uint64>(Command.TargeterPoints.Num()) * sizeof(FSeinTargeterPoint)
		+ static_cast<uint64>(Command.EntityList.Num()) * sizeof(FSeinEntityHandle);
	auto ChargeNativeAllocation = [&](uint64 Amount)
	{
		const uint64 Max = static_cast<uint64>(MaxWireCommandBytes);
		if (Amount > Max || NativeAllocationBytes > Max - Amount)
		{
			OutError = TEXT("command exceeds its native-allocation budget");
			return false;
		}
		NativeAllocationBytes += Amount;
		return true;
	};
	const FString CommandTypeText = Command.CommandType.ToString();
	if (!ChargeNativeAllocation(NativeDecodedStringBytes(CommandTypeText))
		|| (Command.AbilityTag.IsValid()
			&& !ChargeNativeAllocation(
				NativeDecodedStringBytes(Command.AbilityTag.ToString()))))
	{
		OutBytes.Reset();
		return false;
	}

	TArray<uint8> PayloadBytes;
	uint64 PayloadCanonicalSurchargeBytes = 0;
	if (Schema.PayloadStruct)
	{
		const int32 PayloadNativeLimit = static_cast<int32>(
			(static_cast<uint64>(MaxWireCommandBytes) - NativeAllocationBytes) / 2u);
		const FSeinStructWireLimits PayloadLimits{
			PayloadWireByteLimit(Schema),
			Schema.MaxPayloadAggregateElements,
			MaxIdentifierBytes,
			64,
			PayloadNativeLimit };
		FSeinWireCost PayloadCost;
		if (!FSeinCanonicalStateCodec::EncodeWithCost(
			Schema.PayloadStruct, Command.Payload.GetMemory(),
			{ Schema.DynamicPayloadStructs, Schema.AllowedPayloadNames },
			PayloadLimits, PayloadBytes, OutError,
			PayloadCost)
			|| PayloadCost.NativeAllocationBytes > static_cast<uint64>(PayloadNativeLimit)
			|| PayloadCost.CanonicalCostBytes < static_cast<uint64>(PayloadBytes.Num()))
		{
			OutBytes.Reset();
			if (OutError.IsEmpty()) OutError = TEXT("command payload exceeds its wire-cost or native-allocation budget");
			return false;
		}
		// Decode holds its reflected scratch value while CopyScriptStruct builds
		// the destination. Charge both live copies to bound that transient peak.
		NativeAllocationBytes += PayloadCost.NativeAllocationBytes * 2u;
		PayloadCanonicalSurchargeBytes = PayloadCost.CanonicalCostBytes
			- static_cast<uint64>(PayloadBytes.Num());
	}

	FWireWriter Writer(OutBytes, MaxWireCommandBytes, OutError, true);
	if (!Writer.U32(CommandMagic)
		|| !Writer.U16(CommandWireVersion)
		|| !Writer.Utf8(CommandTypeText, MaxIdentifierBytes)
		|| !Writer.I32(Command.SchemaVersion)
		|| !Writer.U8(Command.PlayerID.Value)
		|| !Writer.U8(static_cast<uint8>(Command.IssuerKind))
		|| !Writer.U8(Command.DerivedResourcePayer.Value)
		|| !WriteEntity(Writer, Command.EntityHandle)
		|| !WriteTag(Writer, Command.AbilityTag, MaxIdentifierBytes)
		|| !WriteEntity(Writer, Command.TargetEntity)
		|| !WriteVector(Writer, Command.TargetLocation)
		|| !Writer.I32(Command.Tick)
		|| !Writer.I32(Command.QueueIndex)
		|| !Writer.U8(Command.bQueueCommand ? 1 : 0)
		|| !WriteVector(Writer, Command.AuxLocation)
		|| !Writer.U32(static_cast<uint32>(Command.TargeterPoints.Num())))
	{
		OutBytes.Reset();
		return false;
	}
	for (const FSeinTargeterPoint& Point : Command.TargeterPoints)
	{
		if (!WriteVector(Writer, Point.Location)
			|| !WriteVector(Writer, Point.AuxLocation)
			|| !Writer.U8(Point.RotationStep)
			|| !Writer.I64(Point.YawDegrees.Value))
		{
			OutBytes.Reset();
			return false;
		}
	}
	if (!Writer.I64(Command.AuxA.Value)
		|| !Writer.I64(Command.AuxB.Value)
		|| !Writer.U32(static_cast<uint32>(Command.EntityList.Num())))
	{
		OutBytes.Reset();
		return false;
	}
	for (const FSeinEntityHandle& Entity : Command.EntityList)
	{
		if (!WriteEntity(Writer, Entity))
		{
			OutBytes.Reset();
			return false;
		}
	}
	if (!Writer.I32(Command.ActiveFocusIndex)
		|| !Writer.U8(Schema.PayloadStruct ? 1 : 0)
		|| !Writer.U32(static_cast<uint32>(PayloadBytes.Num()))
		|| !Writer.Raw(PayloadBytes.GetData(), PayloadBytes.Num()))
	{
		OutBytes.Reset();
		return false;
	}
	const uint64 CommonLogicalElements =
		static_cast<uint64>(Command.TargeterPoints.Num())
		+ static_cast<uint64>(Command.EntityList.Num());
	if (!BuildCanonicalCost(
		OutBytes.Num(), CommonLogicalElements,
		OutCost.CanonicalCostBytes, OutError)
		|| PayloadCanonicalSurchargeBytes
			> MAX_uint64 - OutCost.CanonicalCostBytes)
	{
		OutBytes.Reset();
		OutCost = {};
		if (OutError.IsEmpty()) OutError = TEXT("command canonical-cost overflow");
		return false;
	}
	OutCost.CanonicalCostBytes += PayloadCanonicalSurchargeBytes;
	OutCost.NativeAllocationBytes = NativeAllocationBytes;
	return true;
}

bool FSeinCommandWireCodec::Encode(
	const FSeinCommand& Command,
	const FSeinCommandSchemaDescriptor& Schema,
	TArray<uint8>& OutBytes,
	FString& OutError,
	uint64* OutDecodedAllocationBytes)
{
	if (OutDecodedAllocationBytes) *OutDecodedAllocationBytes = 0;
	FSeinWireCost Cost;
	if (!EncodeWithCost(Command, Schema, OutBytes, OutError, Cost)) return false;
	if (OutDecodedAllocationBytes)
		*OutDecodedAllocationBytes = Cost.NativeAllocationBytes;
	return true;
}

bool FSeinCommandWireCodec::DecodeWithCost(
	TConstArrayView<uint8> Bytes,
	FSeinCommandWireSchemaLookup FindSchema,
	FSeinCommand& OutCommand,
	FString& OutError,
	int32 MaxNativeAllocationBytes,
	FSeinWireCost& OutCost)
{
	OutCost = {};
	if (Bytes.Num() < 0 || Bytes.Num() > MaxWireCommandBytes
		|| MaxNativeAllocationBytes < 0)
	{
		OutError = TEXT("opaque command exceeds the hard wire byte cap");
		return false;
	}

	uint64 NativeAllocationBytes = 0;
	auto ChargeAllocation = [&](uint64 Amount)
	{
		const uint64 Max = static_cast<uint64>(MaxNativeAllocationBytes);
		if (Amount > Max || NativeAllocationBytes > Max - Amount)
		{
			OutError = TEXT("opaque command exceeds its native-allocation budget");
			return false;
		}
		NativeAllocationBytes += Amount;
		return true;
	};
	FWireReader Reader(Bytes, OutError, true);
	uint32 Magic = 0;
	uint16 Version = 0;
	FString CommandTypeText;
	int32 SchemaVersion = 0;
	if (!Reader.U32(Magic) || Magic != CommandMagic
		|| !Reader.U16(Version) || Version != CommandWireVersion
		|| !Reader.Utf8(CommandTypeText, MaxIdentifierBytes, ChargeAllocation)
		|| !Reader.I32(SchemaVersion) || SchemaVersion <= 0)
	{
		if (OutError.IsEmpty()) OutError = TEXT("invalid opaque command prefix");
		return false;
	}
	FGameplayTag CommandType;
	if (!ResolveExistingTag(CommandTypeText, CommandType, OutError)) return false;
	FSeinCommandSchemaDescriptor Schema;
	if (!FindSchema(CommandType, SchemaVersion, Schema)
		|| Schema.CommandType != CommandType || Schema.SchemaVersion != SchemaVersion)
	{
		OutError = TEXT("opaque command key is absent from the world's frozen schema snapshot");
		return false;
	}

	FSeinCommand Candidate;
	Candidate.CommandType = Schema.CommandType;
	Candidate.SchemaVersion = Schema.SchemaVersion;
	uint8 Issuer = 0;
	uint8 Queue = 0;
	if (!Reader.U8(Candidate.PlayerID.Value)
		|| !Reader.U8(Issuer)
		|| Issuer > static_cast<uint8>(ESeinCommandIssuerKind::DeterministicSystem)
		|| !Reader.U8(Candidate.DerivedResourcePayer.Value)
		|| !ReadEntity(Reader, Candidate.EntityHandle)
		|| !ReadTag(
			Reader, Candidate.AbilityTag, MaxIdentifierBytes,
			ChargeAllocation, OutError)
		|| !ReadEntity(Reader, Candidate.TargetEntity)
		|| !ReadVector(Reader, Candidate.TargetLocation)
		|| !Reader.I32(Candidate.Tick)
		|| !Reader.I32(Candidate.QueueIndex)
		|| !Reader.U8(Queue) || Queue > 1
		|| !ReadVector(Reader, Candidate.AuxLocation))
	{
		if (OutError.IsEmpty()) OutError = TEXT("invalid opaque command common envelope");
		return false;
	}
	Candidate.IssuerKind = static_cast<ESeinCommandIssuerKind>(Issuer);
	Candidate.bQueueCommand = Queue != 0;
	uint32 TargeterCount = 0;
	if (!Reader.U32(TargeterCount)
		|| TargeterCount > static_cast<uint32>(FMath::Max(0, Schema.MaxTargeterPoints))
		|| static_cast<uint64>(TargeterCount) * 57u > static_cast<uint64>(Reader.Remaining())
		|| !ChargeAllocation(static_cast<uint64>(TargeterCount) * sizeof(FSeinTargeterPoint)))
	{
		if (OutError.IsEmpty())
			OutError = TEXT("opaque command targeter count exceeds its schema or remaining-byte bound");
		return false;
	}
	Candidate.TargeterPoints.SetNum(static_cast<int32>(TargeterCount));
	for (FSeinTargeterPoint& Point : Candidate.TargeterPoints)
	{
		if (!ReadVector(Reader, Point.Location)
			|| !ReadVector(Reader, Point.AuxLocation)
			|| !Reader.U8(Point.RotationStep)
			|| !Reader.I64(Point.YawDegrees.Value)) return false;
	}

	uint32 EntityCount = 0;
	if (!Reader.I64(Candidate.AuxA.Value)
		|| !Reader.I64(Candidate.AuxB.Value)
		|| !Reader.U32(EntityCount)
		|| EntityCount > static_cast<uint32>(FMath::Max(0, Schema.MaxEntityListEntries))
		|| static_cast<uint64>(EntityCount) * 8u > static_cast<uint64>(Reader.Remaining())
		|| !ChargeAllocation(static_cast<uint64>(EntityCount) * sizeof(FSeinEntityHandle)))
	{
		if (OutError.IsEmpty())
			OutError = TEXT("opaque command entity-list count exceeds its schema or remaining-byte bound");
		return false;
	}
	Candidate.EntityList.SetNum(static_cast<int32>(EntityCount));
	for (FSeinEntityHandle& Entity : Candidate.EntityList)
	{
		if (!ReadEntity(Reader, Entity)) return false;
	}

	uint8 bHasPayload = 0;
	uint32 PayloadBytes = 0;
	if (!Reader.I32(Candidate.ActiveFocusIndex)
		|| !Reader.U8(bHasPayload) || bHasPayload > 1
		|| !Reader.U32(PayloadBytes)) return false;
	if ((Schema.PayloadStruct != nullptr) != (bHasPayload != 0)
		|| PayloadBytes > static_cast<uint32>(PayloadWireByteLimit(Schema)))
	{
		OutError = TEXT("opaque command payload presence or byte length disagrees with its schema");
		return false;
	}
	TConstArrayView<uint8> PayloadView;
	if (!Reader.Slice(static_cast<int32>(PayloadBytes), PayloadView) || !Reader.AtEnd())
	{
		if (OutError.IsEmpty()) OutError = TEXT("opaque command has a truncated payload or trailing bytes");
		return false;
	}
	uint64 PayloadCanonicalSurchargeBytes = 0;
	if (Schema.PayloadStruct)
	{
		const int32 RemainingAllocation = static_cast<int32>(FMath::Min<uint64>(
			static_cast<uint64>(MaxNativeAllocationBytes) - NativeAllocationBytes,
			static_cast<uint64>(MAX_int32)));
		if (static_cast<uint64>(Schema.PayloadStruct->GetStructureSize()) * 2u
			> static_cast<uint64>(RemainingAllocation))
		{
			OutError = TEXT("opaque command payload root exceeds its native-allocation budget");
			return false;
		}
		Candidate.Payload.InitializeAs(Schema.PayloadStruct);
		const FSeinStructWireLimits PayloadLimits{
			PayloadWireByteLimit(Schema),
			Schema.MaxPayloadAggregateElements,
			MaxIdentifierBytes,
			64,
			RemainingAllocation / 2 };
		FSeinWireCost PayloadCost;
		if (!FSeinCanonicalStateCodec::DecodeWithCost(
			PayloadView, Schema.PayloadStruct, Candidate.Payload.GetMutableMemory(),
			{ Schema.DynamicPayloadStructs, Schema.AllowedPayloadNames },
			PayloadLimits, OutError,
			PayloadCost)
			|| PayloadCost.NativeAllocationBytes > MAX_uint64 / 2u
			|| PayloadCost.CanonicalCostBytes < static_cast<uint64>(PayloadView.Num())
			|| !ChargeAllocation(PayloadCost.NativeAllocationBytes * 2u)) return false;
		PayloadCanonicalSurchargeBytes = PayloadCost.CanonicalCostBytes
			- static_cast<uint64>(PayloadView.Num());
	}

	const uint64 CommonLogicalElements =
		static_cast<uint64>(TargeterCount) + static_cast<uint64>(EntityCount);
	if (!BuildCanonicalCost(
		Bytes.Num(), CommonLogicalElements,
		OutCost.CanonicalCostBytes, OutError)
		|| PayloadCanonicalSurchargeBytes
			> MAX_uint64 - OutCost.CanonicalCostBytes)
	{
		OutCost = {};
		if (OutError.IsEmpty()) OutError = TEXT("command canonical-cost overflow");
		return false;
	}
	OutCost.CanonicalCostBytes += PayloadCanonicalSurchargeBytes;
	OutCost.NativeAllocationBytes = NativeAllocationBytes;
	if (SeinValidateCommandAgainstSchema(Candidate, Schema)
		!= ESeinCommandStructureResult::Valid)
	{
		OutCost = {};
		OutError = TEXT("decoded command failed frozen-schema structural validation");
		return false;
	}
	OutCommand = MoveTemp(Candidate);
	return true;
}

bool FSeinCommandWireCodec::Decode(
	TConstArrayView<uint8> Bytes,
	FSeinCommandWireSchemaLookup FindSchema,
	FSeinCommand& OutCommand,
	FString& OutError,
	int32 MaxDecodedAllocationBytes,
	uint64* OutDecodedAllocationBytes)
{
	if (OutDecodedAllocationBytes) *OutDecodedAllocationBytes = 0;
	FSeinWireCost Cost;
	if (!DecodeWithCost(
		Bytes, FindSchema, OutCommand, OutError,
		MaxDecodedAllocationBytes, Cost)) return false;
	if (OutDecodedAllocationBytes)
		*OutDecodedAllocationBytes = Cost.NativeAllocationBytes;
	return true;
}

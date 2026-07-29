/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSnapshotEnvelopeCodec.cpp
 */

#include "Serialization/SeinSnapshotEnvelopeCodec.h"

#include "Hash/Blake3.h"

namespace
{
	constexpr uint8 SnapshotMagic[8] =
		{'S', 'E', 'I', 'N', 'S', 'N', 'P', '1'};
	constexpr uint64 HeaderFlags = 0;
	constexpr uint16 EntryFlags = 0;
	constexpr uint64 FixedDirectoryEntryBytes = 74;
	constexpr ANSICHAR LeafDomain[] = "SEIN.SNAPSHOT.LEAF.V11";
	constexpr ANSICHAR RootDomain[] = "SEIN.SNAPSHOT.ROOT.V11";

	static_assert(FSeinSnapshotEnvelopeCodec::PrefixBytes == 120);

	bool Fail(FString& OutError, const TCHAR* Message)
	{
		if (OutError.IsEmpty())
		{
			OutError = Message;
		}
		return false;
	}

	bool Fail(FString& OutError, FString Message)
	{
		if (OutError.IsEmpty())
		{
			OutError = MoveTemp(Message);
		}
		return false;
	}

	bool CheckedAdd(uint64 A, uint64 B, uint64& Out)
	{
		if (A > MAX_uint64 - B)
		{
			return false;
		}
		Out = A + B;
		return true;
	}

	bool CheckedMultiply(uint64 A, uint64 B, uint64& Out)
	{
		if (A != 0 && B > MAX_uint64 / A)
		{
			return false;
		}
		Out = A * B;
		return true;
	}

	void WriteBigEndian(uint8* Out, uint64 Value, int32 Width)
	{
		for (int32 Index = 0; Index < Width; ++Index)
		{
			Out[Index] = static_cast<uint8>(
				Value >> ((Width - 1 - Index) * 8));
		}
	}

	uint64 ReadBigEndian(const uint8* Bytes, int32 Width)
	{
		uint64 Value = 0;
		for (int32 Index = 0; Index < Width; ++Index)
		{
			Value = (Value << 8) | Bytes[Index];
		}
		return Value;
	}

	void GuidToBytes(const FGuid& Guid, uint8 (&Out)[16])
	{
		WriteBigEndian(Out, Guid.A, 4);
		WriteBigEndian(Out + 4, Guid.B, 4);
		WriteBigEndian(Out + 8, Guid.C, 4);
		WriteBigEndian(Out + 12, Guid.D, 4);
	}

	FGuid GuidFromBytes(const uint8* Bytes)
	{
		return FGuid(
			static_cast<uint32>(ReadBigEndian(Bytes, 4)),
			static_cast<uint32>(ReadBigEndian(Bytes + 4, 4)),
			static_cast<uint32>(ReadBigEndian(Bytes + 8, 4)),
			static_cast<uint32>(ReadBigEndian(Bytes + 12, 4)));
	}

	FGuid DigestFromHash(const FBlake3Hash& Hash)
	{
		return GuidFromBytes(Hash.GetBytes());
	}

	FGuid HashBytes(TConstArrayView<uint8> Bytes)
	{
		return DigestFromHash(FBlake3::HashBuffer(
			Bytes.GetData(), static_cast<uint64>(Bytes.Num())));
	}

	class FByteWriter
	{
	public:
		FByteWriter(TArray<uint8>& InBytes, uint64 InLimit, FString& InError)
			: Bytes(InBytes)
			, Limit(InLimit)
			, Error(InError)
		{
		}

		bool Raw(const void* Data, uint64 Count)
		{
			const uint64 Existing = static_cast<uint64>(Bytes.Num());
			uint64 NewSize = 0;
			if (!CheckedAdd(Existing, Count, NewSize)
				|| NewSize > Limit
				|| NewSize > static_cast<uint64>(MAX_int32))
			{
				return Fail(Error, TEXT("snapshot envelope byte limit exceeded"));
			}
			if (Count > 0)
			{
				Bytes.Append(
					static_cast<const uint8*>(Data),
					static_cast<int32>(Count));
			}
			return true;
		}

		bool UInt(uint64 Value, int32 Width)
		{
			if (Width != 1 && Width != 2 && Width != 4 && Width != 8)
			{
				return Fail(
					Error, TEXT("snapshot envelope integer width is invalid"));
			}
			uint8 Encoded[8];
			WriteBigEndian(Encoded, Value, Width);
			return Raw(Encoded, static_cast<uint64>(Width));
		}

		bool U8(uint8 Value) { return UInt(Value, 1); }
		bool U16(uint16 Value) { return UInt(Value, 2); }
		bool U32(uint32 Value) { return UInt(Value, 4); }
		bool U64(uint64 Value) { return UInt(Value, 8); }
		bool I64(int64 Value)
		{
			return UInt(BitCast<uint64>(Value), 8);
		}

		bool Guid(const FGuid& Value)
		{
			uint8 Encoded[16];
			GuidToBytes(Value, Encoded);
			return Raw(Encoded, sizeof(Encoded));
		}

	private:
		TArray<uint8>& Bytes;
		uint64 Limit;
		FString& Error;
	};

	class FByteReader
	{
	public:
		FByteReader(TConstArrayView<uint8> InBytes, FString& InError)
			: Bytes(InBytes)
			, Error(InError)
		{
		}

		bool Slice(uint64 Count, TConstArrayView<uint8>& Out)
		{
			const uint64 Size = static_cast<uint64>(Bytes.Num());
			if (Offset > Size || Count > Size - Offset)
			{
				return Fail(Error, TEXT("snapshot envelope value is truncated"));
			}
			Out = TConstArrayView<uint8>(
				Bytes.GetData() + static_cast<int32>(Offset),
				static_cast<int32>(Count));
			Offset += Count;
			return true;
		}

		bool UInt(uint64& Out, int32 Width)
		{
			TConstArrayView<uint8> Encoded;
			if ((Width != 1 && Width != 2 && Width != 4 && Width != 8)
				|| !Slice(static_cast<uint64>(Width), Encoded))
			{
				return false;
			}
			Out = ReadBigEndian(Encoded.GetData(), Width);
			return true;
		}

		bool U8(uint8& Out)
		{
			uint64 Value = 0;
			if (!UInt(Value, 1)) return false;
			Out = static_cast<uint8>(Value);
			return true;
		}

		bool U16(uint16& Out)
		{
			uint64 Value = 0;
			if (!UInt(Value, 2)) return false;
			Out = static_cast<uint16>(Value);
			return true;
		}

		bool U32(uint32& Out)
		{
			uint64 Value = 0;
			if (!UInt(Value, 4)) return false;
			Out = static_cast<uint32>(Value);
			return true;
		}

		bool U64(uint64& Out) { return UInt(Out, 8); }

		bool I64(int64& Out)
		{
			uint64 Value = 0;
			if (!UInt(Value, 8)) return false;
			Out = BitCast<int64>(Value);
			return true;
		}

		bool Guid(FGuid& Out)
		{
			TConstArrayView<uint8> Encoded;
			if (!Slice(16, Encoded)) return false;
			Out = GuidFromBytes(Encoded.GetData());
			return true;
		}

		bool AtEnd() const
		{
			return Offset == static_cast<uint64>(Bytes.Num());
		}

	private:
		TConstArrayView<uint8> Bytes;
		uint64 Offset = 0;
		FString& Error;
	};

	class FDigestWriter
	{
	public:
		void Raw(const void* Data, uint64 Count)
		{
			if (Count > 0)
			{
				Hasher.Update(Data, Count);
			}
		}

		void UInt(uint64 Value, int32 Width)
		{
			uint8 Encoded[8];
			WriteBigEndian(Encoded, Value, Width);
			Raw(Encoded, static_cast<uint64>(Width));
		}

		void U8(uint8 Value) { UInt(Value, 1); }
		void U16(uint16 Value) { UInt(Value, 2); }
		void U32(uint32 Value) { UInt(Value, 4); }
		void U64(uint64 Value) { UInt(Value, 8); }
		void I64(int64 Value) { UInt(BitCast<uint64>(Value), 8); }

		void Guid(const FGuid& Value)
		{
			uint8 Encoded[16];
			GuidToBytes(Value, Encoded);
			Raw(Encoded, sizeof(Encoded));
		}

		FGuid Finalize() const
		{
			return DigestFromHash(Hasher.Finalize());
		}

	private:
		FBlake3 Hasher;
	};

	struct FSectionFrame
	{
		FString SectionId;
		ESeinSnapshotSectionRole Role =
			ESeinSnapshotSectionRole::Authoritative;
		ESeinSnapshotSectionCodec Codec =
			ESeinSnapshotSectionCodec::CanonicalBytes;
		uint32 SchemaVersion = 0;
		FGuid SchemaDigest;
		FGuid DescriptorDigest;
		uint64 PayloadOffset = 0;
		TConstArrayView<uint8> Payload;
		FGuid LeafDigest;
	};

	bool IsKnownRole(ESeinSnapshotSectionRole Role)
	{
		switch (Role)
		{
		case ESeinSnapshotSectionRole::Authoritative:
		case ESeinSnapshotSectionRole::Continuation:
		case ESeinSnapshotSectionRole::DerivedCache:
		case ESeinSnapshotSectionRole::Local:
			return true;
		default:
			return false;
		}
	}

	bool ContributesToAggregateRoot(ESeinSnapshotSectionRole Role)
	{
		return Role == ESeinSnapshotSectionRole::Authoritative
			|| Role == ESeinSnapshotSectionRole::Continuation;
	}

	bool IsAlphaNumericASCII(TCHAR Character)
	{
		return (Character >= TCHAR('a') && Character <= TCHAR('z'))
			|| (Character >= TCHAR('0') && Character <= TCHAR('9'));
	}

	bool ValidateSectionId(const FString& SectionId, FString& OutError)
	{
		if (SectionId.IsEmpty()
			|| SectionId.Len()
				> static_cast<int32>(
					FSeinSnapshotEnvelopeCodec::MaxSectionIdBytes)
			|| !IsAlphaNumericASCII(SectionId[0])
			|| !IsAlphaNumericASCII(SectionId[SectionId.Len() - 1]))
		{
			return Fail(
				OutError,
				TEXT("snapshot section ID violates the lowercase-ASCII contract"));
		}
		for (const TCHAR Character : SectionId)
		{
			const bool bAllowed = IsAlphaNumericASCII(Character)
				|| Character == TCHAR('.')
				|| Character == TCHAR('_')
				|| Character == TCHAR('/')
				|| Character == TCHAR('-');
			if (!bAllowed)
			{
				return Fail(
					OutError,
					TEXT("snapshot section ID violates the lowercase-ASCII contract"));
			}
		}
		return true;
	}

	bool ValidateSectionContract(
		const FSectionFrame& Section,
		FString& OutError)
	{
		if (!ValidateSectionId(Section.SectionId, OutError))
		{
			return false;
		}
		if (!IsKnownRole(Section.Role))
		{
			return Fail(OutError, TEXT("snapshot section role is unknown"));
		}
		if (Section.Codec != ESeinSnapshotSectionCodec::CanonicalBytes)
		{
			return Fail(
				OutError,
				TEXT("snapshot section codec is unknown or compressed"));
		}
		if (Section.SchemaVersion == 0
			|| !Section.SchemaDigest.IsValid()
			|| !Section.DescriptorDigest.IsValid())
		{
			return Fail(
				OutError,
				TEXT("snapshot section schema contract is invalid"));
		}
		if (Section.Role == ESeinSnapshotSectionRole::DerivedCache
			&& !Section.Payload.IsEmpty())
		{
			return Fail(
				OutError,
				TEXT("derived-cache snapshot sections must not carry payload"));
		}
		if (static_cast<uint64>(Section.Payload.Num())
			> FSeinSnapshotEnvelopeCodec::MaxSectionPayloadBytes)
		{
			return Fail(
				OutError,
				TEXT("snapshot section payload exceeds its bound"));
		}
		return true;
	}

	void HashSectionIdentity(
		FDigestWriter& Writer,
		const FSectionFrame& Section)
	{
		FTCHARToUTF8 SectionId(*Section.SectionId, Section.SectionId.Len());
		Writer.U16(static_cast<uint16>(SectionId.Length()));
		Writer.Raw(SectionId.Get(), static_cast<uint64>(SectionId.Length()));
		Writer.U8(static_cast<uint8>(Section.Role));
		Writer.U8(static_cast<uint8>(Section.Codec));
		Writer.U16(EntryFlags);
		Writer.U32(Section.SchemaVersion);
		Writer.Guid(Section.SchemaDigest);
		Writer.Guid(Section.DescriptorDigest);
		Writer.U64(static_cast<uint64>(Section.Payload.Num()));
	}

	FGuid ComputeLeafDigest(const FSectionFrame& Section)
	{
		FDigestWriter Writer;
		Writer.Raw(LeafDomain, UE_ARRAY_COUNT(LeafDomain) - 1);
		HashSectionIdentity(Writer, Section);
		Writer.Raw(
			Section.Payload.GetData(),
			static_cast<uint64>(Section.Payload.Num()));
		return Writer.Finalize();
	}

	FGuid ComputeAggregateStateRoot(
		int64 SnapshotTick,
		const FGuid& CommandProtocolDigest,
		const FGuid& CompatibilityDigest,
		TConstArrayView<FSectionFrame> Sections)
	{
		uint32 ContributingSections = 0;
		for (const FSectionFrame& Section : Sections)
		{
			if (ContributesToAggregateRoot(Section.Role))
			{
				++ContributingSections;
			}
		}

		FDigestWriter Writer;
		Writer.Raw(RootDomain, UE_ARRAY_COUNT(RootDomain) - 1);
		Writer.U32(FSeinSnapshotEnvelopeCodec::WireFormatVersion);
		Writer.U32(FSeinSnapshotEnvelopeCodec::SnapshotSemanticsVersion);
		Writer.I64(SnapshotTick);
		Writer.Guid(CommandProtocolDigest);
		Writer.Guid(CompatibilityDigest);
		Writer.U32(ContributingSections);
		for (const FSectionFrame& Section : Sections)
		{
			if (!ContributesToAggregateRoot(Section.Role))
			{
				continue;
			}
			HashSectionIdentity(Writer, Section);
			Writer.Guid(Section.LeafDigest);
		}
		return Writer.Finalize();
	}

	bool ParsePrefixInternal(
		TConstArrayView<uint8> Prefix,
		FSeinSnapshotEnvelopeMetadata& OutMetadata,
		FString& OutError)
	{
		if (Prefix.Num() != FSeinSnapshotEnvelopeCodec::PrefixBytes)
		{
			return Fail(
				OutError,
				TEXT("snapshot envelope prefix must be exactly 120 bytes"));
		}

		FByteReader Reader(Prefix, OutError);
		TConstArrayView<uint8> Magic;
		uint32 PrefixByteCount = 0;
		uint64 Flags = 0;
		if (!Reader.Slice(UE_ARRAY_COUNT(SnapshotMagic), Magic)
			|| FMemory::Memcmp(
				Magic.GetData(), SnapshotMagic, UE_ARRAY_COUNT(SnapshotMagic))
				!= 0)
		{
			return Fail(OutError, TEXT("snapshot envelope magic mismatch"));
		}
		if (!Reader.U32(OutMetadata.WireFormatVersion)
			|| !Reader.U32(OutMetadata.SnapshotSemanticsVersion)
			|| !Reader.U32(PrefixByteCount)
			|| !Reader.U32(OutMetadata.SectionCount)
			|| !Reader.U64(OutMetadata.DirectoryBytes)
			|| !Reader.U64(OutMetadata.BodyBytes)
			|| !Reader.U64(Flags)
			|| !Reader.I64(OutMetadata.SnapshotTick)
			|| !Reader.Guid(OutMetadata.CommandProtocolDigest)
			|| !Reader.Guid(OutMetadata.CompatibilityDigest)
			|| !Reader.Guid(OutMetadata.AggregateStateRoot)
			|| !Reader.Guid(OutMetadata.BodyDigest)
			|| !Reader.AtEnd())
		{
			return Fail(OutError, TEXT("snapshot envelope prefix is truncated"));
		}

		if (OutMetadata.WireFormatVersion
				!= FSeinSnapshotEnvelopeCodec::WireFormatVersion
			|| OutMetadata.SnapshotSemanticsVersion
				!= FSeinSnapshotEnvelopeCodec::SnapshotSemanticsVersion
			|| PrefixByteCount
				!= static_cast<uint32>(
					FSeinSnapshotEnvelopeCodec::PrefixBytes))
		{
			return Fail(
				OutError,
				TEXT("snapshot envelope version or prefix size is unsupported"));
		}
		if (Flags != HeaderFlags)
		{
			return Fail(OutError, TEXT("snapshot envelope header flags are unknown"));
		}
		if (OutMetadata.SnapshotTick < 0)
		{
			return Fail(OutError, TEXT("snapshot envelope tick is negative"));
		}
		if (OutMetadata.SectionCount
			> FSeinSnapshotEnvelopeCodec::MaxSections)
		{
			return Fail(
				OutError,
				TEXT("snapshot envelope section count exceeds its bound"));
		}
		if (OutMetadata.DirectoryBytes
			> FSeinSnapshotEnvelopeCodec::MaxDirectoryBytes)
		{
			return Fail(
				OutError,
				TEXT("snapshot envelope directory exceeds its bound"));
		}
		if (OutMetadata.BodyBytes
			> FSeinSnapshotEnvelopeCodec::MaxBodyBytes)
		{
			return Fail(
				OutError,
				TEXT("snapshot envelope body exceeds its bound"));
		}
		if (OutMetadata.DirectoryBytes > OutMetadata.BodyBytes)
		{
			return Fail(
				OutError,
				TEXT("snapshot envelope directory exceeds its body"));
		}
		if (!OutMetadata.CommandProtocolDigest.IsValid()
			|| !OutMetadata.CompatibilityDigest.IsValid()
			|| !OutMetadata.AggregateStateRoot.IsValid()
			|| !OutMetadata.BodyDigest.IsValid())
		{
			return Fail(
				OutError,
				TEXT("snapshot envelope prefix contains an invalid digest"));
		}

		uint64 MinimumDirectoryBytes = 0;
		if (!CheckedMultiply(
				OutMetadata.SectionCount,
				FixedDirectoryEntryBytes + 1,
				MinimumDirectoryBytes))
		{
			return Fail(
				OutError,
				TEXT("snapshot envelope directory-size arithmetic overflow"));
		}
		if (OutMetadata.SectionCount == 0)
		{
			if (OutMetadata.DirectoryBytes != 0
				|| OutMetadata.BodyBytes != 0)
			{
				return Fail(
					OutError,
					TEXT("empty snapshot envelope owns unexpected body bytes"));
			}
		}
		else if (OutMetadata.DirectoryBytes < MinimumDirectoryBytes)
		{
			return Fail(
				OutError,
				TEXT("snapshot envelope directory is too small for its section count"));
		}
		return true;
	}

	bool DecodeSectionId(
		FByteReader& Reader,
		FString& OutSectionId,
		FString& OutError)
	{
		uint16 SectionIdBytes = 0;
		TConstArrayView<uint8> Encoded;
		if (!Reader.U16(SectionIdBytes)
			|| SectionIdBytes == 0
			|| SectionIdBytes
				> FSeinSnapshotEnvelopeCodec::MaxSectionIdBytes
			|| !Reader.Slice(SectionIdBytes, Encoded))
		{
			return Fail(
				OutError,
				TEXT("snapshot section ID length exceeds its bound"));
		}

		OutSectionId.Reset(SectionIdBytes);
		for (const uint8 Byte : Encoded)
		{
			OutSectionId.AppendChar(static_cast<TCHAR>(Byte));
		}
		return ValidateSectionId(OutSectionId, OutError);
	}

	bool ParseDirectory(
		TConstArrayView<uint8> Body,
		const FSeinSnapshotEnvelopeMetadata& Metadata,
		TArray<FSectionFrame>& OutSections,
		FString& OutError)
	{
		TConstArrayView<uint8> Directory(
			Body.GetData(), static_cast<int32>(Metadata.DirectoryBytes));
		FByteReader Reader(Directory, OutError);
		OutSections.Reset(static_cast<int32>(Metadata.SectionCount));
		uint64 ExpectedPayloadOffset = Metadata.DirectoryBytes;
		FString PreviousSectionId;

		for (uint32 Index = 0; Index < Metadata.SectionCount; ++Index)
		{
			FSectionFrame Section;
			uint8 Role = 0;
			uint8 Codec = 0;
			uint16 Flags = 0;
			uint64 PayloadBytes = 0;
			if (!DecodeSectionId(Reader, Section.SectionId, OutError)
				|| !Reader.U8(Role)
				|| !Reader.U8(Codec)
				|| !Reader.U16(Flags)
				|| !Reader.U32(Section.SchemaVersion)
				|| !Reader.Guid(Section.SchemaDigest)
				|| !Reader.Guid(Section.DescriptorDigest)
				|| !Reader.U64(Section.PayloadOffset)
				|| !Reader.U64(PayloadBytes)
				|| !Reader.Guid(Section.LeafDigest))
			{
				return false;
			}
			Section.Role = static_cast<ESeinSnapshotSectionRole>(Role);
			Section.Codec = static_cast<ESeinSnapshotSectionCodec>(Codec);
			if (Flags != EntryFlags)
			{
				return Fail(
					OutError,
					TEXT("snapshot section entry flags are unknown"));
			}
			if (!PreviousSectionId.IsEmpty()
				&& PreviousSectionId.Compare(
					Section.SectionId, ESearchCase::CaseSensitive) >= 0)
			{
				return Fail(
					OutError,
					TEXT("snapshot section IDs are duplicate or not canonically sorted"));
			}
			PreviousSectionId = Section.SectionId;

			if (Section.PayloadOffset != ExpectedPayloadOffset)
			{
				return Fail(
					OutError,
					TEXT("snapshot section payload offsets are not contiguous"));
			}
			if (PayloadBytes
				> FSeinSnapshotEnvelopeCodec::MaxSectionPayloadBytes)
			{
				return Fail(
					OutError,
					TEXT("snapshot section payload exceeds its bound"));
			}
			uint64 EndOffset = 0;
			if (!CheckedAdd(Section.PayloadOffset, PayloadBytes, EndOffset)
				|| EndOffset > Metadata.BodyBytes)
			{
				return Fail(
					OutError,
					TEXT("snapshot section payload range exceeds the body"));
			}
			Section.Payload = TConstArrayView<uint8>(
				Body.GetData() + static_cast<int32>(Section.PayloadOffset),
				static_cast<int32>(PayloadBytes));
			ExpectedPayloadOffset = EndOffset;

			if (!ValidateSectionContract(Section, OutError))
			{
				return false;
			}
			OutSections.Add(MoveTemp(Section));
		}

		if (!Reader.AtEnd())
		{
			return Fail(
				OutError,
				TEXT("snapshot section directory contains trailing bytes"));
		}
		if (ExpectedPayloadOffset != Metadata.BodyBytes)
		{
			return Fail(
				OutError,
				TEXT("snapshot envelope contains unowned trailing payload bytes"));
		}
		return true;
	}
}

bool FSeinSnapshotEnvelopeCodec::ParsePrefix(
	TConstArrayView<uint8> Prefix,
	FSeinSnapshotEnvelopeMetadata& OutMetadata,
	FString& OutError)
{
	OutError.Reset();
	FSeinSnapshotEnvelopeMetadata Candidate;
	if (!ParsePrefixInternal(Prefix, Candidate, OutError))
	{
		return false;
	}
	OutMetadata = MoveTemp(Candidate);
	return true;
}

bool FSeinSnapshotEnvelopeCodec::Encode(
	const FSeinSnapshotEnvelope& Envelope,
	TArray<uint8>& OutBytes,
	FSeinSnapshotEnvelopeMetadata& OutMetadata,
	FString& OutError)
{
	OutError.Reset();
	if (Envelope.SnapshotTick < 0)
	{
		return Fail(OutError, TEXT("snapshot envelope tick is negative"));
	}
	if (!Envelope.CommandProtocolDigest.IsValid()
		|| !Envelope.CompatibilityDigest.IsValid())
	{
		return Fail(
			OutError,
			TEXT("snapshot envelope compatibility digests are invalid"));
	}
	if (Envelope.Sections.Num() > static_cast<int32>(MaxSections))
	{
		return Fail(
			OutError,
			TEXT("snapshot envelope section count exceeds its bound"));
	}

	TArray<FSectionFrame> Sections;
	Sections.Reserve(Envelope.Sections.Num());
	for (const FSeinSnapshotEnvelopeSection& Source : Envelope.Sections)
	{
		FSectionFrame& Section = Sections.AddDefaulted_GetRef();
		Section.SectionId = Source.SectionId;
		Section.Role = Source.Role;
		Section.Codec = Source.Codec;
		Section.SchemaVersion = Source.SchemaVersion;
		Section.SchemaDigest = Source.SchemaDigest;
		Section.DescriptorDigest = Source.DescriptorDigest;
		Section.Payload = TConstArrayView<uint8>(Source.Payload);
		if (!ValidateSectionContract(Section, OutError))
		{
			return false;
		}
	}
	Sections.Sort([](const FSectionFrame& A, const FSectionFrame& B)
	{
		return A.SectionId.Compare(
			B.SectionId, ESearchCase::CaseSensitive) < 0;
	});
	for (int32 Index = 1; Index < Sections.Num(); ++Index)
	{
		if (Sections[Index - 1].SectionId == Sections[Index].SectionId)
		{
			return Fail(
				OutError,
				TEXT("snapshot section IDs must be unique"));
		}
	}

	uint64 DirectoryBytes = 0;
	uint64 PayloadBytes = 0;
	for (FSectionFrame& Section : Sections)
	{
		uint64 EntryBytes = 0;
		if (!CheckedAdd(
				FixedDirectoryEntryBytes,
				static_cast<uint64>(Section.SectionId.Len()),
				EntryBytes)
			|| !CheckedAdd(DirectoryBytes, EntryBytes, DirectoryBytes)
			|| !CheckedAdd(
				PayloadBytes,
				static_cast<uint64>(Section.Payload.Num()),
				PayloadBytes))
		{
			return Fail(
				OutError,
				TEXT("snapshot envelope size arithmetic overflow"));
		}
	}
	if (DirectoryBytes > MaxDirectoryBytes)
	{
		return Fail(
			OutError,
			TEXT("snapshot envelope directory exceeds its bound"));
	}
	uint64 BodyBytes = 0;
	if (!CheckedAdd(DirectoryBytes, PayloadBytes, BodyBytes)
		|| BodyBytes > MaxBodyBytes)
	{
		return Fail(
			OutError,
			TEXT("snapshot envelope body exceeds its bound"));
	}
	uint64 FileBytes = 0;
	if (!CheckedAdd(PrefixBytes, BodyBytes, FileBytes)
		|| FileBytes > static_cast<uint64>(MAX_int32))
	{
		return Fail(
			OutError,
			TEXT("snapshot envelope file size exceeds its native bound"));
	}

	uint64 NextPayloadOffset = DirectoryBytes;
	for (FSectionFrame& Section : Sections)
	{
		Section.PayloadOffset = NextPayloadOffset;
		Section.LeafDigest = ComputeLeafDigest(Section);
		if (!CheckedAdd(
			NextPayloadOffset,
			static_cast<uint64>(Section.Payload.Num()),
			NextPayloadOffset))
		{
			return Fail(
				OutError,
				TEXT("snapshot envelope payload-offset arithmetic overflow"));
		}
	}
	if (NextPayloadOffset != BodyBytes)
	{
		return Fail(
			OutError,
			TEXT("snapshot envelope payload layout is inconsistent"));
	}

	// Assemble directly into the final file buffer. The fixed prefix is filled
	// after the body digest is known, avoiding a second body-sized allocation.
	TArray<uint8> Candidate;
	Candidate.Reserve(static_cast<int32>(FileBytes));
	Candidate.AddZeroed(PrefixBytes);
	FByteWriter BodyWriter(Candidate, FileBytes, OutError);
	for (const FSectionFrame& Section : Sections)
	{
		FTCHARToUTF8 SectionId(
			*Section.SectionId, Section.SectionId.Len());
		if (!BodyWriter.U16(static_cast<uint16>(SectionId.Length()))
			|| !BodyWriter.Raw(
				SectionId.Get(), static_cast<uint64>(SectionId.Length()))
			|| !BodyWriter.U8(static_cast<uint8>(Section.Role))
			|| !BodyWriter.U8(static_cast<uint8>(Section.Codec))
			|| !BodyWriter.U16(EntryFlags)
			|| !BodyWriter.U32(Section.SchemaVersion)
			|| !BodyWriter.Guid(Section.SchemaDigest)
			|| !BodyWriter.Guid(Section.DescriptorDigest)
			|| !BodyWriter.U64(Section.PayloadOffset)
			|| !BodyWriter.U64(
				static_cast<uint64>(Section.Payload.Num()))
			|| !BodyWriter.Guid(Section.LeafDigest))
		{
			return false;
		}
	}
	if (static_cast<uint64>(Candidate.Num())
		!= static_cast<uint64>(PrefixBytes) + DirectoryBytes)
	{
		return Fail(
			OutError,
			TEXT("snapshot envelope directory size is inconsistent"));
	}
	for (const FSectionFrame& Section : Sections)
	{
		if (!BodyWriter.Raw(
			Section.Payload.GetData(),
			static_cast<uint64>(Section.Payload.Num())))
		{
			return false;
		}
	}
	if (static_cast<uint64>(Candidate.Num()) != FileBytes)
	{
		return Fail(
			OutError,
			TEXT("snapshot envelope body size is inconsistent"));
	}

	FSeinSnapshotEnvelopeMetadata Metadata;
	Metadata.WireFormatVersion = WireFormatVersion;
	Metadata.SnapshotSemanticsVersion = SnapshotSemanticsVersion;
	Metadata.SectionCount = static_cast<uint32>(Sections.Num());
	Metadata.DirectoryBytes = DirectoryBytes;
	Metadata.BodyBytes = BodyBytes;
	Metadata.SnapshotTick = Envelope.SnapshotTick;
	Metadata.CommandProtocolDigest = Envelope.CommandProtocolDigest;
	Metadata.CompatibilityDigest = Envelope.CompatibilityDigest;
	Metadata.AggregateStateRoot = ComputeAggregateStateRoot(
		Envelope.SnapshotTick,
		Envelope.CommandProtocolDigest,
		Envelope.CompatibilityDigest,
		Sections);
	const TConstArrayView<uint8> EncodedBody(
		Candidate.GetData() + PrefixBytes,
		static_cast<int32>(BodyBytes));
	Metadata.BodyDigest = HashBytes(EncodedBody);

	TArray<uint8> EncodedPrefix;
	EncodedPrefix.Reserve(PrefixBytes);
	FByteWriter PrefixWriter(EncodedPrefix, PrefixBytes, OutError);
	if (!PrefixWriter.Raw(SnapshotMagic, UE_ARRAY_COUNT(SnapshotMagic))
		|| !PrefixWriter.U32(WireFormatVersion)
		|| !PrefixWriter.U32(SnapshotSemanticsVersion)
		|| !PrefixWriter.U32(PrefixBytes)
		|| !PrefixWriter.U32(Metadata.SectionCount)
		|| !PrefixWriter.U64(Metadata.DirectoryBytes)
		|| !PrefixWriter.U64(Metadata.BodyBytes)
		|| !PrefixWriter.U64(HeaderFlags)
		|| !PrefixWriter.I64(Metadata.SnapshotTick)
		|| !PrefixWriter.Guid(Metadata.CommandProtocolDigest)
		|| !PrefixWriter.Guid(Metadata.CompatibilityDigest)
		|| !PrefixWriter.Guid(Metadata.AggregateStateRoot)
		|| !PrefixWriter.Guid(Metadata.BodyDigest)
		|| EncodedPrefix.Num() != PrefixBytes
		|| static_cast<uint64>(Candidate.Num()) != FileBytes)
	{
		return Fail(
			OutError,
			TEXT("snapshot envelope prefix or body assembly failed"));
	}
	FMemory::Memcpy(
		Candidate.GetData(), EncodedPrefix.GetData(), PrefixBytes);

	OutBytes = MoveTemp(Candidate);
	OutMetadata = MoveTemp(Metadata);
	return true;
}

bool FSeinSnapshotEnvelopeCodec::Decode(
	TConstArrayView<uint8> Bytes,
	FSeinSnapshotEnvelope& OutEnvelope,
	FSeinSnapshotEnvelopeMetadata& OutMetadata,
	FString& OutError)
{
	OutError.Reset();
	if (Bytes.Num() < PrefixBytes)
	{
		return Fail(
			OutError,
			TEXT("snapshot envelope is smaller than its fixed prefix"));
	}

	FSeinSnapshotEnvelopeMetadata Metadata;
	const TConstArrayView<uint8> Prefix(Bytes.GetData(), PrefixBytes);
	if (!ParsePrefixInternal(Prefix, Metadata, OutError))
	{
		return false;
	}

	uint64 ExpectedFileBytes = 0;
	if (!CheckedAdd(PrefixBytes, Metadata.BodyBytes, ExpectedFileBytes)
		|| ExpectedFileBytes != static_cast<uint64>(Bytes.Num()))
	{
		return Fail(
			OutError,
			TEXT("snapshot envelope declared body does not exactly match file length"));
	}
	const TConstArrayView<uint8> Body(
		Bytes.GetData() + PrefixBytes,
		static_cast<int32>(Metadata.BodyBytes));

	// Corruption is checked before any directory field or payload range is
	// interpreted. Source authentication is an outer transport/file concern.
	if (HashBytes(Body) != Metadata.BodyDigest)
	{
		return Fail(
			OutError,
			TEXT("snapshot envelope exact-body digest mismatch"));
	}

	TArray<FSectionFrame> Sections;
	if (!ParseDirectory(Body, Metadata, Sections, OutError))
	{
		return false;
	}
	for (const FSectionFrame& Section : Sections)
	{
		if (ComputeLeafDigest(Section) != Section.LeafDigest)
		{
			return Fail(
				OutError,
				FString::Printf(
					TEXT("snapshot section '%s' leaf digest mismatch"),
					*Section.SectionId));
		}
	}
	const FGuid AggregateStateRoot = ComputeAggregateStateRoot(
		Metadata.SnapshotTick,
		Metadata.CommandProtocolDigest,
		Metadata.CompatibilityDigest,
		Sections);
	if (AggregateStateRoot != Metadata.AggregateStateRoot)
	{
		return Fail(
			OutError,
			TEXT("snapshot envelope aggregate state root mismatch"));
	}

	FSeinSnapshotEnvelope Candidate;
	Candidate.SnapshotTick = Metadata.SnapshotTick;
	Candidate.CommandProtocolDigest = Metadata.CommandProtocolDigest;
	Candidate.CompatibilityDigest = Metadata.CompatibilityDigest;
	Candidate.Sections.Reserve(Sections.Num());
	for (const FSectionFrame& Section : Sections)
	{
		FSeinSnapshotEnvelopeSection& Decoded =
			Candidate.Sections.AddDefaulted_GetRef();
		Decoded.SectionId = Section.SectionId;
		Decoded.Role = Section.Role;
		Decoded.Codec = Section.Codec;
		Decoded.SchemaVersion = Section.SchemaVersion;
		Decoded.SchemaDigest = Section.SchemaDigest;
		Decoded.DescriptorDigest = Section.DescriptorDigest;
		Decoded.Payload.Append(
			Section.Payload.GetData(), Section.Payload.Num());
	}

	OutEnvelope = MoveTemp(Candidate);
	OutMetadata = MoveTemp(Metadata);
	return true;
}

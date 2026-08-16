#include "CQTest.h"

#include "Data/SeinWorldSnapshot.h"
#include "Hash/Blake3.h"
#include "Serialization/SeinSnapshotEnvelopeCodec.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		constexpr int32 PrefixBytes =
			FSeinSnapshotEnvelopeCodec::PrefixBytes;
		constexpr int32 SectionCountOffset = 20;
		constexpr int32 DirectoryBytesOffset = 24;
		constexpr int32 BodyBytesOffset = 32;
		constexpr int32 HeaderFlagsOffset = 40;
		constexpr int32 AggregateDigestOffset = 88;
		constexpr int32 BodyDigestOffset = 104;

		uint16 ReadU16(const TArray<uint8>& Bytes, int32 Offset)
		{
			return static_cast<uint16>(
				(static_cast<uint16>(Bytes[Offset]) << 8)
				| static_cast<uint16>(Bytes[Offset + 1]));
		}

		uint32 ReadU32(const TArray<uint8>& Bytes, int32 Offset)
		{
			return (static_cast<uint32>(Bytes[Offset]) << 24)
				| (static_cast<uint32>(Bytes[Offset + 1]) << 16)
				| (static_cast<uint32>(Bytes[Offset + 2]) << 8)
				| static_cast<uint32>(Bytes[Offset + 3]);
		}

		uint64 ReadU64(const TArray<uint8>& Bytes, int32 Offset)
		{
			return (static_cast<uint64>(ReadU32(Bytes, Offset)) << 32)
				| static_cast<uint64>(ReadU32(Bytes, Offset + 4));
		}

		void WriteU16(TArray<uint8>& Bytes, int32 Offset, uint16 Value)
		{
			Bytes[Offset] = static_cast<uint8>(Value >> 8);
			Bytes[Offset + 1] = static_cast<uint8>(Value);
		}

		void WriteU32(TArray<uint8>& Bytes, int32 Offset, uint32 Value)
		{
			Bytes[Offset] = static_cast<uint8>(Value >> 24);
			Bytes[Offset + 1] = static_cast<uint8>(Value >> 16);
			Bytes[Offset + 2] = static_cast<uint8>(Value >> 8);
			Bytes[Offset + 3] = static_cast<uint8>(Value);
		}

		void WriteU64(TArray<uint8>& Bytes, int32 Offset, uint64 Value)
		{
			WriteU32(Bytes, Offset, static_cast<uint32>(Value >> 32));
			WriteU32(Bytes, Offset + 4, static_cast<uint32>(Value));
		}

		void RewriteBodyDigest(TArray<uint8>& Bytes)
		{
			const FBlake3Hash Hash = FBlake3::HashBuffer(
				Bytes.GetData() + PrefixBytes,
				static_cast<uint64>(Bytes.Num() - PrefixBytes));
			FMemory::Memcpy(
				Bytes.GetData() + BodyDigestOffset,
				Hash.GetBytes(),
				16);
		}

		FGuid MakeDigest(uint32 Seed)
		{
			return FGuid(
				Seed,
				Seed + 0x10101010u,
				Seed + 0x20202020u,
				Seed + 0x30303030u);
		}

		FSeinSnapshotEnvelopeSection MakeSection(
			const TCHAR* SectionId,
			ESeinSnapshotSectionRole Role,
			TArray<uint8> Payload,
			uint32 Seed)
		{
			FSeinSnapshotEnvelopeSection Section;
			Section.SectionId = SectionId;
			Section.Role = Role;
			Section.SchemaVersion = Seed;
			Section.SchemaDigest = MakeDigest(Seed * 10);
			Section.DescriptorDigest = MakeDigest(Seed * 100);
			Section.Payload = MoveTemp(Payload);
			return Section;
		}

		FSeinSnapshotEnvelope MakeCanonicalFixture()
		{
			FSeinSnapshotEnvelope Envelope;
			Envelope.SnapshotTick =
				static_cast<int64>(0x0102030405060708ULL);
			Envelope.CommandProtocolDigest = FGuid(
				0x11121314u, 0x21222324u, 0x31323334u, 0x41424344u);
			Envelope.CompatibilityDigest = FGuid(
				0x51525354u, 0x61626364u, 0x71727374u, 0x81828384u);

			// Deliberately noncanonical insertion order.
			Envelope.Sections.Add(MakeSection(
				TEXT("local.camera"),
				ESeinSnapshotSectionRole::Local,
				{9},
				4));
			Envelope.Sections.Add(MakeSection(
				TEXT("core.cache"),
				ESeinSnapshotSectionRole::DerivedCache,
				{},
				3));
			Envelope.Sections.Add(MakeSection(
				TEXT("core.bravo"),
				ESeinSnapshotSectionRole::Continuation,
				{4, 5},
				2));
			Envelope.Sections.Add(MakeSection(
				TEXT("core.alpha"),
				ESeinSnapshotSectionRole::Authoritative,
				{1, 2, 3},
				1));
			return Envelope;
		}

		FSeinSnapshotEnvelopeSection* FindSection(
			FSeinSnapshotEnvelope& Envelope,
			const TCHAR* SectionId)
		{
			return Envelope.Sections.FindByPredicate(
				[SectionId](const FSeinSnapshotEnvelopeSection& Section)
				{
					return Section.SectionId == SectionId;
				});
		}

		struct FEntryOffsets
		{
			int32 Entry = 0;
			int32 Id = 0;
			int32 Role = 0;
			int32 Codec = 0;
			int32 Flags = 0;
			int32 SchemaVersion = 0;
			int32 SchemaDigest = 0;
			int32 DescriptorDigest = 0;
			int32 PayloadOffset = 0;
			int32 PayloadBytes = 0;
			int32 LeafDigest = 0;
			int32 NextEntry = 0;
			uint16 IdBytes = 0;
		};

		FEntryOffsets GetEntryOffsets(
			const TArray<uint8>& File,
			int32 EntryOffset)
		{
			FEntryOffsets Result;
			Result.Entry = EntryOffset;
			Result.IdBytes = ReadU16(File, EntryOffset);
			Result.Id = EntryOffset + 2;
			Result.Role = Result.Id + Result.IdBytes;
			Result.Codec = Result.Role + 1;
			Result.Flags = Result.Codec + 1;
			Result.SchemaVersion = Result.Flags + 2;
			Result.SchemaDigest = Result.SchemaVersion + 4;
			Result.DescriptorDigest = Result.SchemaDigest + 16;
			Result.PayloadOffset = Result.DescriptorDigest + 16;
			Result.PayloadBytes = Result.PayloadOffset + 8;
			Result.LeafDigest = Result.PayloadBytes + 8;
			Result.NextEntry = Result.LeafDigest + 16;
			return Result;
		}

		bool DecodeFailsWith(
			const TArray<uint8>& Bytes,
			const TCHAR* ExpectedError,
			FString& OutObservedError)
		{
			FSeinSnapshotEnvelope Destination;
			Destination.SnapshotTick = 777;
			Destination.Sections.Add(MakeSection(
				TEXT("sentinel.state"),
				ESeinSnapshotSectionRole::Authoritative,
				{77},
				77));
			FSeinSnapshotEnvelopeMetadata Metadata;
			Metadata.SnapshotTick = 888;

			const bool bDecoded = FSeinSnapshotEnvelopeCodec::Decode(
				Bytes, Destination, Metadata, OutObservedError);
			return !bDecoded
				&& OutObservedError.Contains(ExpectedError)
				&& Destination.SnapshotTick == 777
				&& Destination.Sections.Num() == 1
				&& Destination.Sections[0].SectionId == TEXT("sentinel.state")
				&& Metadata.SnapshotTick == 888;
		}
	}

	TEST(SnapshotV17EnvelopeHasFrozenBigEndianFramingAndCanonicalOrder,
		"SeinARTS.Unit.CoreEntity.SnapshotEnvelope")
	{
		const FSeinSnapshotEnvelope Source = MakeCanonicalFixture();
		TArray<uint8> Bytes;
		FSeinSnapshotEnvelopeMetadata Metadata;
		FString Error;
		ASSERT_THAT(IsTrue(FSeinSnapshotEnvelopeCodec::Encode(
			Source, Bytes, Metadata, Error)));
		const FBlake3Hash FrozenFileHash = FBlake3::HashBuffer(
			FMemoryView(Bytes.GetData(), Bytes.Num()));
		ASSERT_THAT(AreEqual(
			FString(TEXT("222E3B64D6A6DDA09D4BD691222A077B")),
			Metadata.AggregateStateRoot.ToString(EGuidFormats::Digits)));
		ASSERT_THAT(AreEqual(
			FString(TEXT("8F6E11B2EA6DCFABF443CE46B9FB86F8")),
			Metadata.BodyDigest.ToString(EGuidFormats::Digits)));
		ASSERT_THAT(AreEqual(
			FString(TEXT(
				"0A7A0F62BC11A42563267EF74E49C201"
				"658E2607570E6BD1375530A2A1E91060")),
			BytesToHex(FrozenFileHash.GetBytes(), 32)));

		const uint8 ExpectedMagic[8] =
			{'S', 'E', 'I', 'N', 'S', 'N', 'P', '1'};
		ASSERT_THAT(IsTrue(
			Bytes.Num() == 464
			&& FMemory::Memcmp(Bytes.GetData(), ExpectedMagic, 8) == 0));
		ASSERT_THAT(AreEqual(
			FSeinSnapshotEnvelopeCodec::WireFormatVersion,
			ReadU32(Bytes, 8)));
		ASSERT_THAT(AreEqual(
			FSeinSnapshotEnvelopeCodec::SnapshotSemanticsVersion,
			ReadU32(Bytes, 12)));
		ASSERT_THAT(AreEqual(
			static_cast<uint32>(FSeinWorldSnapshot::CurrentVersion),
			FSeinSnapshotEnvelopeCodec::SnapshotSemanticsVersion));
		ASSERT_THAT(AreEqual(
			static_cast<uint32>(PrefixBytes), ReadU32(Bytes, 16)));
		ASSERT_THAT(AreEqual(static_cast<uint32>(4), ReadU32(Bytes, 20)));
		ASSERT_THAT(IsTrue(ReadU64(Bytes, 24) == 338));
		ASSERT_THAT(IsTrue(ReadU64(Bytes, 32) == 344));
		ASSERT_THAT(IsTrue(ReadU64(Bytes, HeaderFlagsOffset) == 0));
		ASSERT_THAT(IsTrue(
			ReadU64(Bytes, 48) == 0x0102030405060708ULL));

		const uint8 ExpectedProtocol[16] =
		{
			0x11, 0x12, 0x13, 0x14,
			0x21, 0x22, 0x23, 0x24,
			0x31, 0x32, 0x33, 0x34,
			0x41, 0x42, 0x43, 0x44,
		};
		ASSERT_THAT(IsTrue(FMemory::Memcmp(
			Bytes.GetData() + 56, ExpectedProtocol, 16) == 0));

		FSeinSnapshotEnvelopeMetadata PrefixMetadata;
		ASSERT_THAT(IsTrue(FSeinSnapshotEnvelopeCodec::ParsePrefix(
			TConstArrayView<uint8>(Bytes.GetData(), PrefixBytes),
			PrefixMetadata,
			Error)));
		ASSERT_THAT(IsTrue(
			PrefixMetadata.DirectoryBytes == Metadata.DirectoryBytes
			&& PrefixMetadata.BodyBytes == Metadata.BodyBytes
			&& PrefixMetadata.AggregateStateRoot
				== Metadata.AggregateStateRoot
			&& PrefixMetadata.BodyDigest == Metadata.BodyDigest));

		FSeinSnapshotEnvelope Decoded;
		FSeinSnapshotEnvelopeMetadata DecodedMetadata;
		ASSERT_THAT(IsTrue(FSeinSnapshotEnvelopeCodec::Decode(
			Bytes, Decoded, DecodedMetadata, Error)));
		ASSERT_THAT(AreEqual(4, Decoded.Sections.Num()));
		ASSERT_THAT(IsTrue(
			Decoded.Sections[0].SectionId == TEXT("core.alpha")
			&& Decoded.Sections[1].SectionId == TEXT("core.bravo")
			&& Decoded.Sections[2].SectionId == TEXT("core.cache")
			&& Decoded.Sections[3].SectionId == TEXT("local.camera")));
		ASSERT_THAT(IsTrue(
			Decoded.Sections[0].Payload == TArray<uint8>{1, 2, 3}
			&& Decoded.Sections[1].Payload == TArray<uint8>{4, 5}
			&& Decoded.Sections[2].Payload.IsEmpty()
			&& Decoded.Sections[3].Payload == TArray<uint8>{9}));

		FSeinSnapshotEnvelope Permuted = Source;
		Permuted.Sections.Swap(0, 3);
		Permuted.Sections.Swap(1, 2);
		TArray<uint8> PermutedBytes;
		FSeinSnapshotEnvelopeMetadata PermutedMetadata;
		ASSERT_THAT(IsTrue(FSeinSnapshotEnvelopeCodec::Encode(
			Permuted, PermutedBytes, PermutedMetadata, Error)));
		ASSERT_THAT(IsTrue(PermutedBytes == Bytes));
	}

	TEST(SnapshotV17AggregateRootIncludesOnlyFutureAffectingSections,
		"SeinARTS.Unit.CoreEntity.SnapshotEnvelope")
	{
		const FSeinSnapshotEnvelope Baseline = MakeCanonicalFixture();
		TArray<uint8> BaselineBytes;
		FSeinSnapshotEnvelopeMetadata BaselineMetadata;
		FString Error;
		ASSERT_THAT(IsTrue(FSeinSnapshotEnvelopeCodec::Encode(
			Baseline, BaselineBytes, BaselineMetadata, Error)));

		FSeinSnapshotEnvelope LocalChanged = Baseline;
		FindSection(LocalChanged, TEXT("local.camera"))->Payload[0] ^= 0xff;
		TArray<uint8> LocalBytes;
		FSeinSnapshotEnvelopeMetadata LocalMetadata;
		ASSERT_THAT(IsTrue(FSeinSnapshotEnvelopeCodec::Encode(
			LocalChanged, LocalBytes, LocalMetadata, Error)));
		ASSERT_THAT(IsTrue(
			LocalMetadata.AggregateStateRoot
				== BaselineMetadata.AggregateStateRoot
			&& LocalMetadata.BodyDigest != BaselineMetadata.BodyDigest));
		FSeinSnapshotEnvelope DecodedLocal;
		FSeinSnapshotEnvelopeMetadata DecodedLocalMetadata;
		ASSERT_THAT(IsTrue(FSeinSnapshotEnvelopeCodec::Decode(
			LocalBytes, DecodedLocal, DecodedLocalMetadata, Error)));
		ASSERT_THAT(IsTrue(
			DecodedLocalMetadata.AggregateStateRoot
				== BaselineMetadata.AggregateStateRoot));

		FSeinSnapshotEnvelope DerivedChanged = Baseline;
		FSeinSnapshotEnvelopeSection* Derived =
			FindSection(DerivedChanged, TEXT("core.cache"));
		Derived->SchemaVersion++;
		Derived->SchemaDigest.D++;
		TArray<uint8> DerivedBytes;
		FSeinSnapshotEnvelopeMetadata DerivedMetadata;
		ASSERT_THAT(IsTrue(FSeinSnapshotEnvelopeCodec::Encode(
			DerivedChanged, DerivedBytes, DerivedMetadata, Error)));
		ASSERT_THAT(IsTrue(
			DerivedMetadata.AggregateStateRoot
				== BaselineMetadata.AggregateStateRoot
			&& DerivedMetadata.BodyDigest != BaselineMetadata.BodyDigest));

		FSeinSnapshotEnvelope AuthoritativeChanged = Baseline;
		FindSection(
			AuthoritativeChanged, TEXT("core.alpha"))->Payload[0] ^= 0xff;
		TArray<uint8> AuthoritativeBytes;
		FSeinSnapshotEnvelopeMetadata AuthoritativeMetadata;
		ASSERT_THAT(IsTrue(FSeinSnapshotEnvelopeCodec::Encode(
			AuthoritativeChanged,
			AuthoritativeBytes,
			AuthoritativeMetadata,
			Error)));
		ASSERT_THAT(IsTrue(
			AuthoritativeMetadata.AggregateStateRoot
				!= BaselineMetadata.AggregateStateRoot));
		// The changed payload, its stored leaf, and the body digest are all
		// internally valid. Restoring only the old prefix root must therefore
		// reach and fail the independent aggregate-root check.
		FMemory::Memcpy(
			AuthoritativeBytes.GetData() + AggregateDigestOffset,
			BaselineBytes.GetData() + AggregateDigestOffset,
			16);
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			AuthoritativeBytes,
			TEXT("aggregate state root"),
			Error)));

		FSeinSnapshotEnvelope ContinuationChanged = Baseline;
		FindSection(
			ContinuationChanged, TEXT("core.bravo"))->Payload[0] ^= 0xff;
		TArray<uint8> ContinuationBytes;
		FSeinSnapshotEnvelopeMetadata ContinuationMetadata;
		ASSERT_THAT(IsTrue(FSeinSnapshotEnvelopeCodec::Encode(
			ContinuationChanged,
			ContinuationBytes,
			ContinuationMetadata,
			Error)));
		ASSERT_THAT(IsTrue(
			ContinuationMetadata.AggregateStateRoot
				!= BaselineMetadata.AggregateStateRoot));

		FSeinSnapshotEnvelope InvalidDerived = Baseline;
		FindSection(
			InvalidDerived, TEXT("core.cache"))->Payload.Add(1);
		TArray<uint8> RejectedBytes{0xaa};
		FSeinSnapshotEnvelopeMetadata RejectedMetadata;
		RejectedMetadata.SnapshotTick = 999;
		ASSERT_THAT(IsFalse(FSeinSnapshotEnvelopeCodec::Encode(
			InvalidDerived, RejectedBytes, RejectedMetadata, Error)));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("must not carry payload"))
			&& RejectedBytes == TArray<uint8>{0xaa}
			&& RejectedMetadata.SnapshotTick == 999));
	}

	TEST(SnapshotV17EmptyEnvelopeRoundTripsWithoutExposingOutputsOnFailure,
		"SeinARTS.Unit.CoreEntity.SnapshotEnvelope")
	{
		FSeinSnapshotEnvelope Empty;
		Empty.SnapshotTick = 0;
		Empty.CommandProtocolDigest = MakeDigest(101);
		Empty.CompatibilityDigest = MakeDigest(102);

		TArray<uint8> Bytes;
		FSeinSnapshotEnvelopeMetadata Metadata;
		FString Error;
		ASSERT_THAT(IsTrue(FSeinSnapshotEnvelopeCodec::Encode(
			Empty, Bytes, Metadata, Error)));
		ASSERT_THAT(IsTrue(
			Bytes.Num() == PrefixBytes
			&& Metadata.SectionCount == 0
			&& Metadata.DirectoryBytes == 0
			&& Metadata.BodyBytes == 0
			&& Metadata.AggregateStateRoot.IsValid()
			&& Metadata.BodyDigest.IsValid()));

		FSeinSnapshotEnvelope Decoded;
		FSeinSnapshotEnvelopeMetadata DecodedMetadata;
		ASSERT_THAT(IsTrue(FSeinSnapshotEnvelopeCodec::Decode(
			Bytes, Decoded, DecodedMetadata, Error)));
		ASSERT_THAT(IsTrue(
			Decoded.SnapshotTick == 0
			&& Decoded.Sections.IsEmpty()
			&& DecodedMetadata.BodyDigest == Metadata.BodyDigest));
	}

	TEST(SnapshotV17PrefixRejectsHostileBoundsBeforeBodyDecode,
		"SeinARTS.Unit.CoreEntity.SnapshotEnvelope.Security")
	{
		const FSeinSnapshotEnvelope Source = MakeCanonicalFixture();
		TArray<uint8> Clean;
		FSeinSnapshotEnvelopeMetadata CleanMetadata;
		FString Error;
		ASSERT_THAT(IsTrue(FSeinSnapshotEnvelopeCodec::Encode(
			Source, Clean, CleanMetadata, Error)));

		FSeinSnapshotEnvelopeMetadata SentinelMetadata;
		SentinelMetadata.SnapshotTick = 888;
		TArray<uint8> Truncated;
		Truncated.Append(Clean.GetData(), PrefixBytes - 1);
		ASSERT_THAT(IsFalse(FSeinSnapshotEnvelopeCodec::ParsePrefix(
			Truncated, SentinelMetadata, Error)));
		ASSERT_THAT(AreEqual(static_cast<int64>(888), SentinelMetadata.SnapshotTick));

		TArray<uint8> Bad = Clean;
		Bad[0] = 'X';
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			Bad, TEXT("magic mismatch"), Error)));

		Bad = Clean;
		WriteU32(
			Bad,
			8,
			FSeinSnapshotEnvelopeCodec::WireFormatVersion + 1);
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			Bad, TEXT("version or prefix size"), Error)));

		Bad = Clean;
		WriteU32(
			Bad,
			12,
			FSeinSnapshotEnvelopeCodec::SnapshotSemanticsVersion + 1);
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			Bad, TEXT("version or prefix size"), Error)));

		Bad = Clean;
		WriteU32(Bad, 16, PrefixBytes + 1);
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			Bad, TEXT("version or prefix size"), Error)));

		Bad = Clean;
		WriteU32(
			Bad,
			SectionCountOffset,
			FSeinSnapshotEnvelopeCodec::MaxSections + 1);
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			Bad, TEXT("section count exceeds"), Error)));

		Bad = Clean;
		WriteU64(
			Bad,
			DirectoryBytesOffset,
			FSeinSnapshotEnvelopeCodec::MaxDirectoryBytes + 1);
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			Bad, TEXT("directory exceeds"), Error)));

		Bad = Clean;
		WriteU64(
			Bad,
			BodyBytesOffset,
			FSeinSnapshotEnvelopeCodec::MaxBodyBytes + 1);
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			Bad, TEXT("body exceeds"), Error)));

		Bad = Clean;
		WriteU64(Bad, HeaderFlagsOffset, 1);
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			Bad, TEXT("header flags"), Error)));

		Bad = Clean;
		Bad.Add(0);
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			Bad, TEXT("exactly match file length"), Error)));
	}

	TEST(SnapshotV17RejectsNoncanonicalDirectoriesAndDigestForgeries,
		"SeinARTS.Unit.CoreEntity.SnapshotEnvelope.Security")
	{
		const FSeinSnapshotEnvelope Source = MakeCanonicalFixture();
		TArray<uint8> Clean;
		FSeinSnapshotEnvelopeMetadata CleanMetadata;
		FString Error;
		ASSERT_THAT(IsTrue(FSeinSnapshotEnvelopeCodec::Encode(
			Source, Clean, CleanMetadata, Error)));
		const FEntryOffsets First = GetEntryOffsets(Clean, PrefixBytes);
		const FEntryOffsets Second =
			GetEntryOffsets(Clean, First.NextEntry);

		// The exact-body digest is checked before the invalid directory ID can
		// be interpreted.
		TArray<uint8> Bad = Clean;
		Bad[First.Id] = 'C';
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			Bad, TEXT("exact-body digest"), Error)));

		RewriteBodyDigest(Bad);
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			Bad, TEXT("lowercase-ASCII"), Error)));

		Bad = Clean;
		Bad[First.Role] = 99;
		RewriteBodyDigest(Bad);
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			Bad, TEXT("role is unknown"), Error)));

		Bad = Clean;
		Bad[First.Codec] = 99;
		RewriteBodyDigest(Bad);
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			Bad, TEXT("codec is unknown"), Error)));

		Bad = Clean;
		WriteU16(Bad, First.Flags, 1);
		RewriteBodyDigest(Bad);
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			Bad, TEXT("entry flags"), Error)));

		ASSERT_THAT(AreEqual(First.IdBytes, Second.IdBytes));
		Bad = Clean;
		FMemory::Memcpy(
			Bad.GetData() + Second.Id,
			Bad.GetData() + First.Id,
			First.IdBytes);
		RewriteBodyDigest(Bad);
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			Bad, TEXT("duplicate or not canonically sorted"), Error)));

		const uint64 OriginalPayloadOffset =
			ReadU64(Clean, First.PayloadOffset);
		Bad = Clean;
		WriteU64(Bad, First.PayloadOffset, OriginalPayloadOffset + 1);
		RewriteBodyDigest(Bad);
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			Bad, TEXT("not contiguous"), Error)));

		Bad = Clean;
		WriteU64(Bad, First.PayloadOffset, OriginalPayloadOffset - 1);
		RewriteBodyDigest(Bad);
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			Bad, TEXT("not contiguous"), Error)));

		Bad = Clean;
		WriteU64(Bad, First.PayloadBytes, MAX_uint64);
		RewriteBodyDigest(Bad);
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			Bad, TEXT("payload exceeds its bound"), Error)));

		// Re-signing the body alone cannot forge an individual leaf.
		Bad = Clean;
		Bad.Last() ^= 0xff;
		RewriteBodyDigest(Bad);
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			Bad, TEXT("leaf digest mismatch"), Error)));

		// The body and leaves remain valid, but the semantic root is independently
		// recomputed from authoritative and continuation leaves.
		Bad = Clean;
		Bad[AggregateDigestOffset] ^= 0x01;
		ASSERT_THAT(IsTrue(DecodeFailsWith(
			Bad, TEXT("aggregate state root"), Error)));
	}
}

#include "CQTest.h"

#include "SeinReplayJournalFormat.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		FSeinMatchBootstrapReceipt MakeJournalReceipt(
			const FGuid& ContractDigest)
		{
			FSeinMatchBootstrapReceipt Receipt;
			Receipt.ContractDigest = ContractDigest;
			Receipt.SimulationContentDigest = FGuid(10, 11, 12, 13);
			Receipt.StateContractDigest = FGuid(20, 21, 22, 23);
			Receipt.PlanDigest = FGuid(30, 31, 32, 33);
			Receipt.InitialStateDigest = FGuid(40, 41, 42, 43);
			return Receipt;
		}

		void WriteUInt32BE(TArray<uint8>& Bytes, int32 Offset, uint32 Value)
		{
			check(Offset >= 0 && Offset + 4 <= Bytes.Num());
			Bytes[Offset] = static_cast<uint8>(Value >> 24);
			Bytes[Offset + 1] = static_cast<uint8>(Value >> 16);
			Bytes[Offset + 2] = static_cast<uint8>(Value >> 8);
			Bytes[Offset + 3] = static_cast<uint8>(Value);
		}
	}

	TEST(ReplayV9PrefixRoundTripsAndRejectsTampering,
		"SeinARTS.Unit.Network.ReplayFormat.V9")
	{
		const FGuid CommandDigest(1, 2, 3, 4);
		const FGuid MatchDigest(5, 6, 7, 8);
		const FSeinMatchBootstrapReceipt Receipt =
			MakeJournalReceipt(MatchDigest);
		const FGuid JournalID(50, 51, 52, 53);
		TArray<uint8> Bytes;
		SeinReplayJournalFormat::FPrefix Built;
		FString Error;
		ASSERT_THAT(IsTrue(SeinReplayJournalFormat::BuildPrefix(
			CommandDigest,
			MatchDigest,
			Receipt,
			static_cast<int32>(0x89abcdefu),
			JournalID,
			Bytes,
			Built,
			Error)));
		ASSERT_THAT(AreEqual(
			SeinReplayJournalFormat::PrefixBytes, Bytes.Num()));

		SeinReplayJournalFormat::FPrefix Parsed;
		ASSERT_THAT(IsTrue(SeinReplayJournalFormat::ParsePrefix(
			Bytes, Parsed, Error)));
		ASSERT_THAT(IsTrue(Parsed.CommandProtocolDigest == CommandDigest));
		ASSERT_THAT(IsTrue(Parsed.MatchSettingsDigest == MatchDigest));
		ASSERT_THAT(IsTrue(Parsed.BootstrapReceipt == Receipt));
		ASSERT_THAT(IsTrue(Parsed.JournalID == JournalID));
		ASSERT_THAT(IsTrue(Parsed.PrefixDigest == Built.PrefixDigest));

		Bytes[120] ^= 1u;
		SeinReplayJournalFormat::FPrefix Preserved = Parsed;
		ASSERT_THAT(IsFalse(SeinReplayJournalFormat::ParsePrefix(
			Bytes, Parsed, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("digest mismatch"))));
		ASSERT_THAT(IsTrue(Parsed.PrefixDigest == Preserved.PrefixDigest));
	}

	TEST(ReplayV9TurnFramesBindChainMetadataAndPayload,
		"SeinARTS.Unit.Network.ReplayFormat.V9")
	{
		using namespace SeinReplayJournalFormat;
		FTurnRecord First;
		First.TurnId = 3;
		First.OpaqueCommands.Bytes = {1, 2, 3};
		FTurnRecord Second;
		Second.TurnId = 4;
		Second.OpaqueCommands.Bytes = {4, 5};
		TArray<FTurnRecord> Records{First, Second};
		TArray<uint8> Payload;
		FString Error;
		ASSERT_THAT(IsTrue(EncodeTurnBatch(Records, Payload, Error)));

		const FGuid Previous(60, 61, 62, 63);
		TArray<uint8> FrameBytes;
		FFrameHeader Built;
		ASSERT_THAT(IsTrue(BuildFrame(
			EFrameType::TurnBatch,
			/*Flags=*/0,
			/*Sequence=*/7,
			/*FirstTurn=*/3,
			/*LastTurn=*/4,
			/*TimelineTick=*/12,
			Previous,
			Payload,
			FrameBytes,
			Built,
			Error)));

		FFrameHeader Parsed;
		ASSERT_THAT(IsTrue(ValidateFrame(FrameBytes, Parsed, Error)));
		ASSERT_THAT(AreEqual(static_cast<uint64>(7), Parsed.Sequence));
		ASSERT_THAT(AreEqual(3, Parsed.FirstTurn));
		ASSERT_THAT(AreEqual(4, Parsed.LastTurn));
		ASSERT_THAT(IsTrue(Parsed.PreviousDigest == Previous));

		TArray<FTurnRecord> Decoded;
		ASSERT_THAT(IsTrue(DecodeTurnBatch(
			MakeArrayView(
				FrameBytes.GetData() + FrameHeaderBytes,
				FrameBytes.Num() - FrameHeaderBytes),
			Decoded,
			Error)));
		ASSERT_THAT(AreEqual(2, Decoded.Num()));
		ASSERT_THAT(AreEqual(3, Decoded[0].TurnId));
		ASSERT_THAT(AreEqual(4, Decoded[1].TurnId));

		TArray<uint8> MetadataTampered = FrameBytes;
		// Sequence begins at byte 8 and is part of the digest-bound header.
		MetadataTampered[15] ^= 1u;
		ASSERT_THAT(IsFalse(
			ValidateFrame(MetadataTampered, Parsed, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("digest mismatch"))));

		FTurnRecord Third;
		Third.TurnId = 5;
		Third.OpaqueCommands.Bytes = {6};
		TArray<FTurnRecord> NextRecords{Third};
		TArray<uint8> NextPayload;
		ASSERT_THAT(IsTrue(
			EncodeTurnBatch(NextRecords, NextPayload, Error)));
		TArray<uint8> NextFrameBytes;
		FFrameHeader NextBuilt;
		ASSERT_THAT(IsTrue(BuildFrame(
			EFrameType::TurnBatch,
			0,
			8,
			5,
			5,
			15,
			Built.CurrentDigest,
			NextPayload,
			NextFrameBytes,
			NextBuilt,
			Error)));
		FFrameHeader NextParsed;
		ASSERT_THAT(IsTrue(
			ValidateFrame(NextFrameBytes, NextParsed, Error)));
		ASSERT_THAT(IsTrue(
			NextParsed.PreviousDigest == Built.CurrentDigest));

		FrameBytes.Last() ^= 1u;
		ASSERT_THAT(IsFalse(ValidateFrame(FrameBytes, Parsed, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("digest mismatch"))));
	}

	TEST(ReplayV9FrameHeaderRejectsHostilePayloadBoundsBeforeAllocation,
		"SeinARTS.Unit.Network.ReplayFormat.V9.Security")
	{
		using namespace SeinReplayJournalFormat;
		FTurnRecord Record;
		Record.TurnId = 3;
		Record.OpaqueCommands.Bytes = {1};
		TArray<uint8> Payload;
		FString Error;
		TArray<FTurnRecord> Records{Record};
		ASSERT_THAT(IsTrue(EncodeTurnBatch(Records, Payload, Error)));
		TArray<uint8> FrameBytes;
		FFrameHeader Header;
		ASSERT_THAT(IsTrue(BuildFrame(
			EFrameType::TurnBatch,
			0,
			0,
			3,
			3,
			9,
			FGuid(70, 71, 72, 73),
			Payload,
			FrameBytes,
			Header,
			Error)));

		// PayloadBytes begins at byte 28 of the fixed frame header.
		WriteUInt32BE(FrameBytes, 28, MAX_uint32);
		FFrameHeader Preserved = Header;
		ASSERT_THAT(IsFalse(ParseFrameHeader(
			MakeArrayView(FrameBytes.GetData(), FrameHeaderBytes),
			Header,
			Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("TurnBatch"))));
		ASSERT_THAT(IsTrue(Header.CurrentDigest == Preserved.CurrentDigest));
	}

	TEST(ReplayV9FrontierRequiresAnExactContiguousAppliedRange,
		"SeinARTS.Unit.Network.ReplayFormat.V9")
	{
		using namespace SeinReplayJournalFormat;
		FFrontier Frontier;
		Frontier.EndTick = 30;
		Frontier.FirstAppliedTurn = 3;
		Frontier.LastAppliedTurn = 10;
		Frontier.AppliedTurnCount = 8;
		TArray<uint8> Bytes;
		FString Error;
		ASSERT_THAT(IsTrue(EncodeFrontier(Frontier, Bytes, Error)));
		FFrontier Decoded;
		ASSERT_THAT(IsTrue(DecodeFrontier(Bytes, Decoded, Error)));
		ASSERT_THAT(AreEqual(30, Decoded.EndTick));
		ASSERT_THAT(AreEqual(static_cast<uint32>(8), Decoded.AppliedTurnCount));

		Frontier.AppliedTurnCount = 7;
		ASSERT_THAT(IsFalse(EncodeFrontier(Frontier, Bytes, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("contiguous range"))));
	}
}

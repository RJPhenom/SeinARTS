#include "CQTest.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Input/SeinCommandSchemaRegistry.h"
#include "SeinNetCommandWireCodec.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinCommandSchemaTestTypes.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		FSeinCommandSchemaDescriptor MakePingSchema()
		{
			FSeinCommandSchemaDescriptor Schema;
			Schema.StableSchemaId = TEXT("SeinFrameworkTests.NetWire.Ping.V1");
			Schema.CommandType = SeinARTSTags::Command_Type_Ping;
			Schema.SchemaVersion = 1;
			Schema.MaxEntityListEntries = 0;
			Schema.MaxTargeterPoints = 0;
			Schema.MaxPayloadBytes = 0;
			Schema.MaxPayloadAggregateElements = 0;
			return Schema;
		}
	}

	TEST(CanonicalBatchCostExcludesNativeContainerLayout,
		"SeinARTS.Unit.Network.Protocol.Security")
	{
			const FSeinCommandSchemaDescriptor Schema = MakePingSchema();
			auto FindSchema = [&Schema](
				FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
			{
				if (Type != Schema.CommandType || Version != Schema.SchemaVersion)
					return false;
				Out = Schema;
				return true;
			};

			FSeinCommand Command;
			Command.CommandType = Schema.CommandType;
			Command.SchemaVersion = Schema.SchemaVersion;
			const TArray<FSeinCommand> Commands{ Command };
			FSeinCommandSubmissionDraft Draft;
			Draft.Command = Command;
			const TArray<FSeinCommandSubmissionDraft> Drafts{ Draft };

			FString Error;
			FSeinOpaqueCommandBatch CommandBatch;
			FSeinOpaqueCommandBatch DraftBatch;
			FSeinWireCost CommandEncodeCost;
			FSeinWireCost DraftEncodeCost;
			ASSERT_THAT(IsTrue(FSeinNetCommandWireCodec::EncodeCommandsWithCost(
				Commands, 1, FindSchema, CommandBatch, Error, CommandEncodeCost)));
			ASSERT_THAT(IsTrue(FSeinNetCommandWireCodec::EncodeDraftsWithCost(
				Drafts, 1, FindSchema, DraftBatch, Error, DraftEncodeCost)));

			const uint64 ExpectedCommandCost = static_cast<uint64>(CommandBatch.Bytes.Num())
				+ FSeinWireCost::CanonicalBytesPerLogicalElement;
			const uint64 ExpectedDraftCost = static_cast<uint64>(DraftBatch.Bytes.Num())
				+ FSeinWireCost::CanonicalBytesPerLogicalElement;
			ASSERT_THAT(AreEqual(ExpectedCommandCost, CommandEncodeCost.CanonicalCostBytes));
			ASSERT_THAT(AreEqual(ExpectedDraftCost, DraftEncodeCost.CanonicalCostBytes));
			ASSERT_THAT(AreEqual(
				CommandEncodeCost.CanonicalCostBytes,
				DraftEncodeCost.CanonicalCostBytes));

			TArray<FSeinCommand> Decoded;
			FSeinWireCost DecodeCost;
			ASSERT_THAT(IsTrue(FSeinNetCommandWireCodec::DecodeCommandsWithCost(
				CommandBatch, 1, FindSchema, Decoded, Error, DecodeCost)));
			ASSERT_THAT(AreEqual(1, Decoded.Num()));
			ASSERT_THAT(AreEqual(
				CommandEncodeCost.CanonicalCostBytes,
				DecodeCost.CanonicalCostBytes));
	}

	TEST(BrokerOrderV2WireRoundTripPreservesFrozenDestinations,
		"SeinARTS.Unit.Network.Protocol")
	{
		FSeinCommandSchemaDescriptor Schema;
		ASSERT_THAT(IsTrue(FSeinCommandSchemaRegistry::FindSchema(
			SeinARTSTags::Command_Type_BrokerOrder,
			SeinBrokerOrderProtocol::SchemaVersion,
			Schema)));
		auto FindSchema = [](FGameplayTag Type, int32 Version,
			FSeinCommandSchemaDescriptor& Out)
		{
			return FSeinCommandSchemaRegistry::FindSchema(
				Type, Version, Out);
		};

		FSeinFrozenDestination Destination;
		Destination.Member = FSeinEntityHandle(17, 3);
		Destination.WorldPosition = FFixedVector(
			FFixedPoint::FromInt(123),
			FFixedPoint::FromInt(-456),
			FFixedPoint::FromInt(7));
		Destination.FootprintRadius = FFixedPoint::FromInt(40);
		Destination.bReserveFootprint = true;
		Destination.SourceEntity = FSeinEntityHandle(22, 4);
		Destination.SourceIndex = 9;

		FSeinBrokerOrderPayload Payload;
		Payload.DestinationArtifact.Add(Destination);
		FSeinCommand Command;
		Command.PlayerID = FSeinPlayerID(1);
		Command.CommandType = SeinARTSTags::Command_Type_BrokerOrder;
		Command.SchemaVersion = SeinBrokerOrderProtocol::SchemaVersion;
		Command.EntityList.Add(Destination.Member);
		Command.Payload = FInstancedStruct::Make(Payload);

		FSeinOpaqueCommandBatch Batch;
		FString Error;
		ASSERT_THAT(IsTrue(FSeinNetCommandWireCodec::EncodeCommands(
			MakeArrayView(&Command, 1), 1, FindSchema, Batch, Error)));
		TArray<FSeinCommand> Decoded;
		ASSERT_THAT(IsTrue(FSeinNetCommandWireCodec::DecodeCommands(
			Batch, 1, FindSchema, Decoded, Error)));
		ASSERT_THAT(AreEqual(1, Decoded.Num()));
		const FSeinBrokerOrderPayload* DecodedPayload =
			Decoded[0].Payload.GetPtr<FSeinBrokerOrderPayload>();
		ASSERT_THAT(IsNotNull(DecodedPayload));
		ASSERT_THAT(AreEqual(1, DecodedPayload->DestinationArtifact.Num()));
		const FSeinFrozenDestination& DecodedDestination =
			DecodedPayload->DestinationArtifact[0];
		ASSERT_THAT(IsTrue(DecodedDestination.Member == Destination.Member));
		ASSERT_THAT(IsTrue(
			DecodedDestination.WorldPosition == Destination.WorldPosition));
		ASSERT_THAT(IsTrue(
			DecodedDestination.FootprintRadius == Destination.FootprintRadius));
		ASSERT_THAT(IsTrue(DecodedDestination.bReserveFootprint));
		ASSERT_THAT(IsTrue(
			DecodedDestination.SourceEntity == Destination.SourceEntity));
		ASSERT_THAT(AreEqual(
			Destination.SourceIndex, DecodedDestination.SourceIndex));
	}

	TEST(NativeCeilingRejectsBeforeOutputMutation,
		"SeinARTS.Unit.Network.Protocol.Security")
	{
			const FSeinCommandSchemaDescriptor Schema = MakePingSchema();
			auto FindSchema = [&Schema](
				FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
			{
				if (Type != Schema.CommandType || Version != Schema.SchemaVersion)
					return false;
				Out = Schema;
				return true;
			};

			FSeinCommand Command;
			Command.CommandType = Schema.CommandType;
			Command.SchemaVersion = Schema.SchemaVersion;
			FSeinOpaqueCommandBatch Batch;
			FString Error;
			FSeinWireCost EncodeCost;
			ASSERT_THAT(IsTrue(FSeinNetCommandWireCodec::EncodeCommandsWithCost(
				MakeArrayView(&Command, 1), 1, FindSchema, Batch, Error, EncodeCost)));

			FSeinCommand Preserved;
			Preserved.Tick = 777;
			TArray<FSeinCommand> Destination{ Preserved };
			FSeinWireCost RejectedCost;
			RejectedCost.CanonicalCostBytes = 123;
			RejectedCost.NativeAllocationBytes = 456;
			ASSERT_THAT(IsFalse(FSeinNetCommandWireCodec::DecodeCommandsWithCost(
				Batch, 1, FindSchema, Destination, Error, RejectedCost,
				/*NativeAllocationLimit=*/1)));
			ASSERT_THAT(AreEqual(1, Destination.Num()));
			ASSERT_THAT(AreEqual(777, Destination[0].Tick));
			ASSERT_THAT(AreEqual(static_cast<uint64>(0), RejectedCost.CanonicalCostBytes));
			ASSERT_THAT(IsTrue(Error.Contains(TEXT("native-allocation"))));
	}

	TEST(RawNamePayloadUsesFrozenCatalogAcrossOpaqueBatch,
		"SeinARTS.Unit.Network.Protocol.Security")
	{
		FSeinCommandSchemaDescriptor Schema = MakePingSchema();
		Schema.PayloadStruct =
			FSeinCommandSchemaIdentityWireTestPayload::StaticStruct();
		Schema.MaxPayloadBytes = 128;
		Schema.MaxPayloadAggregateElements = 8;
		TArray<FName> CanonicalNames;
		FString NameManifest;
		const TArray<FName> AuthoredNames{ TEXT("OpaqueBatchIdentifier") };
		SeinBuildCanonicalWireNameCatalog(
			AuthoredNames, CanonicalNames, NameManifest);
		Schema.AllowedPayloadNames = CanonicalNames;
		auto FindSchema = [&Schema](
			FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
		{
			if (Type != Schema.CommandType || Version != Schema.SchemaVersion)
				return false;
			Out = Schema;
			return true;
		};

		FSeinCommandSchemaIdentityWireTestPayload Payload;
		Payload.Name = TEXT("OPAQUEBATCHIDENTIFIER");
		FSeinCommand Command;
		Command.CommandType = Schema.CommandType;
		Command.SchemaVersion = Schema.SchemaVersion;
		Command.Payload = FInstancedStruct::Make(Payload);
		FSeinOpaqueCommandBatch Batch;
		FString Error;
		ASSERT_THAT(IsTrue(FSeinNetCommandWireCodec::EncodeCommands(
			MakeArrayView(&Command, 1), 1, FindSchema, Batch, Error)));
		TArray<FSeinCommand> Decoded;
		ASSERT_THAT(IsTrue(FSeinNetCommandWireCodec::DecodeCommands(
			Batch, 1, FindSchema, Decoded, Error)));
		ASSERT_THAT(AreEqual(1, Decoded.Num()));
		ASSERT_THAT(AreEqual(
			CanonicalNames[0],
			Decoded[0].Payload.Get<FSeinCommandSchemaIdentityWireTestPayload>().Name));
	}
}

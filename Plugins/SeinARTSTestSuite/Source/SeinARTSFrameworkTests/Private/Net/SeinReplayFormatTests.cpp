#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Containers/Ticker.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SeinReplayFormat.h"
#include "SeinReplayReader.h"
#include "SeinReplayWriter.h"
#include "SeinReplayWireCodec.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinReplayTestTypes.h"
#include "TestTypes/SeinCommandSchemaTestTypes.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		const FGuid ReplayTestPlanDigest(
			0x81000000, 0x82000000, 0x83000000, 1);
		const FGuid ReplayTestAuthorizationContext(
			0x91000000, 0x92000000, 0x93000000, 1);
		const FGuid ReplayTestSimulationContentDigest(
			0x94000000, 0x95000000, 0x96000000, 1);

		void StampSyntheticBootstrapReceipt(FSeinReplayHeader& Header)
		{
			if (!Header.MatchSettingsDigest.IsValid())
			{
				SeinCanonicalizeAndDigestMatchSettings(
					Header.SettingsSnapshot,
					Header.MatchSettingsDigest,
					nullptr);
			}
			Header.BootstrapReceipt.ContractDigest = Header.MatchSettingsDigest;
			if (!Header.BootstrapReceipt.SimulationContentDigest.IsValid())
			{
				Header.BootstrapReceipt.SimulationContentDigest =
					ReplayTestSimulationContentDigest;
			}
			Header.BootstrapReceipt.StateContractDigest =
				FGuid(0x87000000, 0x88000000, 0x89000000, 1);
			Header.BootstrapReceipt.PlanDigest = ReplayTestPlanDigest;
			Header.BootstrapReceipt.InitialStateDigest =
				FGuid(0x84000000, 0x85000000, 0x86000000, 1);
		}

		void BindReplayTestMaterializer(USeinWorldSubsystem& World)
		{
			World.MatchBootstrapMaterializer.BindLambda(
				[&World](
					const FSeinMatchSettings& Settings,
					const FGuid& AuthorizationContext,
					FSeinMatchBootstrapReceipt& OutReceipt,
					FString& OutError)
				{
					FSeinMatchSettings CanonicalSettings = Settings;
					FGuid ContractDigest;
					if (!SeinCanonicalizeAndDigestMatchSettings(
							CanonicalSettings, ContractDigest, nullptr))
					{
						OutError = TEXT("Replay test materializer could not digest settings.");
						return false;
					}
					if (World.GetMatchBootstrapState()
							!= ESeinMatchBootstrapState::Applying
						|| World.GetMatchBootstrapAuthorizationContextDigest()
							!= AuthorizationContext)
					{
						OutError = TEXT(
							"Core did not open the replay test bootstrap transaction.");
						return false;
					}

					World.RegisterFactionsFromSettings();
					World.StartMatch(CanonicalSettings);
					if (World.GetMatchState() != ESeinMatchState::Starting
						|| World.GetMatchSettingsDigest() != ContractDigest)
					{
						OutError = TEXT("Replay test materializer could not install settings.");
						return false;
					}

					for (const FSeinMatchSlot& Slot : CanonicalSettings.Slots)
					{
						if (Slot.State != ESeinSlotState::Human
							&& Slot.State != ESeinSlotState::AI)
						{
							continue;
						}
						const FSeinPlayerID PlayerID(
							static_cast<uint8>(Slot.SlotIndex));
						World.RegisterPlayer(PlayerID, Slot.FactionID, Slot.TeamID);
						const FSeinPlayerState* Player = World.GetPlayerState(PlayerID);
						if (!Player || Player->FactionID != Slot.FactionID
							|| Player->TeamID != Slot.TeamID)
						{
							OutError = TEXT("Replay test materializer could not register a player.");
							return false;
						}
					}

					return World.SealLocalMatchBootstrap(
						ReplayTestPlanDigest, OutReceipt, OutError);
				});
		}

		FSeinReplayHeader MakeReplayHeaderWithPlayerOne()
		{
			FSeinReplayHeader Header;
			FSeinMatchSlot& Slot = Header.SettingsSnapshot.Slots.AddDefaulted_GetRef();
			Slot.SlotIndex = 1;
			Slot.State = ESeinSlotState::Human;
			return Header;
		}

		FSeinReplayTurnRecord MakeCanonicalTurn(int32 TurnId, int32 TicksPerTurn)
		{
			FSeinReplayTurnRecord Turn;
			Turn.TurnId = TurnId;
			FSeinCommand& Command = Turn.Commands.AddDefaulted_GetRef();
			Command.PlayerID = FSeinPlayerID(1);
			Command.IssuerKind = ESeinCommandIssuerKind::Player;
			Command.Tick = TurnId * TicksPerTurn;
			return Turn;
		}

		FSeinReplayTurnRecord MakeEmptyTurn(int32 TurnId)
		{
			FSeinReplayTurnRecord Turn;
			Turn.TurnId = TurnId;
			return Turn;
		}

		void WriteUInt32At(
			TArray<uint8>& Bytes,
			int32 Offset,
			uint32 Value)
		{
			check(Offset >= 0 && Offset + 4 <= Bytes.Num());
			Bytes[Offset] = static_cast<uint8>(Value >> 24);
			Bytes[Offset + 1] = static_cast<uint8>(Value >> 16);
			Bytes[Offset + 2] = static_cast<uint8>(Value >> 8);
			Bytes[Offset + 3] = static_cast<uint8>(Value);
		}

		void WriteGuidAt(
			TArray<uint8>& Bytes,
			int32 Offset,
			const FGuid& Value)
		{
			WriteUInt32At(Bytes, Offset, Value.A);
			WriteUInt32At(Bytes, Offset + 4, Value.B);
			WriteUInt32At(Bytes, Offset + 8, Value.C);
			WriteUInt32At(Bytes, Offset + 12, Value.D);
		}

		FSeinReplayHeader MakeExecutableHeader(
			USeinWorldSubsystem& IdentityWorld,
			const FSeinMatchSettings& Settings = FSeinMatchSettings())
		{
			FSeinReplayHeader Header;
			SeinReplayCompatibility::StampCurrent(
				Header, IdentityWorld.GetWorld());
			Header.CommandProtocolDigest =
				IdentityWorld.GetCommandProtocolDigest();
			Header.ConfigFingerprint = IdentityWorld.GetConfigFingerprint();
			Header.SettingsSnapshot = Settings;
			SeinCanonicalizeAndDigestMatchSettings(
				Header.SettingsSnapshot, Header.MatchSettingsDigest, nullptr);
			for (const FSeinMatchSlot& Slot : Header.SettingsSnapshot.Slots)
			{
				if (Slot.State != ESeinSlotState::Human
					&& Slot.State != ESeinSlotState::AI)
				{
					continue;
				}
				FSeinPlayerRegistration& Player =
					Header.Players.AddDefaulted_GetRef();
				Player.PlayerID = FSeinPlayerID(
					static_cast<uint8>(Slot.SlotIndex));
				Player.FactionID = Slot.FactionID;
				Player.TeamID = Slot.TeamID;
				Player.bIsAI = Slot.State == ESeinSlotState::AI;
			}

			// Playback must compare against a receipt from an independently
			// materialized world. Reusing the target would consume its one-shot
			// bootstrap before the reader gets to prove its own result.
			FActorTestSpawner SourceSpawner;
			USeinWorldSubsystem* SourceWorld =
				SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			if (SourceWorld)
			{
				BindReplayTestMaterializer(*SourceWorld);
				FString Error;
				FSeinMatchBootstrapAuthorityHandle Authority;
				if (SourceWorld->ClaimMatchBootstrapAuthority(
						TEXT("SeinFrameworkTests.ReplayHeaderSource"),
						SourceWorld,
						Authority,
						Error)
					&& SourceWorld->SeedSimRandom(
						Authority, Header.RandomSeed, Error))
				{
					SourceWorld->EnsureMatchBootstrapLocallyReady(
						Authority,
						Header.SettingsSnapshot,
						ReplayTestAuthorizationContext,
						Header.BootstrapReceipt,
						Error);
				}
			}
			Header.RecordedAt = FDateTime::UtcNow();
			return Header;
		}

		void StampUnitReplayIdentity(FSeinReplayHeader& Header)
		{
			Header.FrameworkVersion =
				SeinReplayCompatibility::GetFrameworkVersion();
			Header.GameVersion = SeinReplayCompatibility::GetGameVersion();
		}

		struct FScopedReplayFile
		{
			FString Path;

			~FScopedReplayFile()
			{
				if (!Path.IsEmpty())
				{
					IFileManager::Get().Delete(*Path, false, true);
				}
			}
		};
	}

	TEST(ReplayV8PrefixRoundTripsAndProtectsBody, "SeinARTS.Unit.Network.ReplayFormat")
	{
		const FGuid CommandDigest(1, 2, 3, 4);
		const FGuid MatchDigest(5, 6, 7, 8);
		const int32 ConfigFingerprint = static_cast<int32>(0x89abcdefu);
		const TArray<uint8> Body{10, 20, 30, 40};
		FSeinReplayHeader ReceiptHeader;
		ReceiptHeader.MatchSettingsDigest = MatchDigest;
		StampSyntheticBootstrapReceipt(ReceiptHeader);
		TArray<uint8> PrefixBytes;
		FString Error;
		ASSERT_THAT(IsTrue(SeinReplayFormat::BuildPrefix(
			CommandDigest,
			MatchDigest,
			ReceiptHeader.BootstrapReceipt,
			ConfigFingerprint,
			Body,
			PrefixBytes,
			Error)));
		ASSERT_THAT(AreEqual(
			SeinReplayFormat::PrefixBytes, PrefixBytes.Num()));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>('8'), PrefixBytes[7]));

		TArray<uint8> FileBytes = PrefixBytes;
		FileBytes.Append(Body);
		SeinReplayFormat::FPrefix Parsed;
		ASSERT_THAT(IsTrue(SeinReplayFormat::ParsePrefix(
			FileBytes, Parsed, Error)));
		ASSERT_THAT(IsTrue(Parsed.CommandProtocolDigest == CommandDigest));
		ASSERT_THAT(IsTrue(Parsed.MatchSettingsDigest == MatchDigest));
		ASSERT_THAT(IsTrue(
			Parsed.BootstrapReceipt == ReceiptHeader.BootstrapReceipt));
		ASSERT_THAT(AreEqual(ConfigFingerprint, Parsed.ConfigFingerprint));
		ASSERT_THAT(AreEqual(static_cast<uint64>(Body.Num()), Parsed.BodyBytes));

		FileBytes.Last() ^= 0xff;
		ASSERT_THAT(IsFalse(SeinReplayFormat::ParsePrefix(
			FileBytes, Parsed, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("checksum"))));
	}

	TEST(ReplayV8AuthorizationIdentityCoversSimulationContent,
		"SeinARTS.Unit.Network.ReplayFormat")
	{
		FSeinReplayHeader Header;
		Header.CommandProtocolDigest = FGuid(1, 2, 3, 4);
		ASSERT_THAT(IsTrue(SeinCanonicalizeAndDigestMatchSettings(
			Header.SettingsSnapshot, Header.MatchSettingsDigest, nullptr)));
		StampSyntheticBootstrapReceipt(Header);
		StampUnitReplayIdentity(Header);
		Header.MapIdentifier = TEXT("/Game/Maps/ReplayAuthorization");
		Header.RandomSeed = 1234;

		FGuid Baseline;
		FString Error;
		ASSERT_THAT(IsTrue(
			SeinReplayFormat::ComputeBootstrapAuthorizationContextDigest(
				Header, Baseline, Error)));
		ASSERT_THAT(IsTrue(Baseline.IsValid()));
		FGuid Retry;
		ASSERT_THAT(IsTrue(
			SeinReplayFormat::ComputeBootstrapAuthorizationContextDigest(
				Header, Retry, Error)));
		ASSERT_THAT(IsTrue(Baseline == Retry));

		FSeinReplayHeader OtherContent = Header;
		OtherContent.BootstrapReceipt.SimulationContentDigest.D ^= 1u;
		if (!OtherContent.BootstrapReceipt.SimulationContentDigest.IsValid())
		{
			OtherContent.BootstrapReceipt.SimulationContentDigest.D = 2;
		}
		FGuid OtherDigest;
		ASSERT_THAT(IsTrue(
			SeinReplayFormat::ComputeBootstrapAuthorizationContextDigest(
				OtherContent, OtherDigest, Error)));
		ASSERT_THAT(IsTrue(Baseline != OtherDigest));
	}

	TEST(ReplayV8PrefixRejectsLegacyAndOversizedBodiesBeforeDecode,
		"SeinARTS.Unit.Network.ReplayFormat")
	{
		SeinReplayFormat::FPrefix Parsed;
		FString Error;
		TArray<uint8> LegacyBytes{1, 2, 3, 4};
		ASSERT_THAT(IsFalse(SeinReplayFormat::ParsePrefix(
			LegacyBytes, Parsed, Error)));

		const TArray<uint8> Body{42};
		FSeinReplayHeader ReceiptHeader;
		ReceiptHeader.MatchSettingsDigest = FGuid(5, 6, 7, 8);
		StampSyntheticBootstrapReceipt(ReceiptHeader);
		TArray<uint8> FileBytes;
		ASSERT_THAT(IsTrue(SeinReplayFormat::BuildPrefix(
			FGuid(1, 2, 3, 4),
			FGuid(5, 6, 7, 8),
			ReceiptHeader.BootstrapReceipt,
			123,
			Body,
			FileBytes,
			Error)));
		FileBytes.Append(Body);

		// Body length starts at byte 112 in the frozen v8 prefix. Mutating only
		// that declaration proves the cap is checked before body access/decode.
		const uint64 Oversized = SeinReplayFormat::MaxBodyBytes + 1;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			FileBytes[112 + Index] = static_cast<uint8>(
				Oversized >> ((7 - Index) * 8));
		}
		ASSERT_THAT(IsFalse(SeinReplayFormat::ParsePrefix(
			FileBytes, Parsed, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("outside the supported range"))));
	}

	TEST(ReplayV8BoundedBodyRejectsHostileTurnCountTransactionally,
		"SeinARTS.Unit.Network.ReplayFormat.Security")
	{
		FSeinReplay Source;
		Source.Header.CommandProtocolDigest = FGuid(1, 2, 3, 4);
		Source.Header.MatchSettingsDigest = FGuid(5, 6, 7, 8);
		StampSyntheticBootstrapReceipt(Source.Header);
		Source.Header.MapIdentifier = TEXT("SeinReplayWireSecurityTestMap");
		Source.Header.RecordedAt = FDateTime(2026, 7, 18);
		TArray<uint8> Body;
		FString Error;
		auto NoCommands = [](
			FGameplayTag, int32, FSeinCommandSchemaDescriptor&) { return false; };
		ASSERT_THAT(IsTrue(FSeinReplayWireCodec::Encode(
			Source, {}, NoCommands, Body, Error)));
		ASSERT_THAT(IsTrue(Body.Num() >= 6));
		ASSERT_THAT(AreEqual(static_cast<uint8>(0), Body[4]));
		ASSERT_THAT(AreEqual(static_cast<uint8>(4), Body[5]));

		// With an empty journal the last four bytes are TurnCount. The decoder
		// must reject this before reserving any turn records.
		Body[Body.Num() - 4] = 0xff;
		Body[Body.Num() - 3] = 0xff;
		Body[Body.Num() - 2] = 0xff;
		Body[Body.Num() - 1] = 0xff;
		FSeinReplay Destination;
		Destination.Header.GameVersion = TEXT("preserved");
		ASSERT_THAT(IsFalse(FSeinReplayWireCodec::Decode(
			Body, {}, NoCommands, Destination, Error)));
		ASSERT_THAT(AreEqual(FString(TEXT("preserved")), Destination.Header.GameVersion));
		ASSERT_THAT(AreEqual(0, Destination.Turns.Num()));
	}

	TEST(ReplayV8BoundedBodyRoundTripsUninternedUnicodeMapIdentity,
		"SeinARTS.Unit.Network.ReplayFormat.Security")
	{
		const FString MapIdentity = FString::Printf(
			TEXT("/Game/Maps/Replay_地图_%s"),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		ASSERT_THAT(IsTrue(FName(*MapIdentity, FNAME_Find).IsNone()));

		FSeinReplay Source;
		Source.Header.CommandProtocolDigest = FGuid(1, 2, 3, 4);
		Source.Header.MatchSettingsDigest = FGuid(5, 6, 7, 8);
		StampSyntheticBootstrapReceipt(Source.Header);
		Source.Header.MapIdentifier = MapIdentity;
		Source.Header.RecordedAt = FDateTime(2026, 7, 18);
		TArray<uint8> Body;
		FString Error;
		auto NoCommands = [](
			FGameplayTag, int32, FSeinCommandSchemaDescriptor&) { return false; };
		ASSERT_THAT(IsTrue(FSeinReplayWireCodec::Encode(
			Source, {}, NoCommands, Body, Error)));

		FSeinReplay Loaded;
		ASSERT_THAT(IsTrue(FSeinReplayWireCodec::Decode(
			Body, {}, NoCommands, Loaded, Error)));
		ASSERT_THAT(AreEqual(MapIdentity, Loaded.Header.MapIdentifier));
		ASSERT_THAT(IsTrue(
			Loaded.Header.BootstrapReceipt
				== Source.Header.BootstrapReceipt));
		ASSERT_THAT(IsTrue(FName(*MapIdentity, FNAME_Find).IsNone()));
	}

	TEST(ReplayV8AllocationAccountingIncludesHeaderStringBacking,
		"SeinARTS.Unit.Network.ReplayFormat.Security")
	{
		FSeinReplay EmptyStrings;
		EmptyStrings.Header.RecordedAt = FDateTime(2026, 7, 18);
		StampSyntheticBootstrapReceipt(EmptyStrings.Header);
		FSeinReplay WithStrings = EmptyStrings;
		WithStrings.Header.FrameworkVersion = TEXT("Framework-57");
		WithStrings.Header.GameVersion = TEXT("Game-Alpha");
		WithStrings.Header.MapIdentifier = TEXT("/Game/Maps/ReplayAllocation");

		auto NoCommands = [](
			FGameplayTag, int32, FSeinCommandSchemaDescriptor&) { return false; };
		TArray<uint8> EmptyBody;
		TArray<uint8> StringBody;
		FString Error;
		uint64 EmptyAllocationBytes = 0;
		uint64 StringAllocationBytes = 0;
		ASSERT_THAT(IsTrue(FSeinReplayWireCodec::Encode(
			EmptyStrings, {}, NoCommands, EmptyBody, Error, &EmptyAllocationBytes)));
		ASSERT_THAT(IsTrue(FSeinReplayWireCodec::Encode(
			WithStrings, {}, NoCommands, StringBody, Error, &StringAllocationBytes)));

		auto NativeDecodedBytes = [](const FString& Text)
		{
			FTCHARToUTF8 Utf8(*Text, Text.Len());
			return (static_cast<uint64>(Utf8.Length()) + 1u) * sizeof(TCHAR);
		};
		const uint64 ExpectedStringBytes =
			NativeDecodedBytes(WithStrings.Header.FrameworkVersion)
			+ NativeDecodedBytes(WithStrings.Header.GameVersion)
			+ NativeDecodedBytes(WithStrings.Header.MapIdentifier);
		ASSERT_THAT(AreEqual(
			EmptyAllocationBytes + ExpectedStringBytes,
			StringAllocationBytes));

		FSeinReplay Loaded;
		uint64 DecodedAllocationBytes = 0;
		ASSERT_THAT(IsTrue(FSeinReplayWireCodec::Decode(
			StringBody, {}, NoCommands, Loaded, Error, &DecodedAllocationBytes)));
		ASSERT_THAT(AreEqual(
			StringAllocationBytes, DecodedAllocationBytes));
		ASSERT_THAT(AreEqual(
			WithStrings.Header.MapIdentifier, Loaded.Header.MapIdentifier));
	}

	TEST(ReplayV8RoundTripsCataloguedRawNameCommandPayload,
		"SeinARTS.Unit.Network.ReplayFormat.Security")
	{
		FSeinCommandSchemaDescriptor Schema;
		Schema.StableSchemaId = TEXT("SeinFrameworkTests.Replay.RawName");
		Schema.CommandType = SeinARTSTags::Command_Type_Ping;
		Schema.SchemaVersion = 1;
		Schema.PayloadStruct =
			FSeinCommandSchemaIdentityWireTestPayload::StaticStruct();
		Schema.MaxPayloadBytes = 128;
		Schema.MaxPayloadAggregateElements = 8;
		TArray<FName> CanonicalNames;
		FString NameManifest;
		const TArray<FName> AuthoredNames{ TEXT("ReplayCommandIdentifier") };
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
		Payload.Name = TEXT("REPLAYCOMMANDIDENTIFIER");
		FSeinReplay Source;
		Source.Header.RecordedAt = FDateTime(2026, 7, 22);
		StampSyntheticBootstrapReceipt(Source.Header);
		FSeinCommand& Command =
			Source.Turns.AddDefaulted_GetRef().Commands.AddDefaulted_GetRef();
		Command.CommandType = Schema.CommandType;
		Command.SchemaVersion = Schema.SchemaVersion;
		Command.Payload = FInstancedStruct::Make(Payload);
		TArray<uint8> Body;
		FString Error;
		ASSERT_THAT(IsTrue(FSeinReplayWireCodec::Encode(
			Source, {}, FindSchema, Body, Error)));

		FSeinReplay Loaded;
		ASSERT_THAT(IsTrue(FSeinReplayWireCodec::Decode(
			Body, {}, FindSchema, Loaded, Error)));
		ASSERT_THAT(AreEqual(1, Loaded.Turns.Num()));
		ASSERT_THAT(AreEqual(1, Loaded.Turns[0].Commands.Num()));
		ASSERT_THAT(AreEqual(
			CanonicalNames[0],
			Loaded.Turns[0].Commands[0].Payload
				.Get<FSeinCommandSchemaIdentityWireTestPayload>().Name));
	}

	TEST(ReplayV8BoundedBodyRejectsEmbeddedNullInMapIdentity,
		"SeinARTS.Unit.Network.ReplayFormat.Security")
	{
		FSeinReplay Source;
		Source.Header.CommandProtocolDigest = FGuid(1, 2, 3, 4);
		Source.Header.MatchSettingsDigest = FGuid(5, 6, 7, 8);
		StampSyntheticBootstrapReceipt(Source.Header);
		const FString MapText = TEXT("SeinReplayEmbeddedNullMap");
		Source.Header.MapIdentifier = MapText;
		Source.Header.RecordedAt = FDateTime(2026, 7, 18);
		TArray<uint8> Body;
		FString Error;
		auto NoCommands = [](
			FGameplayTag, int32, FSeinCommandSchemaDescriptor&) { return false; };
		ASSERT_THAT(IsTrue(FSeinReplayWireCodec::Encode(
			Source, {}, NoCommands, Body, Error)));

		FTCHARToUTF8 Utf8Map(*MapText);
		int32 MapOffset = INDEX_NONE;
		for (int32 Candidate = 0;
			Candidate + Utf8Map.Length() <= Body.Num(); ++Candidate)
		{
			if (FMemory::Memcmp(
				Body.GetData() + Candidate,
				Utf8Map.Get(), Utf8Map.Length()) == 0)
			{
				MapOffset = Candidate;
				break;
			}
		}
		ASSERT_THAT(IsTrue(MapOffset != INDEX_NONE));
		Body[MapOffset + 4] = 0;
		FSeinReplay Destination;
		Destination.Header.GameVersion = TEXT("preserved");
		ASSERT_THAT(IsFalse(FSeinReplayWireCodec::Decode(
			Body, {}, NoCommands, Destination, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("embedded null"))));
		ASSERT_THAT(AreEqual(
			FString(TEXT("preserved")), Destination.Header.GameVersion));
	}

	TEST(ReplayWriterAcceptsZeroAsADeterministicSeed,
		"SeinARTS.Unit.Network.ReplayFormat")
	{
		FSeinReplayHeader Header;
		Header.CommandProtocolDigest = FGuid(1, 2, 3, 4);
		ASSERT_THAT(IsTrue(SeinCanonicalizeAndDigestMatchSettings(
			Header.SettingsSnapshot, Header.MatchSettingsDigest, nullptr)));
		StampSyntheticBootstrapReceipt(Header);
		StampUnitReplayIdentity(Header);
		Header.MapIdentifier = TEXT("ReplayFormatTestMap");
		Header.RandomSeed = 0;

		USeinReplayWriter* Writer = NewObject<USeinReplayWriter>();
		ASSERT_THAT(IsNotNull(Writer));
		Writer->StartRecording(Header);
		ASSERT_THAT(IsTrue(Writer->IsRecording()));
	}

	TEST(ReplayWriterRequiresContiguousCompletedTickObservations,
		"SeinARTS.Unit.Network.ReplayFormat")
	{
		FSeinReplayHeader Header;
		Header.CommandProtocolDigest = FGuid(1, 2, 3, 4);
		ASSERT_THAT(IsTrue(SeinCanonicalizeAndDigestMatchSettings(
			Header.SettingsSnapshot, Header.MatchSettingsDigest, nullptr)));
		StampSyntheticBootstrapReceipt(Header);
		StampUnitReplayIdentity(Header);
		Header.MapIdentifier = TEXT("ReplayFormatTestMap");

		USeinReplayWriter* Writer = NewObject<USeinReplayWriter>();
		ASSERT_THAT(IsNotNull(Writer));
		Writer->StartRecording(Header);
		Writer->ObserveCompletedTick(1);
		ASSERT_THAT(AreEqual(1, Writer->GetObservedEndTick()));
		ASSERT_THAT(IsFalse(Writer->HasTickObservationFailure()));

		TestRunner->AddExpectedError(
			TEXT("non-contiguous completed tick"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		Writer->ObserveCompletedTick(3);
		ASSERT_THAT(AreEqual(1, Writer->GetObservedEndTick()));
		ASSERT_THAT(IsTrue(Writer->HasTickObservationFailure()));
		ASSERT_THAT(IsFalse(Writer->IsRecording()));

		// Once the observation contract is broken, later producer traffic must
		// remain a no-op instead of rebuilding an unfinishable in-memory journal.
		for (int32 Turn = 0; Turn < 128; ++Turn)
		{
			Writer->RecordTurn(Turn, {});
		}
		ASSERT_THAT(AreEqual(1, Writer->GetObservedEndTick()));
		TestRunner->AddExpectedError(
			TEXT("recording was aborted by a completed-tick observation gap"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsTrue(Writer->FinishRecording().IsEmpty()));
	}

	TEST(ReplayWriterRejectsPlaceholderOrStaleRuntimeIdentity,
		"SeinARTS.Unit.Network.ReplayFormat")
	{
		FSeinReplayHeader Header;
		Header.CommandProtocolDigest = FGuid(1, 2, 3, 4);
		ASSERT_THAT(IsTrue(SeinCanonicalizeAndDigestMatchSettings(
			Header.SettingsSnapshot, Header.MatchSettingsDigest, nullptr)));
		StampSyntheticBootstrapReceipt(Header);
		StampUnitReplayIdentity(Header);
		Header.MapIdentifier = TEXT("ReplayFormatTestMap");
		Header.FrameworkVersion = TEXT("0.1.0");

		USeinReplayWriter* Writer = NewObject<USeinReplayWriter>();
		ASSERT_THAT(IsNotNull(Writer));
		TestRunner->AddExpectedError(
			TEXT("incomplete or inconsistent compatibility header"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		Writer->StartRecording(Header);
		ASSERT_THAT(IsFalse(Writer->IsRecording()));
	}

	TEST(ReplayWriterRejectsAnOversizedTurnAtRecordTime,
		"SeinARTS.Unit.Network.ReplayFormat.Security")
	{
		FSeinReplayHeader Header;
		Header.CommandProtocolDigest = FGuid(1, 2, 3, 4);
		ASSERT_THAT(IsTrue(SeinCanonicalizeAndDigestMatchSettings(
			Header.SettingsSnapshot, Header.MatchSettingsDigest, nullptr)));
		StampSyntheticBootstrapReceipt(Header);
		StampUnitReplayIdentity(Header);
		Header.MapIdentifier = TEXT("ReplayFormatTestMap");
		USeinReplayWriter* Writer = NewObject<USeinReplayWriter>();
		Writer->StartRecording(Header);
		ASSERT_THAT(IsTrue(Writer->IsRecording()));

		TArray<FSeinCommand> Oversized;
		Oversized.SetNum(SeinReplayFormat::MaxCommandsPerTurn + 1);
		TestRunner->AddExpectedError(
			TEXT("exceeding the canonical turn cap"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		Writer->RecordTurn(3, Oversized);
		ASSERT_THAT(IsFalse(Writer->IsRecording()));
		TestRunner->AddExpectedError(
			TEXT("recording was aborted by an invalid or oversized journal"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsTrue(Writer->FinishRecording().IsEmpty()));
	}

	TEST(ReplayJournalRequiresEveryAppliedTurnIncludingEmptyHeartbeats,
		"SeinARTS.Unit.Network.ReplayFormat")
	{
		constexpr int32 TicksPerTurn = 2;
		constexpr int32 InputDelayTurns = 3;
		FSeinReplayHeader Header;
		Header.EndTick = 10; // Required turns: 3, 4, 5.
		FString Error;

		TArray<FSeinReplayTurnRecord> Turns{
			MakeEmptyTurn(3), MakeEmptyTurn(4), MakeEmptyTurn(5)};
		ASSERT_THAT(IsTrue(SeinReplayFormat::ValidateJournal(
			Header, Turns, TicksPerTurn, InputDelayTurns, Error)));

		TArray<FSeinReplayTurnRecord> LateFirst{
			MakeEmptyTurn(4), MakeEmptyTurn(5)};
		ASSERT_THAT(IsFalse(SeinReplayFormat::ValidateJournal(
			Header, LateFirst, TicksPerTurn, InputDelayTurns, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("expected contiguous turn 3"))));

		TArray<FSeinReplayTurnRecord> Gap{
			MakeEmptyTurn(3), MakeEmptyTurn(5)};
		ASSERT_THAT(IsFalse(SeinReplayFormat::ValidateJournal(
			Header, Gap, TicksPerTurn, InputDelayTurns, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("expected contiguous turn 4"))));

		TArray<FSeinReplayTurnRecord> Duplicate{
			MakeEmptyTurn(3), MakeEmptyTurn(3), MakeEmptyTurn(5)};
		ASSERT_THAT(IsFalse(SeinReplayFormat::ValidateJournal(
			Header, Duplicate, TicksPerTurn, InputDelayTurns, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("expected contiguous turn 4"))));

		Turns.Add(MakeEmptyTurn(6));
		ASSERT_THAT(IsFalse(SeinReplayFormat::ValidateJournal(
			Header, Turns, TicksPerTurn, InputDelayTurns, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("beyond inclusive EndTick"))));
	}

	TEST(ReplayWriterFinalizationTrimsOnlyCanonicalFutureTail,
		"SeinARTS.Unit.Network.ReplayFormat")
	{
		constexpr int32 TicksPerTurn = 2;
		constexpr int32 InputDelayTurns = 3;
		FString Error;

		FSeinReplayHeader Header;
		Header.EndTick = 8; // Required turns: 3 and 4.
		TArray<FSeinReplayTurnRecord> Turns{
			MakeEmptyTurn(3), MakeEmptyTurn(4), MakeEmptyTurn(5)};
		ASSERT_THAT(IsTrue(SeinReplayFormat::FinalizeRecordedJournal(
			Header, Turns, TicksPerTurn, InputDelayTurns, Error)));
		ASSERT_THAT(AreEqual(2, Turns.Num()));
		ASSERT_THAT(AreEqual(4, Turns.Last().TurnId));

		Header.EndTick = 0;
		Turns = {MakeEmptyTurn(3), MakeEmptyTurn(4)};
		ASSERT_THAT(IsTrue(SeinReplayFormat::FinalizeRecordedJournal(
			Header, Turns, TicksPerTurn, InputDelayTurns, Error)));
		ASSERT_THAT(IsTrue(Turns.IsEmpty()));

		Header.EndTick = 10;
		Turns = {MakeEmptyTurn(3), MakeEmptyTurn(5)};
		ASSERT_THAT(IsFalse(SeinReplayFormat::FinalizeRecordedJournal(
			Header, Turns, TicksPerTurn, InputDelayTurns, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("expected contiguous turn 4"))));
	}

	TEST(ReplayWriterPublishesAfterTrimmingARecordedFutureTail,
		"SeinARTS.Integration.Network.Replay")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		const int32 TicksPerTurn = Settings->TurnRate > 0
			? FMath::Max(1, Settings->SimulationTickRate / Settings->TurnRate)
			: 1;
		const int32 FirstTurn = Settings->InputDelayTurns > 0
			? Settings->InputDelayTurns
			: 3;
		const int32 EndTick = FirstTurn * TicksPerTurn;

		USeinReplayWriter* Writer = NewObject<USeinReplayWriter>();
		Writer->StartRecording(MakeExecutableHeader(*World));
		Writer->RecordTurn(FirstTurn, {});
		Writer->RecordTurn(FirstTurn + 1, {}); // unapplied input-delay tail
		for (int32 Tick = 1; Tick <= EndTick; ++Tick)
		{
			Writer->ObserveCompletedTick(Tick);
		}
		FScopedReplayFile ReplayFile{Writer->FinishRecording()};
		ASSERT_THAT(IsFalse(ReplayFile.Path.IsEmpty()));

		USeinReplayReader* Reader = NewObject<USeinReplayReader>(
			&Spawner.GetWorld());
		ASSERT_THAT(IsTrue(Reader->LoadFromFile(ReplayFile.Path)));
		ASSERT_THAT(AreEqual(1, Reader->GetTurnCount()));
		ASSERT_THAT(AreEqual(EndTick, Reader->GetHeader().EndTick));
	}

	TEST(ReplayTurnEnvelopeRejectsImpossibleTimingAndForgedProvenance,
		"SeinARTS.Unit.Network.ReplayFormat")
	{
		constexpr int32 TicksPerTurn = 6;
		constexpr int32 InputDelayTurns = 3;
		const FSeinReplayHeader Header = MakeReplayHeaderWithPlayerOne();
		FString Error;

		FSeinReplayTurnRecord Turn = MakeCanonicalTurn(
			InputDelayTurns, TicksPerTurn);
		ASSERT_THAT(IsTrue(SeinReplayFormat::ValidateTurnEnvelope(
			Header, Turn, TicksPerTurn, InputDelayTurns, Error)));

		Turn.TurnId = 0;
		Turn.Commands[0].Tick = 0;
		ASSERT_THAT(IsFalse(SeinReplayFormat::ValidateTurnEnvelope(
			Header, Turn, TicksPerTurn, InputDelayTurns, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("first recordable turn"))));

		Turn = MakeCanonicalTurn(InputDelayTurns, TicksPerTurn);
		Turn.Commands[0].IssuerKind = ESeinCommandIssuerKind::DeterministicSystem;
		ASSERT_THAT(IsFalse(SeinReplayFormat::ValidateTurnEnvelope(
			Header, Turn, TicksPerTurn, InputDelayTurns, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("non-recordable issuer"))));

		Turn = MakeCanonicalTurn(InputDelayTurns, TicksPerTurn);
		Turn.Commands[0].PlayerID = FSeinPlayerID(2);
		ASSERT_THAT(IsFalse(SeinReplayFormat::ValidateTurnEnvelope(
			Header, Turn, TicksPerTurn, InputDelayTurns, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("inactive player slot"))));

		Turn = MakeCanonicalTurn(InputDelayTurns, TicksPerTurn);
		Turn.Commands[0].DerivedResourcePayer = FSeinPlayerID(1);
		ASSERT_THAT(IsFalse(SeinReplayFormat::ValidateTurnEnvelope(
			Header, Turn, TicksPerTurn, InputDelayTurns, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("derived resource payer"))));

		Turn = MakeCanonicalTurn(InputDelayTurns, TicksPerTurn);
		++Turn.Commands[0].Tick;
		ASSERT_THAT(IsFalse(SeinReplayFormat::ValidateTurnEnvelope(
			Header, Turn, TicksPerTurn, InputDelayTurns, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("canonical tick"))));

		Turn.TurnId = MAX_int32;
		Turn.Commands.Reset();
		ASSERT_THAT(IsFalse(SeinReplayFormat::ValidateTurnEnvelope(
			Header, Turn, /*TicksPerTurn=*/2, InputDelayTurns, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("tick range"))));
	}

	TEST(ReplayAdministratorProvenanceIsMatchControlOnly,
		"SeinARTS.Unit.Network.ReplayFormat")
	{
		FString Error;
		ASSERT_THAT(IsFalse(SeinReplayFormat::ValidateIssuerForSchema(
			ESeinCommandIssuerKind::MatchAdministrator,
			ESeinCommandAuthorityScope::Entity,
			Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("MatchControl"))));
		ASSERT_THAT(IsTrue(SeinReplayFormat::ValidateIssuerForSchema(
			ESeinCommandIssuerKind::MatchAdministrator,
			ESeinCommandAuthorityScope::MatchControl,
			Error)));
	}

	TEST(ReplayPlayerManifestMustMatchActiveSlotsAndSpectatorPolicy,
		"SeinARTS.Unit.Network.ReplayFormat.Security")
	{
		FSeinReplayHeader Header;
		FSeinMatchSlot& Slot = Header.SettingsSnapshot.Slots.AddDefaulted_GetRef();
		Slot.SlotIndex = 1;
		Slot.State = ESeinSlotState::AI;
		Slot.FactionID = FSeinFactionID(3);
		Slot.TeamID = 2;
		FString Error;
		ASSERT_THAT(IsFalse(SeinReplayCompatibility::ValidatePlayerManifest(
			Header, Error)));

		FSeinPlayerRegistration& Player = Header.Players.AddDefaulted_GetRef();
		Player.PlayerID = FSeinPlayerID(1);
		Player.FactionID = Slot.FactionID;
		Player.TeamID = Slot.TeamID;
		Player.bIsAI = true;
		ASSERT_THAT(IsTrue(SeinReplayCompatibility::ValidatePlayerManifest(
			Header, Error)));

		Player.bIsAI = false;
		ASSERT_THAT(IsFalse(SeinReplayCompatibility::ValidatePlayerManifest(
			Header, Error)));
		Player.bIsAI = true;
		FSeinPlayerRegistration& Spectator = Header.Players.AddDefaulted_GetRef();
		Spectator.PlayerID = FSeinPlayerID(2);
		Spectator.bIsSpectator = true;
		ASSERT_THAT(IsTrue(SeinReplayCompatibility::ValidatePlayerManifest(
			Header, Error)));
		Spectator.TeamID = 1;
		ASSERT_THAT(IsFalse(SeinReplayCompatibility::ValidatePlayerManifest(
			Header, Error)));
	}

	TEST(ReplayPlaybackRequiresAPristineWorld,
		"SeinARTS.Unit.Network.ReplayFormat")
	{
		FString Error;
		ASSERT_THAT(IsTrue(SeinReplayFormat::ValidatePlaybackStartState(
			ESeinMatchState::Lobby, false, 0, Error)));
		ASSERT_THAT(IsFalse(SeinReplayFormat::ValidatePlaybackStartState(
			ESeinMatchState::Playing, false, 0, Error)));
		ASSERT_THAT(IsFalse(SeinReplayFormat::ValidatePlaybackStartState(
			ESeinMatchState::Lobby, true, 0, Error)));
		ASSERT_THAT(IsFalse(SeinReplayFormat::ValidatePlaybackStartState(
			ESeinMatchState::Lobby, false, 1, Error)));
	}

	TEST(ReplayV8RejectsForeignSimulationContentBeforeBodyDecode,
		"SeinARTS.Integration.Network.Replay")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(World->IsSimulationContentReady()));
		BindReplayTestMaterializer(*World);

		USeinReplayWriter* Writer = NewObject<USeinReplayWriter>();
		Writer->StartRecording(MakeExecutableHeader(*World));
		FScopedReplayFile SourceFile{Writer->FinishRecording()};
		ASSERT_THAT(IsFalse(SourceFile.Path.IsEmpty()));

		TArray<uint8> Bytes;
		ASSERT_THAT(IsTrue(FFileHelper::LoadFileToArray(
			Bytes, *SourceFile.Path)));
		ASSERT_THAT(IsTrue(Bytes.Num() >= SeinReplayFormat::PrefixBytes));
		FGuid ForeignSimulationContent =
			World->GetSimulationContentDigest();
		ForeignSimulationContent.D ^= 1u;
		if (!ForeignSimulationContent.IsValid())
		{
			ForeignSimulationContent.D = 2;
		}
		// v8: magic/version + command + match digests precede content.
		WriteGuidAt(Bytes, 8 + 4 + 16 + 16, ForeignSimulationContent);
		FScopedReplayFile ForeignFile{
			FPaths::ProjectSavedDir()
				/ TEXT("Replays")
				/ FString::Printf(
					TEXT("ForeignContent_%s.seinreplay"),
					*FGuid::NewGuid().ToString(EGuidFormats::Digits))};
		ASSERT_THAT(IsTrue(FFileHelper::SaveArrayToFile(
			Bytes, *ForeignFile.Path)));

		USeinReplayReader* Reader = NewObject<USeinReplayReader>(
			&Spawner.GetWorld());
		TestRunner->AddExpectedError(
			TEXT("before body deserialization: simulation-content digest mismatch"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(Reader->LoadFromFile(ForeignFile.Path)));
		ASSERT_THAT(AreEqual(0, Reader->GetTurnCount()));
	}

	TEST(ReplayTickZeroDoesNotStartAFreeRunningSimulation,
		"SeinARTS.Integration.Network.Replay")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		BindReplayTestMaterializer(*World);

		USeinReplayWriter* Writer = NewObject<USeinReplayWriter>();
		Writer->StartRecording(MakeExecutableHeader(*World));
		FScopedReplayFile ReplayFile{Writer->FinishRecording()};
		ASSERT_THAT(IsFalse(ReplayFile.Path.IsEmpty()));

		USeinReplayReader* Reader = NewObject<USeinReplayReader>(
			&Spawner.GetWorld());
		ASSERT_THAT(IsTrue(Reader->LoadFromFile(ReplayFile.Path)));
		ASSERT_THAT(IsTrue(Reader->Play()));
		ASSERT_THAT(AreEqual(0, World->GetCurrentTick()));
		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
		ASSERT_THAT(IsFalse(Reader->IsPlaying()));
	}

	TEST(ReplayNaturallyStopsAtInclusiveEndTickUnderCatchUp,
		"SeinARTS.Integration.Network.Replay")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		BindReplayTestMaterializer(*World);

		USeinReplayWriter* Writer = NewObject<USeinReplayWriter>();
		Writer->StartRecording(MakeExecutableHeader(*World));
		Writer->ObserveCompletedTick(1);
		Writer->ObserveCompletedTick(2);
		FScopedReplayFile ReplayFile{Writer->FinishRecording()};
		ASSERT_THAT(IsFalse(ReplayFile.Path.IsEmpty()));

		USeinReplayReader* Reader = NewObject<USeinReplayReader>(
			&Spawner.GetWorld());
		ASSERT_THAT(IsTrue(Reader->LoadFromFile(ReplayFile.Path)));
		ASSERT_THAT(IsTrue(Reader->Play()));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds() * 8.0f);

		ASSERT_THAT(AreEqual(2, World->GetCurrentTick()));
		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
		ASSERT_THAT(IsFalse(Reader->IsPlaying()));
	}

	TEST(ReplayPlaybackOwnsExternalCommandIngress,
		"SeinARTS.Integration.Network.Replay")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		BindReplayTestMaterializer(*World);
		const FSeinPlayerID AIPlayer(1);
		FSeinMatchSettings MatchSettings;
		FSeinMatchSlot& Slot = MatchSettings.Slots.AddDefaulted_GetRef();
		Slot.SlotIndex = AIPlayer.Value;
		Slot.State = ESeinSlotState::AI;
		Slot.FactionID = FSeinFactionID(1);
		FSeinReplayHeader Header = MakeExecutableHeader(*World, MatchSettings);

		USeinReplayWriter* Writer = NewObject<USeinReplayWriter>();
		Writer->StartRecording(Header);
		Writer->ObserveCompletedTick(1);
		Writer->ObserveCompletedTick(2);
		FScopedReplayFile ReplayFile{Writer->FinishRecording()};
		ASSERT_THAT(IsFalse(ReplayFile.Path.IsEmpty()));

		USeinReplayReader* Reader = NewObject<USeinReplayReader>(
			&Spawner.GetWorld());
		ASSERT_THAT(IsTrue(Reader->LoadFromFile(ReplayFile.Path)));
		USeinReplayEmittingAIController* AI =
			NewObject<USeinReplayEmittingAIController>(World);
		ASSERT_THAT(IsNotNull(AI));
		World->RegisterAIController(AI, AIPlayer);
		ASSERT_THAT(IsTrue(Reader->Play()));
		ASSERT_THAT(IsTrue(Reader->IsPlaying()));

		TestRunner->AddExpectedError(
			TEXT("while replay owns external ingress"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		World->SubmitLocalCommandDraft(FSeinCommand::MakePingCommand(
			FSeinPlayerID(1), FFixedVector()));
		ASSERT_THAT(AreEqual(0, World->GetPendingCommands().Num()));

		FTSTicker::GetCoreTicker().Tick(
			World->GetFixedDeltaTimeSeconds() * 8.0f);
		ASSERT_THAT(AreEqual(0, AI->TickCount));
		ASSERT_THAT(AreEqual(0, World->GetPendingCommands().Num()));
		ASSERT_THAT(IsFalse(Reader->IsPlaying()));
	}

	TEST(ReplayV8RejectsPauseCommandsWithoutAFrozenTimeJournal,
		"SeinARTS.Integration.Network.Replay")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		const int32 TicksPerTurn = Settings->TurnRate > 0
			? FMath::Max(1, Settings->SimulationTickRate / Settings->TurnRate)
			: 1;
		const int32 FirstTurn = Settings->InputDelayTurns > 0
			? Settings->InputDelayTurns
			: 3;
		const int32 EndTick = FirstTurn * TicksPerTurn;

		FSeinMatchSettings MatchSettings;
		FSeinMatchSlot& Slot = MatchSettings.Slots.AddDefaulted_GetRef();
		Slot.SlotIndex = 1;
		Slot.State = ESeinSlotState::Human;
		Slot.FactionID = FSeinFactionID(1);
		FSeinReplayHeader Header = MakeExecutableHeader(*World, MatchSettings);

		FSeinCommand Pause;
		Pause.PlayerID = FSeinPlayerID(1);
		Pause.IssuerKind = ESeinCommandIssuerKind::Player;
		Pause.CommandType = SeinARTSTags::Command_Type_PauseMatchRequest;
		Pause.Tick = EndTick;

		USeinReplayWriter* Writer = NewObject<USeinReplayWriter>();
		Writer->StartRecording(Header);
		Writer->RecordTurn(FirstTurn, {Pause});
		for (int32 Tick = 1; Tick <= EndTick; ++Tick)
		{
			Writer->ObserveCompletedTick(Tick);
		}
		FScopedReplayFile ReplayFile{Writer->FinishRecording()};
		ASSERT_THAT(IsFalse(ReplayFile.Path.IsEmpty()));

		USeinReplayReader* Reader = NewObject<USeinReplayReader>(
			&Spawner.GetWorld());
		TestRunner->AddExpectedError(
			TEXT("contains unsupported pause-control"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(Reader->LoadFromFile(ReplayFile.Path)));
	}

	TEST(ReplayStopRetractsAPrimedFutureTurnBeforeReleasingIngress,
		"SeinARTS.Integration.Network.Replay")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		BindReplayTestMaterializer(*World);
		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		const int32 TicksPerTurn = Settings->TurnRate > 0
			? FMath::Max(1, Settings->SimulationTickRate / Settings->TurnRate)
			: 1;
		const int32 FirstTurn = Settings->InputDelayTurns > 0
			? Settings->InputDelayTurns
			: 3;
		const int32 EndTick = FirstTurn * TicksPerTurn;

		const FSeinPlayerID PlayerID(1);
		FSeinMatchSettings MatchSettings;
		FSeinMatchSlot& Slot = MatchSettings.Slots.AddDefaulted_GetRef();
		Slot.SlotIndex = PlayerID.Value;
		Slot.State = ESeinSlotState::Human;
		Slot.FactionID = FSeinFactionID(1);
		FSeinReplayHeader Header = MakeExecutableHeader(*World, MatchSettings);

		FSeinCommand Recorded = FSeinCommand::MakePingCommand(
			PlayerID, FFixedVector());
		Recorded.IssuerKind = ESeinCommandIssuerKind::Player;
		Recorded.Tick = EndTick;
		USeinReplayWriter* Writer = NewObject<USeinReplayWriter>();
		Writer->StartRecording(Header);
		Writer->RecordTurn(FirstTurn, {Recorded});
		for (int32 Tick = 1; Tick <= EndTick; ++Tick)
		{
			Writer->ObserveCompletedTick(Tick);
		}
		FScopedReplayFile ReplayFile{Writer->FinishRecording()};
		ASSERT_THAT(IsFalse(ReplayFile.Path.IsEmpty()));

		USeinReplayReader* Reader = NewObject<USeinReplayReader>(
			&Spawner.GetWorld());
		ASSERT_THAT(IsTrue(Reader->LoadFromFile(ReplayFile.Path)));
		ASSERT_THAT(IsTrue(Reader->Play()));
		ASSERT_THAT(IsTrue(Reader->IsPlaying()));
		ASSERT_THAT(AreEqual(0, World->GetPendingCommands().Num()));
		ASSERT_THAT(AreEqual(
			0, World->GetPendingReplayCommandCountForTests()));

		// Replay commands enter the retractable lane only at the boundary
		// immediately before their recorded tick, not when playback starts.
		for (int32 Tick = 1; Tick < EndTick; ++Tick)
		{
			FTSTicker::GetCoreTicker().Tick(
				World->GetFixedDeltaTimeSeconds());
		}
		ASSERT_THAT(AreEqual(EndTick - 1, World->GetCurrentTick()));
		ASSERT_THAT(IsTrue(Reader->IsPlaying()));
		ASSERT_THAT(AreEqual(
			1, World->GetPendingReplayCommandCountForTests()));

		// Stop must retract only the replay-owned future turn. A deterministic
		// system command already pending on the ordinary lane remains intact.
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			World->EnqueueDerivedCommand(FSeinCommand::MakePingCommand(
				PlayerID, FFixedVector()));
		}
		ASSERT_THAT(AreEqual(1, World->GetPendingCommands().Num()));
		Reader->Stop();
		ASSERT_THAT(AreEqual(
			0, World->GetPendingReplayCommandCountForTests()));
		ASSERT_THAT(AreEqual(1, World->GetPendingCommands().Num()));
		World->SubmitLocalCommandDraft(FSeinCommand::MakePingCommand(
			PlayerID, FFixedVector()));
		ASSERT_THAT(AreEqual(2, World->GetPendingCommands().Num()));
		World->StopSimulation();
	}

	TEST(ReplayLoadIsFailureAtomicAndCannotReplaceActivePlayback,
		"SeinARTS.Integration.Network.Replay")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		BindReplayTestMaterializer(*World);

		USeinReplayWriter* ActiveWriter = NewObject<USeinReplayWriter>();
		ActiveWriter->StartRecording(MakeExecutableHeader(*World));
		ActiveWriter->ObserveCompletedTick(1);
		ActiveWriter->ObserveCompletedTick(2);
		FScopedReplayFile ActiveFile{ActiveWriter->FinishRecording()};
		ASSERT_THAT(IsFalse(ActiveFile.Path.IsEmpty()));

		USeinReplayWriter* ReplacementWriter = NewObject<USeinReplayWriter>();
		ReplacementWriter->StartRecording(MakeExecutableHeader(*World));
		FScopedReplayFile ReplacementFile{ReplacementWriter->FinishRecording()};
		ASSERT_THAT(IsFalse(ReplacementFile.Path.IsEmpty()));

		USeinReplayReader* Reader = NewObject<USeinReplayReader>(
			&Spawner.GetWorld());
		ASSERT_THAT(IsTrue(Reader->LoadFromFile(ActiveFile.Path)));
		const int32 OriginalEndTick = Reader->GetHeader().EndTick;
		const int32 OriginalTurnCount = Reader->GetTurnCount();

		TestRunner->AddExpectedError(
			TEXT("refused to replace the loaded replay while playback is active"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsTrue(Reader->Play()));
		ASSERT_THAT(IsFalse(Reader->LoadFromFile(ReplacementFile.Path)));
		ASSERT_THAT(IsTrue(Reader->IsPlaying()));
		ASSERT_THAT(AreEqual(OriginalEndTick, Reader->GetHeader().EndTick));
		ASSERT_THAT(AreEqual(OriginalTurnCount, Reader->GetTurnCount()));

		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds() * 8.0f);
		ASSERT_THAT(AreEqual(OriginalEndTick, World->GetCurrentTick()));
		ASSERT_THAT(IsFalse(Reader->IsPlaying()));

		TestRunner->AddExpectedError(
			TEXT("rejected file size"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		const FString MissingPath = FPaths::ProjectSavedDir()
			/ TEXT("Replays/DefinitelyMissing.seinreplay");
		IFileManager::Get().Delete(*MissingPath, false, true);
		ASSERT_THAT(IsFalse(Reader->LoadFromFile(MissingPath)));
		ASSERT_THAT(AreEqual(OriginalEndTick, Reader->GetHeader().EndTick));
		ASSERT_THAT(AreEqual(OriginalTurnCount, Reader->GetTurnCount()));

		FSeinReplayHeader InvalidSettingsHeader = MakeExecutableHeader(*World);
		FSeinMatchSlot& InvalidSlot =
			InvalidSettingsHeader.SettingsSnapshot.Slots.AddDefaulted_GetRef();
		// Slot zero is wire-valid but rejected by the current match contract.
		// This keeps the fixture encodable so the reader reaches the intended
		// semantic-validation and failure-atomicity branch.
		InvalidSlot.SlotIndex = 0;
		ASSERT_THAT(IsTrue(SeinCanonicalizeAndDigestMatchSettings(
			InvalidSettingsHeader.SettingsSnapshot,
			InvalidSettingsHeader.MatchSettingsDigest,
			nullptr)));
		StampSyntheticBootstrapReceipt(InvalidSettingsHeader);
		USeinReplayWriter* InvalidSettingsWriter =
			NewObject<USeinReplayWriter>();
		InvalidSettingsWriter->StartRecording(InvalidSettingsHeader);
		FScopedReplayFile InvalidSettingsFile{
			InvalidSettingsWriter->FinishRecording()};
		ASSERT_THAT(IsFalse(InvalidSettingsFile.Path.IsEmpty()));

		TestRunner->AddExpectedError(
			TEXT("settings snapshot fails the current match contract"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(Reader->LoadFromFile(InvalidSettingsFile.Path)));
		ASSERT_THAT(AreEqual(OriginalEndTick, Reader->GetHeader().EndTick));
		ASSERT_THAT(AreEqual(OriginalTurnCount, Reader->GetTurnCount()));
	}

	TEST(ReplayReaderDecodesFrozenInstancedPayloadWithoutPackageLoads,
		"SeinARTS.Integration.Network.Replay")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FSeinMatchSettings MatchSettings;
		FSeinMatchSlot& Slot = MatchSettings.Slots.AddDefaulted_GetRef();
		Slot.SlotIndex = 1;
		Slot.State = ESeinSlotState::Human;
		Slot.FactionID = FSeinFactionID(1);
		FSeinReplayHeader Header = MakeExecutableHeader(*World, MatchSettings);

		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		const int32 TicksPerTurn = Settings->TurnRate > 0
			? FMath::Max(1, Settings->SimulationTickRate / Settings->TurnRate)
			: 1;
		const int32 FirstTurn = Settings->InputDelayTurns > 0
			? Settings->InputDelayTurns
			: 3;
		const int32 EndTick = FirstTurn * TicksPerTurn;

		FSeinCommand BrokerCommand;
		BrokerCommand.PlayerID = FSeinPlayerID(1);
		BrokerCommand.IssuerKind = ESeinCommandIssuerKind::Player;
		BrokerCommand.CommandType = SeinARTSTags::Command_Type_BrokerOrder;
		BrokerCommand.Tick = EndTick;
		BrokerCommand.Payload = FInstancedStruct::Make(FSeinBrokerOrderPayload());

		USeinReplayWriter* Writer = NewObject<USeinReplayWriter>();
		Writer->StartRecording(Header);
		Writer->RecordTurn(FirstTurn, {BrokerCommand});
		for (int32 Tick = 1; Tick <= EndTick; ++Tick)
		{
			Writer->ObserveCompletedTick(Tick);
		}
		FScopedReplayFile ReplayFile{Writer->FinishRecording()};
		ASSERT_THAT(IsFalse(ReplayFile.Path.IsEmpty()));

		USeinReplayReader* Reader = NewObject<USeinReplayReader>(
			&Spawner.GetWorld());
		ASSERT_THAT(IsTrue(Reader->LoadFromFile(ReplayFile.Path)));
		ASSERT_THAT(AreEqual(1, Reader->GetTurnCount()));
	}
}

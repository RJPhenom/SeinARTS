#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Async/TaskGraphInterfaces.h"
#include "Data/SeinRelationshipTypes.h"
#include "Data/SeinWorldSnapshot.h"
#include "Containers/Ticker.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SeinNetCommandWireCodec.h"
#include "SeinReplayFormat.h"
#include "SeinReplayJournalFormat.h"
#include "SeinReplayReader.h"
#include "SeinReplayWriter.h"
#include "SeinReplayWireCodec.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinComponentStorageTestTypes.h"
#include "TestTypes/SeinReplayTestTypes.h"
#include "TestTypes/SeinCommandSchemaTestTypes.h"
#include "UObject/GarbageCollection.h"

struct FSeinReplayReaderTestAccess
{
	static uint64 BeginSyntheticPlayback(USeinReplayReader& Reader)
	{
		++Reader.PlaybackGeneration;
		Reader.bPlaying = true;
		Reader.bJournalFailureScheduled = true;
		Reader.PendingJournalFailureReason = TEXT("synthetic failure");
		return Reader.PlaybackGeneration;
	}

	static void DeliverFailure(
		USeinReplayReader& Reader,
		uint64 ExpectedPlaybackGeneration)
	{
		Reader.HandleJournalPlaybackFailure(ExpectedPlaybackGeneration);
	}

	static bool IsPlaying(const USeinReplayReader& Reader)
	{
		return Reader.bPlaying;
	}

	static bool IsFailureScheduled(const USeinReplayReader& Reader)
	{
		return Reader.bJournalFailureScheduled;
	}

	static TArray<int32> GetJournalCheckpointTicks(
		const USeinReplayReader& Reader)
	{
		TArray<int32> Ticks;
		Ticks.Reserve(Reader.LoadedJournalCheckpoints.Num());
		for (const USeinReplayReader::FIndexedJournalFrame& Checkpoint :
			Reader.LoadedJournalCheckpoints)
		{
			Ticks.Add(Checkpoint.TimelineTick);
		}
		return Ticks;
	}

	static void EndSyntheticPlayback(USeinReplayReader& Reader)
	{
		Reader.bPlaying = false;
		Reader.Stop();
	}
};

struct FSeinWorldSubsystemTestAccess
{
	static bool IsSchedulerReserved(const USeinWorldSubsystem& World)
	{
		return World.bSimulationSchedulerReserved
			&& World.TickerHandle.IsValid();
	}
};

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
				if (!Path.IsEmpty()
					&& IFileManager::Get().FileExists(*Path)
					&& !IFileManager::Get().Delete(*Path, false, true))
				{
					UE_LOG(LogTemp, Error,
						TEXT("Could not delete replay test artifact: %s"),
						*Path);
				}
			}
		};

		struct FScopedFastReplayMaintenance
		{
			FScopedFastReplayMaintenance()
			{
				Settings = GetMutableDefault<USeinARTSCoreSettings>();
				check(Settings);
				PreviousCheckpointInterval =
					Settings->ReplayCheckpointIntervalTurns;
				PreviousTurnBatchSize = Settings->ReplayTurnBatchSize;
				Settings->ReplayCheckpointIntervalTurns = 1;
				Settings->ReplayTurnBatchSize = 1;
			}

			~FScopedFastReplayMaintenance()
			{
				Settings->ReplayCheckpointIntervalTurns =
					PreviousCheckpointInterval;
				Settings->ReplayTurnBatchSize = PreviousTurnBatchSize;
			}

			USeinARTSCoreSettings* Settings = nullptr;
			int32 PreviousCheckpointInterval = 0;
			int32 PreviousTurnBatchSize = 0;
		};

		struct FScopedReplayWorkerDrain
		{
			USeinReplayWriter* Writer = nullptr;

			~FScopedReplayWorkerDrain()
			{
				if (Writer)
				{
					Writer->AbortAndDrainBackgroundWorkForTests();
				}
			}
		};

		bool PumpGameThreadTasksUntil(
			TFunctionRef<bool()> IsComplete,
			double TimeoutSeconds = 10.0)
		{
			const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
			do
			{
				FTaskGraphInterface::Get().ProcessThreadUntilIdle(
					ENamedThreads::GameThread);
				if (IsComplete())
				{
					return true;
				}
				// Simulation advances only through explicit fixed ticks. This yield
				// waits solely for replay encode/storage workers to signal the game
				// thread through their production completion callbacks.
				FPlatformProcess::SleepNoStats(0.001f);
			}
			while (FPlatformTime::Seconds() < Deadline);

			FTaskGraphInterface::Get().ProcessThreadUntilIdle(
				ENamedThreads::GameThread);
			return IsComplete();
		}

		/** Freeze a legacy v8 executable fixture so reader compatibility tests do
		 *  not depend on the current writer, which intentionally emits v9 only. */
		FString WriteLegacyV8Replay(
			USeinWorldSubsystem& IdentityWorld,
			FSeinReplayHeader Header,
			TArray<FSeinReplayTurnRecord> Turns,
			int32 EndTick)
		{
			Header.EndTick = EndTick;
			FSeinReplay Replay;
			Replay.Header = MoveTemp(Header);
			Replay.Turns = MoveTemp(Turns);

			TArray<uint8> Body;
			FString Error;
			if (!FSeinReplayWireCodec::Encode(
					Replay,
					{
						IdentityWorld.GetCommandAdditionalDynamicPayloadStructs(),
						IdentityWorld.GetCommandAdditionalWireNames()
					},
					[&IdentityWorld](
						FGameplayTag Type,
						int32 Version,
						FSeinCommandSchemaDescriptor& Out)
					{
						return IdentityWorld.FindCommandSchema(Type, Version, Out);
					},
					Body,
					Error))
			{
				return FString();
			}

			TArray<uint8> FileBytes;
			if (!SeinReplayFormat::BuildPrefix(
					Replay.Header.CommandProtocolDigest,
					Replay.Header.MatchSettingsDigest,
					Replay.Header.BootstrapReceipt,
					Replay.Header.ConfigFingerprint,
					Body,
					FileBytes,
					Error))
			{
				return FString();
			}
			FileBytes.Append(Body);

			const FString Directory =
				FPaths::ProjectSavedDir() / TEXT("Replays");
			IFileManager::Get().MakeDirectory(*Directory, /*Tree=*/true);
			const FString Path = Directory / FString::Printf(
				TEXT("LegacyV8_%s.seinreplay"),
				*FGuid::NewGuid().ToString(EGuidFormats::Digits));
			return FFileHelper::SaveArrayToFile(FileBytes, *Path)
				? Path
				: FString();
		}

		FSeinReplayHeader MakePreparedWorldHeader(
			USeinWorldSubsystem& World)
		{
			FSeinReplayHeader Header;
			SeinReplayCompatibility::StampCurrent(Header, World.GetWorld());
			Header.CommandProtocolDigest = World.GetCommandProtocolDigest();
			Header.MatchSettingsDigest = World.GetMatchSettingsDigest();
			Header.BootstrapReceipt = World.GetMatchBootstrapReceipt();
			Header.ConfigFingerprint = World.GetConfigFingerprint();
			Header.RandomSeed = World.GetSessionSeed();
			Header.SettingsSnapshot = World.GetMatchSettings();
			Header.StartTick = World.GetCurrentTick();
			Header.RecordedAt = FDateTime::UtcNow();
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
			return Header;
		}

		FSeinMatchSettings MakeOnePlayerMatchSettings()
		{
			FSeinMatchSettings Settings;
			FSeinMatchSlot& Slot = Settings.Slots.AddDefaulted_GetRef();
			Slot.SlotIndex = 1;
			Slot.State = ESeinSlotState::Human;
			return Settings;
		}

		FSeinMatchSettings MakeTwoPlayerMatchSettings()
		{
			FSeinMatchSettings Settings;
			for (int32 SlotIndex = 1; SlotIndex <= 2; ++SlotIndex)
			{
				FSeinMatchSlot& Slot = Settings.Slots.AddDefaulted_GetRef();
				Slot.SlotIndex = SlotIndex;
				Slot.State = ESeinSlotState::Human;
			}
			return Settings;
		}

		FSeinCommand MakeCheckpointSessionMutation(
			int32 Tick,
			bool bGrant)
		{
			FSeinSetPairCapabilityCommandPayload Payload;
			Payload.SourcePlayer = FSeinPlayerID(1);
			Payload.TargetPlayer = FSeinPlayerID(2);
			Payload.CapabilityTag =
				SeinARTSTags::Relationship_Capability_ShareVision;
			Payload.SourceKindTag =
				SeinARTSTags::Relationship_Source_TeamBootstrap;
			Payload.SourceInstanceID = 0x43485054;
			Payload.bGrant = bGrant;

			FSeinCommand Command;
			Command.CommandType =
				SeinARTSTags::Command_Type_SetPairCapability;
			Command.SchemaVersion = 1;
			Command.Tick = Tick;
			Command.IssuerKind =
				ESeinCommandIssuerKind::MatchAdministrator;
			Command.Payload = FInstancedStruct::Make(Payload);
			return Command;
		}

		USeinReplayWriter* StartV9Recording(
			USeinWorldSubsystem& World,
			const FSeinMatchSettings& MatchSettings = FSeinMatchSettings())
		{
			FString Error;
			if (!SeinTestMatchBootstrap::Materialize(
					World,
					MatchSettings,
					/*SessionSeed=*/0,
					FName(TEXT("SeinFrameworkTests.ReplayV9Writer")),
					&Error)
				|| !SeinTestMatchBootstrap::Authorize(World, &Error))
			{
				return nullptr;
			}

			USeinReplayWriter* Writer =
				NewObject<USeinReplayWriter>(&World);
			Writer->StartRecording(MakePreparedWorldHeader(World));
			if (!Writer->IsRecording()
				|| !SeinTestMatchBootstrap::Start(World, &Error)
				|| !Writer->CaptureCheckpoint(/*bRequired=*/true))
			{
				return nullptr;
			}
			// Unit/integration fixtures advance the writer's observation contract
			// explicitly; do not leave the world ticker free-running beside them.
			World.StopSimulation();
			return Writer;
		}

		USeinReplayWriter* StartPopulatedV9Recording(
			USeinWorldSubsystem& World,
			const FSeinMatchSettings& MatchSettings,
			int32 Population,
			FString& OutPartialPath,
			FString& OutError)
		{
			bool bAuthoringSucceeded = true;
			const auto AuthorState = [&]()
			{
				for (const FSeinMatchSlot& Slot : MatchSettings.Slots)
				{
					if (Slot.State != ESeinSlotState::Human
						&& Slot.State != ESeinSlotState::AI)
					{
						continue;
					}
					World.RegisterPlayer(
						FSeinPlayerID(static_cast<uint8>(Slot.SlotIndex)),
						Slot.FactionID,
						Slot.TeamID);
				}
				for (int32 Index = 0; Index < Population; ++Index)
				{
					const FSeinEntityHandle Handle = World.SpawnAbstractEntity(
						FFixedTransform(FFixedVector(
							FFixedPoint::FromInt(Index * 100),
							FFixedPoint::Zero,
							FFixedPoint::Zero)),
						FSeinPlayerID::Neutral());
					if (!Handle.IsValid())
					{
						bAuthoringSucceeded = false;
						OutError = TEXT(
							"Could not spawn the checkpoint-session population.");
						return;
					}

					FSeinComponentStorageLifecycleProbe Probe;
					Probe.Values = {Index, Index * 3, Index % 11};
					World.AddComponent(Handle, Probe);
				}
			};
			if (!SeinTestMatchBootstrap::Materialize(
					World,
					AuthorState,
					MatchSettings,
					/*SessionSeed=*/0,
					FName(TEXT("SeinFrameworkTests.ReplayCheckpointSession")),
					&OutError)
				|| !bAuthoringSucceeded
				|| !SeinTestMatchBootstrap::Authorize(World, &OutError))
			{
				return nullptr;
			}

			USeinReplayWriter* Writer = NewObject<USeinReplayWriter>(&World);
			Writer->StartRecording(MakePreparedWorldHeader(World));
			OutPartialPath = Writer->GetActivePartialPath();
			if (!Writer->IsRecording()
				|| !SeinTestMatchBootstrap::Start(World, &OutError)
				|| !Writer->CaptureCheckpoint(/*bRequired=*/true))
			{
				if (OutError.IsEmpty())
				{
					OutError = TEXT(
						"Could not start the populated replay checkpoint session.");
				}
				return nullptr;
			}
			World.StopSimulation();
			return Writer;
		}
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
		FScopedReplayFile PartialFile{Writer->GetActivePartialPath()};
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
		FScopedReplayFile PartialFile{Writer->GetActivePartialPath()};
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
		FScopedReplayFile PartialFile{Writer->GetActivePartialPath()};

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
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* SourceWorld =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(SourceWorld));
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

		USeinReplayWriter* Writer = StartV9Recording(*SourceWorld);
		ASSERT_THAT(IsNotNull(Writer));
		Writer->RecordTurn(FirstTurn, {});
		Writer->RecordTurn(FirstTurn + 1, {}); // unapplied input-delay tail
		for (int32 Tick = 1; Tick <= EndTick; ++Tick)
		{
			Writer->ObserveCompletedTick(Tick);
		}
		Writer->QueueAppliedProgressForTests();
		// Ordinary maintenance transfers the eligible batch to the worker but
		// does not claim it durable or release resident bytes until completion is
		// observed on the game thread. The forced seam below must drain that same
		// in-flight append before publishing Progress.
		ASSERT_THAT(AreEqual(0, Writer->GetPersistedTurnCount()));
		ASSERT_THAT(AreEqual(2, Writer->GetResidentTurnCount()));
		Writer->FlushAppliedProgressForTests();
		ASSERT_THAT(IsTrue(Writer->IsRecording()));
		ASSERT_THAT(AreEqual(1, Writer->GetPersistedTurnCount()));
		ASSERT_THAT(AreEqual(1, Writer->GetResidentTurnCount()));

		// Model interruption before Finish: the applied turn and Progress are
		// durable, while the input-delay future turn exists only in RAM.
		TArray<uint8> LivePartialBytes;
		ASSERT_THAT(IsTrue(FFileHelper::LoadFileToArray(
			LivePartialBytes, *Writer->GetActivePartialPath())));
		FScopedReplayFile LivePartialFile{
			FPaths::ProjectSavedDir()
				/ TEXT("Replays")
				/ FString::Printf(
					TEXT("LiveInterrupted_%s.seinreplay.partial"),
					*FGuid::NewGuid().ToString(EGuidFormats::Digits))};
		ASSERT_THAT(IsTrue(FFileHelper::SaveArrayToFile(
			LivePartialBytes, *LivePartialFile.Path)));
		USeinReplayReader* LivePartialReader = NewObject<USeinReplayReader>(
			&Spawner.GetWorld());
		ASSERT_THAT(IsTrue(
			LivePartialReader->LoadFromFile(LivePartialFile.Path)));
		ASSERT_THAT(AreEqual(1, LivePartialReader->GetTurnCount()));
		ASSERT_THAT(AreEqual(
			EndTick, LivePartialReader->GetHeader().EndTick));

		FScopedReplayFile ReplayFile{Writer->FinishRecording()};
		ASSERT_THAT(IsFalse(ReplayFile.Path.IsEmpty()));
		TArray<uint8> V9Bytes;
		ASSERT_THAT(IsTrue(FFileHelper::LoadFileToArray(
			V9Bytes, *ReplayFile.Path)));
		ASSERT_THAT(IsTrue(
			V9Bytes.Num() >= SeinReplayJournalFormat::PrefixBytes));
		ASSERT_THAT(AreEqual(static_cast<uint8>('9'), V9Bytes[7]));

		// Recomputing only the outer frame digest must not hide a damaged inner
		// snapshot envelope. BuildFrame performs the same full
		// semantic validation as the reader before it signs a Checkpoint frame.
		int64 FrameOffset = SeinReplayJournalFormat::PrefixBytes;
		bool bFoundCheckpoint = false;
		FString JournalError;
		while (FrameOffset + SeinReplayJournalFormat::FrameHeaderBytes
			<= V9Bytes.Num())
		{
			SeinReplayJournalFormat::FFrameHeader FrameHeader;
			ASSERT_THAT(IsTrue(
				SeinReplayJournalFormat::ParseFrameHeader(
					MakeArrayView(
						V9Bytes.GetData() + FrameOffset,
						SeinReplayJournalFormat::FrameHeaderBytes),
					FrameHeader,
					JournalError)));
			const int64 PayloadOffset = FrameOffset
				+ SeinReplayJournalFormat::FrameHeaderBytes;
			const int64 NextFrameOffset = PayloadOffset
				+ static_cast<int64>(FrameHeader.PayloadBytes);
			ASSERT_THAT(IsTrue(NextFrameOffset <= V9Bytes.Num()));
			if (FrameHeader.Type
				== SeinReplayJournalFormat::EFrameType::Checkpoint)
			{
				TArray<uint8> DamagedCheckpoint;
				DamagedCheckpoint.Append(
					V9Bytes.GetData() + PayloadOffset,
					static_cast<int32>(FrameHeader.PayloadBytes));
				DamagedCheckpoint.Last() ^= 1u;
				TArray<uint8> RefusedFrame;
				SeinReplayJournalFormat::FFrameHeader RefusedHeader;
				ASSERT_THAT(IsFalse(
					SeinReplayJournalFormat::BuildFrame(
						FrameHeader.Type,
						FrameHeader.Flags,
						FrameHeader.Sequence,
						FrameHeader.FirstTurn,
						FrameHeader.LastTurn,
						FrameHeader.TimelineTick,
						FrameHeader.PreviousDigest,
						DamagedCheckpoint,
						RefusedFrame,
						RefusedHeader,
						JournalError)));
				ASSERT_THAT(IsTrue(
					JournalError.Contains(TEXT("Checkpoint"))));
				bFoundCheckpoint = true;
				break;
			}
			FrameOffset = NextFrameOffset;
		}
		ASSERT_THAT(IsTrue(bFoundCheckpoint));

		USeinReplayReader* Reader = NewObject<USeinReplayReader>(
			&Spawner.GetWorld());
		ASSERT_THAT(IsTrue(Reader->LoadFromFile(ReplayFile.Path)));
		ASSERT_THAT(AreEqual(1, Reader->GetTurnCount()));
		ASSERT_THAT(AreEqual(EndTick, Reader->GetHeader().EndTick));

		// A complete but corrupt terminal frame is not a recoverable crash tail,
		// even when the file carries the partial extension.
		TArray<uint8> CorruptBytes = V9Bytes;
		CorruptBytes.Last() ^= 1u;
		FScopedReplayFile CorruptPartialFile{
			FPaths::ProjectSavedDir()
				/ TEXT("Replays")
				/ FString::Printf(
					TEXT("Corrupt_%s.seinreplay.partial"),
					*FGuid::NewGuid().ToString(EGuidFormats::Digits))};
		ASSERT_THAT(IsTrue(FFileHelper::SaveArrayToFile(
			CorruptBytes, *CorruptPartialFile.Path)));
		TestRunner->AddExpectedError(
			TEXT("rejected v9 journal"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		USeinReplayReader* CorruptReader = NewObject<USeinReplayReader>(
			&Spawner.GetWorld());
		ASSERT_THAT(IsFalse(
			CorruptReader->LoadFromFile(CorruptPartialFile.Path)));

		// A partial recording may ignore only a torn terminal frame. The forced
		// Progress immediately before Finalize still proves this exact frontier.
		const TArray<uint8> FullV9Bytes = V9Bytes;
		ASSERT_THAT(IsTrue(V9Bytes.Num()
			> SeinReplayJournalFormat::FrameHeaderBytes));
		V9Bytes.SetNum(V9Bytes.Num() - 8, EAllowShrinking::No);
		FScopedReplayFile PartialFile{
			FPaths::ProjectSavedDir()
				/ TEXT("Replays")
				/ FString::Printf(
					TEXT("Interrupted_%s.seinreplay.partial"),
					*FGuid::NewGuid().ToString(EGuidFormats::Digits))};
		ASSERT_THAT(IsTrue(FFileHelper::SaveArrayToFile(
			V9Bytes, *PartialFile.Path)));
		USeinReplayReader* PartialReader = NewObject<USeinReplayReader>(
			&Spawner.GetWorld());
		ASSERT_THAT(IsTrue(PartialReader->LoadFromFile(PartialFile.Path)));
		ASSERT_THAT(AreEqual(1, PartialReader->GetTurnCount()));
		ASSERT_THAT(AreEqual(EndTick, PartialReader->GetHeader().EndTick));

		// The second recoverable tear shape: a tail torn INSIDE the final
		// frame header (fewer than FrameHeaderBytes remaining). The durable
		// frontier before the torn frame must still load.
		int64 LastFrameOffset = SeinReplayJournalFormat::PrefixBytes;
		{
			int64 WalkOffset = SeinReplayJournalFormat::PrefixBytes;
			while (WalkOffset + SeinReplayJournalFormat::FrameHeaderBytes
				<= FullV9Bytes.Num())
			{
				SeinReplayJournalFormat::FFrameHeader WalkHeader;
				ASSERT_THAT(IsTrue(
					SeinReplayJournalFormat::ParseFrameHeader(
						MakeArrayView(
							FullV9Bytes.GetData() + WalkOffset,
							SeinReplayJournalFormat::FrameHeaderBytes),
						WalkHeader,
						JournalError)));
				LastFrameOffset = WalkOffset;
				WalkOffset += SeinReplayJournalFormat::FrameHeaderBytes
					+ static_cast<int64>(WalkHeader.PayloadBytes);
			}
			ASSERT_THAT(IsTrue(WalkOffset == FullV9Bytes.Num()));
		}
		TArray<uint8> TornHeaderBytes = FullV9Bytes;
		TornHeaderBytes.SetNum(
			static_cast<int32>(LastFrameOffset + 32), EAllowShrinking::No);
		FScopedReplayFile TornHeaderFile{
			FPaths::ProjectSavedDir()
				/ TEXT("Replays")
				/ FString::Printf(
					TEXT("TornHeader_%s.seinreplay.partial"),
					*FGuid::NewGuid().ToString(EGuidFormats::Digits))};
		ASSERT_THAT(IsTrue(FFileHelper::SaveArrayToFile(
			TornHeaderBytes, *TornHeaderFile.Path)));
		USeinReplayReader* TornHeaderReader = NewObject<USeinReplayReader>(
			&Spawner.GetWorld());
		ASSERT_THAT(IsTrue(
			TornHeaderReader->LoadFromFile(TornHeaderFile.Path)));
		ASSERT_THAT(AreEqual(1, TornHeaderReader->GetTurnCount()));
		ASSERT_THAT(AreEqual(
			EndTick, TornHeaderReader->GetHeader().EndTick));

		// Seeking outside the recorded range must reject cleanly, before any
		// world interaction.
		TestRunner->AddExpectedError(
			TEXT("is outside 0.."),
			EAutomationExpectedErrorFlags::Contains, 2, false);
		ASSERT_THAT(IsFalse(TornHeaderReader->PlayFromTick(-1)));
		ASSERT_THAT(IsFalse(TornHeaderReader->PlayFromTick(
			TornHeaderReader->GetHeader().EndTick + 1)));
	}

	TEST(ReplayV9StaleFailureCallbackCannotStopANewerPlayback,
		"SeinARTS.Unit.Network.ReplayFormat.V9")
	{
		USeinReplayReader* Reader = NewObject<USeinReplayReader>();
		ASSERT_THAT(IsNotNull(Reader));
		const uint64 OldGeneration =
			FSeinReplayReaderTestAccess::BeginSyntheticPlayback(*Reader);
		Reader->Stop();
		const uint64 NewGeneration =
			FSeinReplayReaderTestAccess::BeginSyntheticPlayback(*Reader);
		ASSERT_THAT(IsTrue(NewGeneration != OldGeneration));

		FSeinReplayReaderTestAccess::DeliverFailure(
			*Reader, OldGeneration);
		ASSERT_THAT(IsTrue(
			FSeinReplayReaderTestAccess::IsPlaying(*Reader)));
		ASSERT_THAT(IsTrue(
			FSeinReplayReaderTestAccess::IsFailureScheduled(*Reader)));
		FSeinReplayReaderTestAccess::EndSyntheticPlayback(*Reader);
	}

	TEST(ReplayV9StreamsBeyondTheLegacySixtyFourMiBLimit,
		"SeinARTS.Integration.Network.Replay")
	{
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		USeinReplayWriter* Writer = StartV9Recording(
			*Source, MakeOnePlayerMatchSettings());
		ASSERT_THAT(IsNotNull(Writer));

		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		const int32 TicksPerTurn = Settings->TurnRate > 0
			? FMath::Max(1, Settings->SimulationTickRate / Settings->TurnRate)
			: 1;
		const int32 FirstTurn = Settings->InputDelayTurns > 0
			? Settings->InputDelayTurns
			: 3;

		// Observer selections are valid replay traffic and let this regression
		// cross the retired v8 whole-body ceiling in only a handful of turns.
		TArray<FSeinEntityHandle> LargeSelection;
		LargeSelection.Reserve(4096);
		for (int32 Index = 1; Index <= 4096; ++Index)
		{
			LargeSelection.Emplace(Index, 1);
		}
		TArray<FSeinCommand> Commands;
		Commands.Reserve(224);
		for (int32 Index = 0; Index < 224; ++Index)
		{
			FSeinCommand Command = FSeinCommand::MakeSelectionChangedCommand(
				FSeinPlayerID(1), LargeSelection, 0);
			Command.IssuerKind = ESeinCommandIssuerKind::Player;
			Commands.Add(MoveTemp(Command));
		}

		int32 ObservedTick = 0;
		int32 Turn = FirstTurn;
		constexpr int32 MaximumRegressionTurns = 32;
		while (Writer->GetPersistedBytes() <= SeinReplayFormat::MaxBodyBytes
			&& Turn < FirstTurn + MaximumRegressionTurns)
		{
			const int32 CanonicalTick = Turn * TicksPerTurn;
			for (FSeinCommand& Command : Commands)
			{
				Command.Tick = CanonicalTick;
			}
			Writer->RecordTurn(Turn, Commands);
			while (ObservedTick < CanonicalTick)
			{
				Writer->ObserveCompletedTick(++ObservedTick);
			}
			Writer->FlushAppliedProgressForTests();
			ASSERT_THAT(IsTrue(Writer->IsRecording()));
			ASSERT_THAT(AreEqual(0, Writer->GetResidentTurnCount()));
			++Turn;
		}

		const int32 ExpectedTurns = Turn - FirstTurn;
		ASSERT_THAT(IsTrue(
			Writer->GetPersistedBytes() > SeinReplayFormat::MaxBodyBytes));
		ASSERT_THAT(AreEqual(ExpectedTurns, Writer->GetPersistedTurnCount()));
		ASSERT_THAT(IsTrue(Writer->GetPeakResidentTurnCount() <= 1));
		ASSERT_THAT(IsTrue(
			Writer->GetPeakResidentBytes() > 0
			&& Writer->GetPeakResidentBytes()
				<= FSeinOpaqueCommandBatch::MaxBytes));

		FScopedReplayFile ReplayFile{Writer->FinishRecording()};
		ASSERT_THAT(IsFalse(ReplayFile.Path.IsEmpty()));
		ASSERT_THAT(IsTrue(
			IFileManager::Get().FileSize(*ReplayFile.Path)
				> static_cast<int64>(SeinReplayFormat::MaxBodyBytes)));

		FActorTestSpawner TargetSpawner;
		USeinReplayReader* Reader = NewObject<USeinReplayReader>(
			&TargetSpawner.GetWorld());
		ASSERT_THAT(IsTrue(Reader->LoadFromFile(ReplayFile.Path)));
		ASSERT_THAT(AreEqual(ExpectedTurns, Reader->GetTurnCount()));
		ASSERT_THAT(AreEqual(0, Reader->GetResidentTurnCount()));
	}

	TEST(ReplayV9CheckpointSeekMatchesTheSourceCanonicalRoot,
		"SeinARTS.Determinism.Network.Replay")
	{
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		USeinReplayWriter* Writer = StartV9Recording(
			*Source, MakeOnePlayerMatchSettings());
		ASSERT_THAT(IsNotNull(Writer));

		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		const int32 TicksPerTurn = Settings->TurnRate > 0
			? FMath::Max(1, Settings->SimulationTickRate / Settings->TurnRate)
			: 1;
		const int32 FirstTurn = Settings->InputDelayTurns > 0
			? Settings->InputDelayTurns
			: 3;
		const int32 CheckpointTick = FirstTurn * TicksPerTurn;
		const int32 EndTick = (FirstTurn + 1) * TicksPerTurn;
		Writer->RecordTurn(FirstTurn, {});
		FSeinCommand Concede;
		Concede.PlayerID = FSeinPlayerID(1);
		Concede.IssuerKind = ESeinCommandIssuerKind::Player;
		Concede.CommandType = SeinARTSTags::Command_Type_ConcedeMatch;
		Concede.Tick = EndTick;
		Writer->RecordTurn(FirstTurn + 1, {Concede});
		FString Error;
		FGuid SourceCheckpointRoot;

		ASSERT_THAT(IsTrue(Source->StartSimulation()));
		for (int32 ExpectedTick = 1; ExpectedTick <= EndTick; ++ExpectedTick)
		{
			if (ExpectedTick == EndTick)
			{
				Source->SubmitLocalCommandDraft(Concede);
			}
			FTSTicker::GetCoreTicker().Tick(
				Source->GetFixedDeltaTimeSeconds());
			ASSERT_THAT(AreEqual(ExpectedTick, Source->GetCurrentTick()));
			Writer->ObserveCompletedTick(ExpectedTick);
			if (ExpectedTick == CheckpointTick)
			{
				ASSERT_THAT(IsTrue(
					Writer->CaptureCheckpoint(/*bRequired=*/false)));
				ASSERT_THAT(IsTrue(Source->ComputeCanonicalStateRoot(
					SourceCheckpointRoot, Error)));
			}
		}

		FGuid SourceRoot;
		ASSERT_THAT(IsTrue(
			Source->ComputeCanonicalStateRoot(SourceRoot, Error)));
		Source->StopSimulation();
		FScopedReplayFile ReplayFile{Writer->FinishRecording()};
		ASSERT_THAT(IsFalse(ReplayFile.Path.IsEmpty()));

		// A seek must first reproduce the selected checkpoint exactly, before
		// any later journal turn is allowed to advance the restored world.
		FActorTestSpawner CheckpointProbeSpawner;
		USeinWorldSubsystem* CheckpointProbe =
			CheckpointProbeSpawner.GetWorld()
				.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(CheckpointProbe));
		USeinReplayReader* CheckpointProbeReader =
			NewObject<USeinReplayReader>(&CheckpointProbeSpawner.GetWorld());
		ASSERT_THAT(IsTrue(
			CheckpointProbeReader->LoadFromFile(ReplayFile.Path)));
		ASSERT_THAT(IsTrue(
			CheckpointProbeReader->PlayFromTick(CheckpointTick)));
		CheckpointProbeReader->Stop();
		FGuid RestoredCheckpointRoot;
		ASSERT_THAT(IsTrue(CheckpointProbe->ComputeCanonicalStateRoot(
			RestoredCheckpointRoot, Error)));
		ASSERT_THAT(AreEqual(
			SourceCheckpointRoot.ToString(EGuidFormats::Digits),
			RestoredCheckpointRoot.ToString(EGuidFormats::Digits)));

		FActorTestSpawner TargetSpawner;
		USeinWorldSubsystem* Target =
			TargetSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Target));
		USeinReplayReader* Reader = NewObject<USeinReplayReader>(
			&TargetSpawner.GetWorld());
		ASSERT_THAT(IsTrue(Reader->LoadFromFile(ReplayFile.Path)));
		ASSERT_THAT(IsTrue(
			Reader->PlayFromTick(CheckpointTick + 1)));
		for (int32 Pump = 0; Pump < EndTick * 4 && Reader->IsPlaying(); ++Pump)
		{
			FTSTicker::GetCoreTicker().Tick(
				Target->GetFixedDeltaTimeSeconds());
		}
		ASSERT_THAT(AreEqual(EndTick, Target->GetCurrentTick()));
		ASSERT_THAT(IsFalse(Reader->IsPlaying()));

		// Natural replay completion explicitly releases the scheduler. Re-arm
		// it without pumping a tick so the canonical-root API has its required
		// active timeline boundary for the source/target comparison.
		ASSERT_THAT(IsTrue(Target->StartSimulation()));
		FGuid TargetRoot;
		ASSERT_THAT(IsTrue(
			Target->ComputeCanonicalStateRoot(TargetRoot, Error)));
		ASSERT_THAT(AreEqual(
			SourceRoot.ToString(EGuidFormats::Digits),
			TargetRoot.ToString(EGuidFormats::Digits)));
		Target->StopSimulation();

		// Tick-zero Play uses the mandatory initial checkpoint and must converge
		// to the same state after executing the post-checkpoint command.
		FActorTestSpawner FullTargetSpawner;
		USeinWorldSubsystem* FullTarget =
			FullTargetSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(FullTarget));
		USeinReplayReader* FullReader = NewObject<USeinReplayReader>(
			&FullTargetSpawner.GetWorld());
		ASSERT_THAT(IsTrue(FullReader->LoadFromFile(ReplayFile.Path)));
		ASSERT_THAT(IsTrue(FullReader->Play()));
		for (int32 Pump = 0;
			Pump < EndTick * 4 && FullReader->IsPlaying();
			++Pump)
		{
			FTSTicker::GetCoreTicker().Tick(
				FullTarget->GetFixedDeltaTimeSeconds());
		}
		ASSERT_THAT(AreEqual(EndTick, FullTarget->GetCurrentTick()));
		ASSERT_THAT(IsFalse(FullReader->IsPlaying()));
		ASSERT_THAT(IsTrue(FullTarget->StartSimulation()));
		FGuid FullTargetRoot;
		ASSERT_THAT(IsTrue(FullTarget->ComputeCanonicalStateRoot(
			FullTargetRoot, Error)));
		ASSERT_THAT(AreEqual(
			SourceRoot.ToString(EGuidFormats::Digits),
			FullTargetRoot.ToString(EGuidFormats::Digits)));
		FullTarget->StopSimulation();
	}

	TEST(ReplayPeriodicCheckpointSessionStaysBoundedAndEveryCheckpointSeeksExact,
		"SeinARTS.Integration.Network.Replay")
	{
		constexpr int32 Population = 128;
		constexpr int32 PeriodicCheckpointCount = 25;
		FScopedFastReplayMaintenance MaintenanceSettings;
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		FString Error;
		FString PartialPath;
		USeinReplayWriter* Writer = StartPopulatedV9Recording(
			*Source,
			MakeTwoPlayerMatchSettings(),
			Population,
			PartialPath,
			Error);
		FScopedReplayFile ReplayFile{MoveTemp(PartialPath)};
		if (!Writer)
		{
			UE_LOG(LogTemp, Error,
				TEXT("Populated replay checkpoint session failed: %s"),
				*Error);
		}
		ASSERT_THAT(IsNotNull(Writer));
		if (!Writer)
		{
			return;
		}
		ASSERT_THAT(AreEqual(1, Writer->GetPersistedCheckpointCount()));
		ASSERT_THAT(AreEqual(
			1, Source->GetComponentStorageSnapshotCacheEntryCountForTests()));
		ASSERT_THAT(IsTrue(
			Source->HasComponentStorageSnapshotCacheEntryForTests(
				FSeinComponentStorageLifecycleProbe::StaticStruct())));
		const uint64 StableCacheBytes =
			Source->GetComponentStorageSnapshotCacheBytesForTests();
		const uint64 StableAllocatedCacheBytes =
			Source
				->CalculateComponentStorageSnapshotCacheAllocatedBytesForTests();
		const int64 InitialCacheHits =
			Source->GetComponentStorageSnapshotCacheHitCountForTests();
		ASSERT_THAT(IsTrue(StableCacheBytes > 0));
		ASSERT_THAT(AreEqual(
			StableCacheBytes,
			Source->CalculateComponentStorageSnapshotCachePayloadBytesForTests()));
		ASSERT_THAT(IsTrue(StableAllocatedCacheBytes >= StableCacheBytes));
		ASSERT_THAT(IsTrue(
			StableCacheBytes
				<= Source->GetDefaultComponentStorageSnapshotCacheBudgetForTests()));

		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		const int32 TicksPerTurn = FMath::Max(
			1, Settings->SimulationTickRate / Settings->TurnRate);
		const int32 FirstTurn = FMath::Max(1, Settings->InputDelayTurns);
		TArray<int32> CheckpointTicks;
		TArray<FGuid> CheckpointRoots;
		TArray<bool> CheckpointCapabilityStates;
		CheckpointTicks.Reserve(PeriodicCheckpointCount);
		CheckpointRoots.Reserve(PeriodicCheckpointCount);
		CheckpointCapabilityStates.Reserve(PeriodicCheckpointCount);
		ASSERT_THAT(IsTrue(Source->StartSimulation()));
		for (int32 CheckpointIndex = 0;
			CheckpointIndex < PeriodicCheckpointCount;
			++CheckpointIndex)
		{
			const int32 Turn = FirstTurn + CheckpointIndex;
			const int32 CheckpointTick = Turn * TicksPerTurn;
			const bool bGrant = (CheckpointIndex % 2) == 0;
			const FSeinCommand Mutation =
				MakeCheckpointSessionMutation(CheckpointTick, bGrant);
			Writer->RecordTurn(Turn, {Mutation});
			while (Source->GetCurrentTick() < CheckpointTick)
			{
				const int32 ExpectedTick = Source->GetCurrentTick() + 1;
				if (ExpectedTick == CheckpointTick)
				{
					Source->SubmitLocalCommandDraft(
						Mutation, /*bRequestMatchAdministration=*/true);
				}
				FTSTicker::GetCoreTicker().Tick(
					Source->GetFixedDeltaTimeSeconds());
				ASSERT_THAT(AreEqual(ExpectedTick, Source->GetCurrentTick()));
				Writer->ObserveCompletedTick(ExpectedTick);
			}
			ASSERT_THAT(AreEqual(
				bGrant,
				Source->HasPairCapability(
					FSeinPlayerID(1),
					FSeinPlayerID(2),
					SeinARTSTags::Relationship_Capability_ShareVision)));

			Writer->FlushAppliedProgressForTests();
			ASSERT_THAT(AreEqual(
				CheckpointIndex + 1, Writer->GetPersistedTurnCount()));
			Writer->RunScheduledMaintenanceForTests();
			ASSERT_THAT(IsTrue(Writer->IsCheckpointEncodePending()));
			Writer->ResolveCheckpointEncodeForTests();
			ASSERT_THAT(IsFalse(Writer->IsCheckpointEncodePending()));
			ASSERT_THAT(IsTrue(Writer->IsCheckpointAppendPending()));
			Writer->FlushAppliedProgressForTests();
			ASSERT_THAT(IsFalse(Writer->IsCheckpointAppendPending()));
			ASSERT_THAT(AreEqual(
				CheckpointIndex + 2,
				Writer->GetPersistedCheckpointCount()));

			ASSERT_THAT(AreEqual(
				StableCacheBytes,
				Source->GetComponentStorageSnapshotCacheBytesForTests()));
			ASSERT_THAT(AreEqual(
				StableCacheBytes,
				Source
					->CalculateComponentStorageSnapshotCachePayloadBytesForTests()));
			ASSERT_THAT(AreEqual(
				StableAllocatedCacheBytes,
				Source
					->CalculateComponentStorageSnapshotCacheAllocatedBytesForTests()));
			ASSERT_THAT(AreEqual(
				1,
				Source->GetComponentStorageSnapshotCacheEntryCountForTests()));
			ASSERT_THAT(AreEqual(
				InitialCacheHits + CheckpointIndex + 1,
				Source->GetComponentStorageSnapshotCacheHitCountForTests()));

			FGuid Root;
			ASSERT_THAT(IsTrue(
				Source->ComputeCanonicalStateRoot(Root, Error)));
			CheckpointTicks.Add(CheckpointTick);
			CheckpointRoots.Add(Root);
			CheckpointCapabilityStates.Add(bGrant);
		}

		ASSERT_THAT(AreEqual(
			PeriodicCheckpointCount, CheckpointTicks.Num()));
		ASSERT_THAT(AreEqual(
			CheckpointTicks.Num(), CheckpointRoots.Num()));
		ASSERT_THAT(AreEqual(
			CheckpointTicks.Num(), CheckpointCapabilityStates.Num()));
		const int32 FinalTick = Source->GetCurrentTick();
		const FGuid SourceFinalRoot = CheckpointRoots.Last();
		Source->StopSimulation();
		const FString PublishedPath = Writer->FinishRecording();
		ASSERT_THAT(IsFalse(PublishedPath.IsEmpty()));
		if (!PublishedPath.IsEmpty())
		{
			ReplayFile.Path = PublishedPath;
		}
		ASSERT_THAT(AreEqual(
			PeriodicCheckpointCount + 1,
			Writer->GetPersistedCheckpointCount()));

		for (int32 CheckpointIndex = 0;
			CheckpointIndex < CheckpointTicks.Num();
			++CheckpointIndex)
		{
			FActorTestSpawner ProbeSpawner;
			USeinWorldSubsystem* Probe =
				ProbeSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			ASSERT_THAT(IsNotNull(Probe));
			USeinReplayReader* ProbeReader = NewObject<USeinReplayReader>(
				&ProbeSpawner.GetWorld());
			ASSERT_THAT(IsTrue(ProbeReader->LoadFromFile(ReplayFile.Path)));
			ASSERT_THAT(IsTrue(
				ProbeReader->PlayFromTick(CheckpointTicks[CheckpointIndex])));
			const bool bTerminalCheckpoint =
				CheckpointIndex == CheckpointTicks.Num() - 1;
			ASSERT_THAT(AreEqual(
				!bTerminalCheckpoint, ProbeReader->IsPlaying()));
			ASSERT_THAT(AreEqual(
				!bTerminalCheckpoint, Probe->IsSimulationRunning()));
			if (!bTerminalCheckpoint)
			{
				ProbeReader->Stop();
			}
			ASSERT_THAT(AreEqual(
				CheckpointTicks[CheckpointIndex], Probe->GetCurrentTick()));
			if (bTerminalCheckpoint)
			{
				ASSERT_THAT(IsTrue(Probe->StartSimulation()));
			}
			ASSERT_THAT(AreEqual(
				CheckpointCapabilityStates[CheckpointIndex],
				Probe->HasPairCapability(
					FSeinPlayerID(1),
					FSeinPlayerID(2),
					SeinARTSTags::Relationship_Capability_ShareVision)));
			FGuid ProbeRoot;
			ASSERT_THAT(IsTrue(
				Probe->ComputeCanonicalStateRoot(ProbeRoot, Error)));
			ASSERT_THAT(AreEqual(
				CheckpointRoots[CheckpointIndex].ToString(EGuidFormats::Digits),
				ProbeRoot.ToString(EGuidFormats::Digits)));

			FSeinWorldSnapshot ColdAfterRestore;
			FSeinWorldSnapshot HotAfterRestore;
			const int64 MissesBeforeColdCapture =
				Probe->GetComponentStorageSnapshotCacheMissCountForTests();
			const int64 HitsBeforeColdCapture =
				Probe->GetComponentStorageSnapshotCacheHitCountForTests();
			Probe->CaptureSnapshot(ColdAfterRestore);
			const int64 HitsAfterColdCapture =
				Probe->GetComponentStorageSnapshotCacheHitCountForTests();
			const int64 MissesAfterColdCapture =
				Probe->GetComponentStorageSnapshotCacheMissCountForTests();
			Probe->CaptureSnapshot(HotAfterRestore);
			ASSERT_THAT(AreEqual(
				HitsBeforeColdCapture, HitsAfterColdCapture));
			ASSERT_THAT(AreEqual(
				MissesBeforeColdCapture + 1, MissesAfterColdCapture));
			ASSERT_THAT(AreEqual(
				StableCacheBytes,
				Probe->GetComponentStorageSnapshotCacheBytesForTests()));
			ASSERT_THAT(AreEqual(
				StableCacheBytes,
				Probe
					->CalculateComponentStorageSnapshotCachePayloadBytesForTests()));
			ASSERT_THAT(AreEqual(
				StableAllocatedCacheBytes,
				Probe
					->CalculateComponentStorageSnapshotCacheAllocatedBytesForTests()));
			ASSERT_THAT(AreEqual(
				HitsAfterColdCapture + 1,
				Probe->GetComponentStorageSnapshotCacheHitCountForTests()));
			ASSERT_THAT(AreEqual(
				MissesAfterColdCapture,
				Probe->GetComponentStorageSnapshotCacheMissCountForTests()));
			if (bTerminalCheckpoint)
			{
				Probe->StopSimulation();
			}
		}

		FActorTestSpawner FullTargetSpawner;
		USeinWorldSubsystem* FullTarget =
			FullTargetSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(FullTarget));
		USeinReplayReader* FullReader = NewObject<USeinReplayReader>(
			&FullTargetSpawner.GetWorld());
		ASSERT_THAT(IsTrue(FullReader->LoadFromFile(ReplayFile.Path)));
		ASSERT_THAT(IsTrue(FullReader->Play()));
		int32 FullCheckpointIndex = 0;
		for (int32 Pump = 0;
			Pump < FinalTick * 4 && FullReader->IsPlaying();
			++Pump)
		{
			FTSTicker::GetCoreTicker().Tick(
				FullTarget->GetFixedDeltaTimeSeconds());
			if (FullCheckpointIndex < CheckpointTicks.Num()
				&& FullTarget->GetCurrentTick()
					== CheckpointTicks[FullCheckpointIndex])
			{
				ASSERT_THAT(AreEqual(
					CheckpointCapabilityStates[FullCheckpointIndex],
					FullTarget->HasPairCapability(
						FSeinPlayerID(1),
						FSeinPlayerID(2),
						SeinARTSTags::Relationship_Capability_ShareVision)));
				++FullCheckpointIndex;
			}
		}
		ASSERT_THAT(IsFalse(FullReader->IsPlaying()));
		ASSERT_THAT(AreEqual(FinalTick, FullTarget->GetCurrentTick()));
		ASSERT_THAT(AreEqual(
			CheckpointTicks.Num(), FullCheckpointIndex));
		ASSERT_THAT(IsTrue(FullTarget->HasPairCapability(
			FSeinPlayerID(1),
			FSeinPlayerID(2),
			SeinARTSTags::Relationship_Capability_ShareVision)));
		ASSERT_THAT(IsFalse(FullTarget->IsSimulationRunning()));
		ASSERT_THAT(IsTrue(FullTarget->StartSimulation()));
		FGuid FullTargetRoot;
		ASSERT_THAT(IsTrue(
			FullTarget->ComputeCanonicalStateRoot(FullTargetRoot, Error)));
		ASSERT_THAT(AreEqual(
			SourceFinalRoot.ToString(EGuidFormats::Digits),
			FullTargetRoot.ToString(EGuidFormats::Digits)));
		FullTarget->StopSimulation();
	}

	TEST(ReplayCheckpointWorkersOverlapTicksAndCatchUpWithoutManualDrains,
		"SeinARTS.Integration.Network.Replay")
	{
		constexpr int32 Population = 128;
		constexpr int32 ControlledOverlapCycles = 8;
		constexpr int32 TurnsPerHeldEncode = 2;
		constexpr int32 TurnsPerHeldAppend = 2;
		constexpr int32 TurnsPerHeldCheckpoint =
			TurnsPerHeldEncode + TurnsPerHeldAppend;
		constexpr uint32 GateWaitMilliseconds = 10000;
		FScopedFastReplayMaintenance MaintenanceSettings;
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		FString Error;
		FString PartialPath;
		USeinReplayWriter* Writer = StartPopulatedV9Recording(
			*Source,
			MakeTwoPlayerMatchSettings(),
			Population,
			PartialPath,
			Error);
		FScopedReplayFile ReplayFile{MoveTemp(PartialPath)};
		if (!Writer)
		{
			UE_LOG(LogTemp, Error,
				TEXT("Overlapped replay checkpoint session failed: %s"),
				*Error);
		}
		ASSERT_THAT(IsNotNull(Writer));
		if (!Writer)
		{
			return;
		}
		FScopedReplayWorkerDrain WorkerDrain{Writer};
		ASSERT_THAT(AreEqual(1, Writer->GetPersistedCheckpointCount()));
		ASSERT_THAT(IsTrue(
			Writer->GetMaximumResidentTurnsForTests()
				> TurnsPerHeldCheckpoint));

		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		const int32 TicksPerTurn = FMath::Max(
			1, Settings->SimulationTickRate / Settings->TurnRate);
		int32 NextTurn = FMath::Max(1, Settings->InputDelayTurns);
		int32 MutationOrdinal = 0;
		uint64 ExpectedResidentBytes = 0;
		uint64 PeakExpectedResidentBytes = 0;
		TArray<int32> TurnTicks;
		TArray<bool> TurnCapabilityStates;
		TArray<int32> CheckpointTicks;
		TArray<FGuid> CheckpointRoots;
		TArray<bool> CheckpointCapabilityStates;
		TurnTicks.Reserve(1 + ControlledOverlapCycles * TurnsPerHeldCheckpoint);
		TurnCapabilityStates.Reserve(TurnTicks.Max());
		CheckpointTicks.Reserve(ControlledOverlapCycles + 1);
		CheckpointRoots.Reserve(ControlledOverlapCycles + 1);
		CheckpointCapabilityStates.Reserve(ControlledOverlapCycles + 1);

		const auto RecordAndAdvanceTurn = [&](bool bGrant)
		{
			const int32 Turn = NextTurn++;
			const int32 TurnTick = Turn * TicksPerTurn;
			const FSeinCommand Mutation =
				MakeCheckpointSessionMutation(TurnTick, bGrant);
			FSeinOpaqueCommandBatch Encoded;
			FString EncodeError;
			if (!FSeinNetCommandWireCodec::EncodeCommands(
					MakeArrayView(&Mutation, 1),
					SeinReplayFormat::MaxCommandsPerTurn,
					[Source](
						FGameplayTag Type,
						int32 Version,
						FSeinCommandSchemaDescriptor& Out)
					{
						return Source->FindCommandSchema(Type, Version, Out);
					},
					Encoded,
					EncodeError))
			{
				return false;
			}
			Writer->RecordEncodedTurn(Turn, Encoded);
			if (!Writer->IsRecording())
			{
				return false;
			}
			ExpectedResidentBytes += Encoded.Bytes.Num();
			PeakExpectedResidentBytes = FMath::Max(
				PeakExpectedResidentBytes, ExpectedResidentBytes);
			while (Source->GetCurrentTick() < TurnTick)
			{
				const int32 ExpectedTick = Source->GetCurrentTick() + 1;
				if (ExpectedTick == TurnTick)
				{
					Source->SubmitLocalCommandDraft(
						Mutation, /*bRequestMatchAdministration=*/true);
				}
				FTSTicker::GetCoreTicker().Tick(
					Source->GetFixedDeltaTimeSeconds());
				if (Source->GetCurrentTick() != ExpectedTick)
				{
					return false;
				}
				Writer->ObserveCompletedTick(ExpectedTick);
				FTaskGraphInterface::Get().ProcessThreadUntilIdle(
					ENamedThreads::GameThread);
				if (!Writer->IsRecording())
				{
					return false;
				}
			}
			TurnTicks.Add(TurnTick);
			TurnCapabilityStates.Add(bGrant);
			return Source->HasPairCapability(
				FSeinPlayerID(1),
				FSeinPlayerID(2),
				SeinARTSTags::Relationship_Capability_ShareVision) == bGrant;
		};

		ASSERT_THAT(IsTrue(Source->StartSimulation()));
		Writer->HoldNextCheckpointEncodeForTests();
		Writer->HoldNextCheckpointAppendForTests();
		ASSERT_THAT(IsTrue(RecordAndAdvanceTurn(/*bGrant=*/true)));
		++MutationOrdinal;
		ASSERT_THAT(IsTrue(PumpGameThreadTasksUntil([&]()
		{
			return Writer->IsCheckpointEncodePending()
				&& !Writer->IsCheckpointAppendPending()
				&& Writer->GetPersistedTurnCount() == 1
				&& Writer->GetResidentTurnCount() == 0
				&& Writer->GetResidentBytes() == 0;
		})));
		ExpectedResidentBytes = 0;
		ASSERT_THAT(IsTrue(Writer->WaitForHeldCheckpointEncodeForTests(
			GateWaitMilliseconds)));

		for (int32 Cycle = 0; Cycle < ControlledOverlapCycles; ++Cycle)
		{
			ASSERT_THAT(AreEqual(
				1 + Cycle, Writer->GetPersistedCheckpointCount()));
			ASSERT_THAT(IsTrue(Writer->IsCheckpointEncodePending()));
			ASSERT_THAT(IsFalse(Writer->IsCheckpointAppendPending()));
			ASSERT_THAT(AreEqual(0, Writer->GetResidentTurnCount()));
			ASSERT_THAT(AreEqual(
				static_cast<uint64>(0), Writer->GetResidentBytes()));
			CheckpointTicks.Add(Source->GetCurrentTick());
			CheckpointCapabilityStates.Add(
				Source->HasPairCapability(
					FSeinPlayerID(1),
					FSeinPlayerID(2),
					SeinARTSTags::Relationship_Capability_ShareVision));
			FGuid Root;
			ASSERT_THAT(IsTrue(
				Source->ComputeCanonicalStateRoot(Root, Error)));
			CheckpointRoots.Add(Root);

			const uint64 DurableBytesBeforeOverlap =
				Writer->GetPersistedBytes();
			const int64 DurableFileBytesBeforeOverlap =
				IFileManager::Get().FileSize(*ReplayFile.Path);
			ASSERT_THAT(AreEqual(
				static_cast<int64>(DurableBytesBeforeOverlap),
				DurableFileBytesBeforeOverlap));
			for (int32 TurnIndex = 0;
				TurnIndex < TurnsPerHeldEncode;
				++TurnIndex)
			{
				const bool bGrant = (MutationOrdinal % 2) == 0;
				ASSERT_THAT(IsTrue(RecordAndAdvanceTurn(bGrant)));
				++MutationOrdinal;
			}
			ASSERT_THAT(IsTrue(Writer->IsRecording()));
			ASSERT_THAT(IsTrue(Writer->IsCheckpointEncodePending()));
			ASSERT_THAT(IsFalse(Writer->IsCheckpointAppendPending()));
			ASSERT_THAT(AreEqual(
				TurnsPerHeldEncode,
				Writer->GetResidentTurnCount()));
			ASSERT_THAT(AreEqual(
				ExpectedResidentBytes, Writer->GetResidentBytes()));
			ASSERT_THAT(AreEqual(
				DurableBytesBeforeOverlap,
				Writer->GetPersistedBytes()));
			ASSERT_THAT(AreEqual(
				DurableFileBytesBeforeOverlap,
				IFileManager::Get().FileSize(*ReplayFile.Path)));

			Writer->ReleaseHeldCheckpointEncodeForTests();
			ASSERT_THAT(IsTrue(PumpGameThreadTasksUntil([&]()
			{
				return Writer->IsRecording()
					&& !Writer->IsCheckpointEncodePending()
					&& Writer->IsCheckpointAppendPending()
					&& Writer->GetResidentTurnCount()
						== TurnsPerHeldEncode
					&& Writer->GetResidentBytes()
						== ExpectedResidentBytes;
			})));
			ASSERT_THAT(IsTrue(
				Writer->WaitForHeldBackgroundAppendForTests(
					GateWaitMilliseconds)));
			for (int32 TurnIndex = 0;
				TurnIndex < TurnsPerHeldAppend;
				++TurnIndex)
			{
				const bool bGrant = (MutationOrdinal % 2) == 0;
				ASSERT_THAT(IsTrue(RecordAndAdvanceTurn(bGrant)));
				++MutationOrdinal;
			}
			ASSERT_THAT(IsTrue(Writer->IsRecording()));
			ASSERT_THAT(IsTrue(Writer->IsCheckpointAppendPending()));
			ASSERT_THAT(AreEqual(
				TurnsPerHeldCheckpoint,
				Writer->GetResidentTurnCount()));
			ASSERT_THAT(AreEqual(
				ExpectedResidentBytes, Writer->GetResidentBytes()));
			ASSERT_THAT(AreEqual(
				DurableBytesBeforeOverlap,
				Writer->GetPersistedBytes()));
			ASSERT_THAT(AreEqual(
				DurableFileBytesBeforeOverlap,
				IFileManager::Get().FileSize(*ReplayFile.Path)));

			Writer->ReleaseHeldBackgroundAppendForTests();
			const int32 ExpectedPersistedTurns =
				1 + (Cycle + 1) * TurnsPerHeldCheckpoint;
			if (Cycle < ControlledOverlapCycles - 1)
			{
				Writer->HoldNextCheckpointEncodeForTests();
				Writer->HoldNextCheckpointAppendForTests();
				ASSERT_THAT(IsTrue(PumpGameThreadTasksUntil([&]()
				{
					return Writer->IsRecording()
						&& Writer->IsCheckpointEncodePending()
						&& !Writer->IsCheckpointAppendPending()
						&& Writer->GetPersistedCheckpointCount()
							== Cycle + 2
						&& Writer->GetPersistedTurnCount()
							== ExpectedPersistedTurns
						&& Writer->GetResidentTurnCount() == 0
						&& Writer->GetResidentBytes() == 0;
				})));
				ExpectedResidentBytes = 0;
				ASSERT_THAT(IsTrue(
					Writer->WaitForHeldCheckpointEncodeForTests(
						GateWaitMilliseconds)));
			}
			else
			{
				ASSERT_THAT(IsTrue(PumpGameThreadTasksUntil([&]()
				{
					return Writer->IsRecording()
						&& !Writer->IsCheckpointEncodePending()
						&& !Writer->IsCheckpointAppendPending()
						&& Writer->GetPersistedCheckpointCount()
							== ControlledOverlapCycles + 2
						&& Writer->GetPersistedTurnCount()
							== ExpectedPersistedTurns
						&& Writer->GetResidentTurnCount() == 0
						&& Writer->GetResidentBytes() == 0;
				})));
				ExpectedResidentBytes = 0;
			}
		}

		CheckpointTicks.Add(Source->GetCurrentTick());
		CheckpointCapabilityStates.Add(
			Source->HasPairCapability(
				FSeinPlayerID(1),
				FSeinPlayerID(2),
				SeinARTSTags::Relationship_Capability_ShareVision));
		FGuid SourceFinalRoot;
		ASSERT_THAT(IsTrue(
			Source->ComputeCanonicalStateRoot(SourceFinalRoot, Error)));
		CheckpointRoots.Add(SourceFinalRoot);
		ASSERT_THAT(AreEqual(
			ControlledOverlapCycles + 1, CheckpointTicks.Num()));
		ASSERT_THAT(AreEqual(
			CheckpointTicks.Num(), CheckpointRoots.Num()));
		ASSERT_THAT(AreEqual(
			CheckpointTicks.Num(), CheckpointCapabilityStates.Num()));
		ASSERT_THAT(AreEqual(
			TurnsPerHeldCheckpoint,
			Writer->GetPeakResidentTurnCount()));
		ASSERT_THAT(IsTrue(
			Writer->GetPeakResidentTurnCount()
				< Writer->GetMaximumResidentTurnsForTests()));
		ASSERT_THAT(AreEqual(
			PeakExpectedResidentBytes, Writer->GetPeakResidentBytes()));
		const int32 FinalTick = Source->GetCurrentTick();
		Source->StopSimulation();
		const FString PublishedPath = Writer->FinishRecording();
		ASSERT_THAT(IsFalse(PublishedPath.IsEmpty()));
		if (!PublishedPath.IsEmpty())
		{
			ReplayFile.Path = PublishedPath;
		}
		ASSERT_THAT(AreEqual(
			ControlledOverlapCycles + 2,
			Writer->GetPersistedCheckpointCount()));

		USeinReplayReader* IndexReader = NewObject<USeinReplayReader>(
			&SourceSpawner.GetWorld());
		ASSERT_THAT(IsTrue(IndexReader->LoadFromFile(ReplayFile.Path)));
		ASSERT_THAT(AreEqual(TurnTicks.Num(), IndexReader->GetTurnCount()));
		ASSERT_THAT(AreEqual(
			TurnTicks.Num() + Writer->GetPersistedCheckpointCount(),
			IndexReader->GetIndexedFrameCount()));
		const TArray<int32> IndexedCheckpointTicks =
			FSeinReplayReaderTestAccess::GetJournalCheckpointTicks(*IndexReader);
		ASSERT_THAT(AreEqual(
			Writer->GetPersistedCheckpointCount(),
			IndexedCheckpointTicks.Num()));
		ASSERT_THAT(AreEqual(0, IndexedCheckpointTicks[0]));
		for (int32 CheckpointIndex = 0;
			CheckpointIndex < CheckpointTicks.Num();
			++CheckpointIndex)
		{
			ASSERT_THAT(AreEqual(
				CheckpointTicks[CheckpointIndex],
				IndexedCheckpointTicks[CheckpointIndex + 1]));
		}

		for (int32 CheckpointIndex = 0;
			CheckpointIndex < CheckpointTicks.Num();
			++CheckpointIndex)
		{
			FActorTestSpawner ProbeSpawner;
			USeinWorldSubsystem* Probe =
				ProbeSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			ASSERT_THAT(IsNotNull(Probe));
			USeinReplayReader* ProbeReader = NewObject<USeinReplayReader>(
				&ProbeSpawner.GetWorld());
			ASSERT_THAT(IsTrue(ProbeReader->LoadFromFile(ReplayFile.Path)));
			ASSERT_THAT(IsTrue(
				ProbeReader->PlayFromTick(CheckpointTicks[CheckpointIndex])));
			const bool bTerminalCheckpoint =
				CheckpointIndex == CheckpointTicks.Num() - 1;
			ASSERT_THAT(AreEqual(
				!bTerminalCheckpoint, ProbeReader->IsPlaying()));
			ASSERT_THAT(AreEqual(
				!bTerminalCheckpoint, Probe->IsSimulationRunning()));
			if (!bTerminalCheckpoint)
			{
				ProbeReader->Stop();
			}
			else
			{
				ASSERT_THAT(IsTrue(Probe->StartSimulation()));
			}
			ASSERT_THAT(AreEqual(
				CheckpointTicks[CheckpointIndex], Probe->GetCurrentTick()));
			ASSERT_THAT(AreEqual(
				CheckpointCapabilityStates[CheckpointIndex],
				Probe->HasPairCapability(
					FSeinPlayerID(1),
					FSeinPlayerID(2),
					SeinARTSTags::Relationship_Capability_ShareVision)));
			FGuid ProbeRoot;
			ASSERT_THAT(IsTrue(
				Probe->ComputeCanonicalStateRoot(ProbeRoot, Error)));
			ASSERT_THAT(AreEqual(
				CheckpointRoots[CheckpointIndex].ToString(
					EGuidFormats::Digits),
				ProbeRoot.ToString(EGuidFormats::Digits)));
			if (bTerminalCheckpoint)
			{
				Probe->StopSimulation();
			}
		}

		FActorTestSpawner FullTargetSpawner;
		USeinWorldSubsystem* FullTarget =
			FullTargetSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(FullTarget));
		USeinReplayReader* FullReader = NewObject<USeinReplayReader>(
			&FullTargetSpawner.GetWorld());
		ASSERT_THAT(IsTrue(FullReader->LoadFromFile(ReplayFile.Path)));
		ASSERT_THAT(IsTrue(FullReader->Play()));
		int32 TurnStateIndex = 0;
		for (int32 Pump = 0;
			Pump < FinalTick * 4 && FullReader->IsPlaying();
			++Pump)
		{
			FTSTicker::GetCoreTicker().Tick(
				FullTarget->GetFixedDeltaTimeSeconds());
			if (TurnStateIndex < TurnTicks.Num()
				&& FullTarget->GetCurrentTick() == TurnTicks[TurnStateIndex])
			{
				ASSERT_THAT(AreEqual(
					TurnCapabilityStates[TurnStateIndex],
					FullTarget->HasPairCapability(
						FSeinPlayerID(1),
						FSeinPlayerID(2),
						SeinARTSTags::Relationship_Capability_ShareVision)));
				++TurnStateIndex;
			}
		}
		ASSERT_THAT(IsFalse(FullReader->IsPlaying()));
		ASSERT_THAT(AreEqual(FinalTick, FullTarget->GetCurrentTick()));
		ASSERT_THAT(AreEqual(TurnTicks.Num(), TurnStateIndex));
		ASSERT_THAT(IsTrue(FullTarget->HasPairCapability(
			FSeinPlayerID(1),
			FSeinPlayerID(2),
			SeinARTSTags::Relationship_Capability_ShareVision)));
		ASSERT_THAT(IsFalse(FullTarget->IsSimulationRunning()));
		ASSERT_THAT(IsTrue(FullTarget->StartSimulation()));
		FGuid FullTargetRoot;
		ASSERT_THAT(IsTrue(
			FullTarget->ComputeCanonicalStateRoot(FullTargetRoot, Error)));
		ASSERT_THAT(AreEqual(
			SourceFinalRoot.ToString(EGuidFormats::Digits),
			FullTargetRoot.ToString(EGuidFormats::Digits)));
		FullTarget->StopSimulation();
	}

	TEST(ReplayScheduledCheckpointFinalizationDrainsBackgroundDurability,
		"SeinARTS.Integration.Network.Replay")
	{
		FScopedFastReplayMaintenance MaintenanceSettings;
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		USeinReplayWriter* Writer = StartV9Recording(
			*Source, MakeOnePlayerMatchSettings());
		ASSERT_THAT(IsNotNull(Writer));
		ASSERT_THAT(AreEqual(1, Writer->GetPersistedCheckpointCount()));
		ASSERT_THAT(IsFalse(Writer->IsCheckpointAppendPending()));

		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		const int32 TicksPerTurn = FMath::Max(
			1, Settings->SimulationTickRate / Settings->TurnRate);
		const int32 FirstTurn = Settings->InputDelayTurns;
		const int32 EndTick = FirstTurn * TicksPerTurn;
		Writer->RecordTurn(FirstTurn, {});
		ASSERT_THAT(IsTrue(Source->StartSimulation()));
		for (int32 Tick = 1; Tick <= EndTick; ++Tick)
		{
			FTSTicker::GetCoreTicker().Tick(
				Source->GetFixedDeltaTimeSeconds());
			ASSERT_THAT(AreEqual(Tick, Source->GetCurrentTick()));
			Writer->ObserveCompletedTick(Tick);
		}
		Source->StopSimulation();

		Writer->RunScheduledMaintenanceForTests();
		ASSERT_THAT(AreEqual(0, Writer->GetPersistedTurnCount()));
		Writer->FlushAppliedProgressForTests();
		ASSERT_THAT(AreEqual(1, Writer->GetPersistedTurnCount()));
		Writer->RunScheduledMaintenanceForTests();
		ASSERT_THAT(IsTrue(Writer->IsCheckpointEncodePending()));
		ASSERT_THAT(IsFalse(Writer->IsCheckpointAppendPending()));
		CollectGarbage(RF_NoFlags);
		ASSERT_THAT(IsTrue(Writer->IsCheckpointEncodePending()));
		Writer->ResolveCheckpointEncodeForTests();
		ASSERT_THAT(IsFalse(Writer->IsCheckpointEncodePending()));
		ASSERT_THAT(IsTrue(Writer->IsCheckpointAppendPending()));
		ASSERT_THAT(AreEqual(1, Writer->GetPersistedCheckpointCount()));

		FScopedReplayFile ReplayFile{Writer->FinishRecording()};
		ASSERT_THAT(IsFalse(ReplayFile.Path.IsEmpty()));
		ASSERT_THAT(IsFalse(Writer->IsCheckpointAppendPending()));
		ASSERT_THAT(AreEqual(2, Writer->GetPersistedCheckpointCount()));

		FActorTestSpawner TargetSpawner;
		USeinReplayReader* Reader = NewObject<USeinReplayReader>(
			&TargetSpawner.GetWorld());
		ASSERT_THAT(IsTrue(Reader->LoadFromFile(ReplayFile.Path)));
		ASSERT_THAT(AreEqual(1, Reader->GetTurnCount()));
		ASSERT_THAT(AreEqual(3, Reader->GetIndexedFrameCount()));
	}

	TEST(ReplayStorageFailuresStopRecordingAndPreserveThePartialJournal,
		"SeinARTS.Integration.Network.Replay")
	{
		FScopedFastReplayMaintenance MaintenanceSettings;
		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		const int32 TicksPerTurn = FMath::Max(
			1, Settings->SimulationTickRate / Settings->TurnRate);
		const int32 FirstTurn = Settings->InputDelayTurns;
		const int32 EndTick = FirstTurn * TicksPerTurn;

		FActorTestSpawner AsyncSpawner;
		USeinWorldSubsystem* AsyncWorld =
			AsyncSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(AsyncWorld));
		USeinReplayWriter* AsyncWriter = StartV9Recording(
			*AsyncWorld, MakeOnePlayerMatchSettings());
		ASSERT_THAT(IsNotNull(AsyncWriter));
		FScopedReplayFile AsyncPartial{AsyncWriter->GetActivePartialPath()};
		AsyncWriter->RecordTurn(FirstTurn, {});
		ASSERT_THAT(IsTrue(AsyncWorld->StartSimulation()));
		for (int32 Tick = 1; Tick <= EndTick; ++Tick)
		{
			FTSTicker::GetCoreTicker().Tick(
				AsyncWorld->GetFixedDeltaTimeSeconds());
			ASSERT_THAT(AreEqual(Tick, AsyncWorld->GetCurrentTick()));
			AsyncWriter->ObserveCompletedTick(Tick);
		}
		AsyncWorld->StopSimulation();
		AsyncWriter->FlushAppliedProgressForTests();
		ASSERT_THAT(AreEqual(1, AsyncWriter->GetPersistedTurnCount()));
		ASSERT_THAT(AreEqual(1, AsyncWriter->GetPersistedCheckpointCount()));

		AsyncWriter->FailNextBackgroundAppendForTests();
		AsyncWriter->RunScheduledMaintenanceForTests();
		ASSERT_THAT(IsTrue(AsyncWriter->IsCheckpointEncodePending()));
		AsyncWriter->ResolveCheckpointEncodeForTests();
		ASSERT_THAT(IsFalse(AsyncWriter->IsCheckpointEncodePending()));
		ASSERT_THAT(IsTrue(AsyncWriter->IsCheckpointAppendPending()));
		TestRunner->AddExpectedError(
			TEXT("synthetic background append failure"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		AsyncWriter->FlushAppliedProgressForTests();
		ASSERT_THAT(IsFalse(AsyncWriter->IsRecording()));
		ASSERT_THAT(IsFalse(AsyncWriter->IsCheckpointAppendPending()));
		ASSERT_THAT(AreEqual(1, AsyncWriter->GetPersistedCheckpointCount()));
		ASSERT_THAT(IsTrue(IFileManager::Get().FileExists(
			*AsyncPartial.Path)));

		FActorTestSpawner DeniedSpawner;
		USeinWorldSubsystem* DeniedWorld =
			DeniedSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(DeniedWorld));
		USeinReplayWriter* DeniedWriter = StartV9Recording(
			*DeniedWorld, MakeOnePlayerMatchSettings());
		ASSERT_THAT(IsNotNull(DeniedWriter));
		FScopedReplayFile DeniedPartial{DeniedWriter->GetActivePartialPath()};
		DeniedWriter->RecordTurn(FirstTurn, {});
		ASSERT_THAT(IsTrue(DeniedWorld->StartSimulation()));
		for (int32 Tick = 1; Tick <= EndTick; ++Tick)
		{
			FTSTicker::GetCoreTicker().Tick(
				DeniedWorld->GetFixedDeltaTimeSeconds());
			ASSERT_THAT(AreEqual(Tick, DeniedWorld->GetCurrentTick()));
			DeniedWriter->ObserveCompletedTick(Tick);
		}
		DeniedWorld->StopSimulation();
		IPlatformFile& PlatformFile =
			FPlatformFileManager::Get().GetPlatformFile();
		ASSERT_THAT(IsTrue(PlatformFile.SetReadOnly(
			*DeniedPartial.Path, true)));
		TestRunner->AddExpectedError(
			TEXT("replay append failed"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		DeniedWriter->FlushAppliedProgressForTests();
		PlatformFile.SetReadOnly(*DeniedPartial.Path, false);
		ASSERT_THAT(IsFalse(DeniedWriter->IsRecording()));
		ASSERT_THAT(AreEqual(0, DeniedWriter->GetPersistedTurnCount()));
		ASSERT_THAT(AreEqual(1, DeniedWriter->GetPersistedCheckpointCount()));
		ASSERT_THAT(IsTrue(IFileManager::Get().FileExists(
			*DeniedPartial.Path)));
	}

	TEST(ReplaySlowCheckpointStoragePreservesBoundedOrderedDurability,
		"SeinARTS.Integration.Network.Replay")
	{
		FScopedFastReplayMaintenance MaintenanceSettings;
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		USeinReplayWriter* Writer = StartV9Recording(
			*Source, MakeOnePlayerMatchSettings());
		ASSERT_THAT(IsNotNull(Writer));
		FScopedReplayFile PartialFile{Writer->GetActivePartialPath()};

		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		const int32 TicksPerTurn = FMath::Max(
			1, Settings->SimulationTickRate / Settings->TurnRate);
		const int32 FirstTurn = Settings->InputDelayTurns;
		const int32 FirstTurnEndTick = FirstTurn * TicksPerTurn;
		const int32 MaximumResidentTurns =
			Writer->GetMaximumResidentTurnsForTests();
		ASSERT_THAT(IsTrue(MaximumResidentTurns >= 8));

		Writer->RecordTurn(FirstTurn, {});
		ASSERT_THAT(IsTrue(Source->StartSimulation()));
		for (int32 Tick = 1; Tick <= FirstTurnEndTick; ++Tick)
		{
			FTSTicker::GetCoreTicker().Tick(
				Source->GetFixedDeltaTimeSeconds());
			ASSERT_THAT(AreEqual(Tick, Source->GetCurrentTick()));
			Writer->ObserveCompletedTick(Tick);
		}
		Source->StopSimulation();
		Writer->FlushAppliedProgressForTests();
		ASSERT_THAT(AreEqual(1, Writer->GetPersistedTurnCount()));
		ASSERT_THAT(AreEqual(0, Writer->GetResidentTurnCount()));

		Writer->HoldNextBackgroundAppendForTests();
		FScopedReplayWorkerDrain WorkerDrain{Writer};
		Writer->RunScheduledMaintenanceForTests();
		ASSERT_THAT(IsTrue(Writer->IsCheckpointEncodePending()));
		Writer->ResolveCheckpointEncodeForTests();
		ASSERT_THAT(IsTrue(Writer->IsCheckpointAppendPending()));
		constexpr uint32 GateWaitMilliseconds = 10000;
		ASSERT_THAT(IsTrue(Writer->WaitForHeldBackgroundAppendForTests(
			GateWaitMilliseconds)));

		const uint64 DurableBytesBeforePressure = Writer->GetPersistedBytes();
		ASSERT_THAT(AreEqual(1, Writer->GetPersistedTurnCount()));
		ASSERT_THAT(AreEqual(1, Writer->GetPersistedCheckpointCount()));
		ASSERT_THAT(AreEqual(
			static_cast<int64>(DurableBytesBeforePressure),
			IFileManager::Get().FileSize(*PartialFile.Path)));

		int32 ObservedTick = FirstTurnEndTick;
		ASSERT_THAT(IsTrue(Source->StartSimulation()));
		for (int32 Turn = FirstTurn + 1;
			Turn <= FirstTurn + MaximumResidentTurns;
			++Turn)
		{
			Writer->RecordTurn(Turn, {});
			const int32 TurnEndTick = Turn * TicksPerTurn;
			while (ObservedTick < TurnEndTick)
			{
				FTSTicker::GetCoreTicker().Tick(
					Source->GetFixedDeltaTimeSeconds());
				++ObservedTick;
				ASSERT_THAT(AreEqual(
					ObservedTick, Source->GetCurrentTick()));
				Writer->ObserveCompletedTick(ObservedTick);
			}
		}
		Source->StopSimulation();
		ASSERT_THAT(AreEqual(
			MaximumResidentTurns, Writer->GetResidentTurnCount()));
		ASSERT_THAT(AreEqual(
			MaximumResidentTurns, Writer->GetPeakResidentTurnCount()));
		ASSERT_THAT(AreEqual(1, Writer->GetPersistedTurnCount()));
		ASSERT_THAT(AreEqual(1, Writer->GetPersistedCheckpointCount()));
		ASSERT_THAT(AreEqual(
			DurableBytesBeforePressure, Writer->GetPersistedBytes()));
		ASSERT_THAT(AreEqual(
			static_cast<int64>(DurableBytesBeforePressure),
			IFileManager::Get().FileSize(*PartialFile.Path)));

		TFuture<bool> PressureRelease =
			Writer->ReleaseHeldBackgroundAppendAfterWriterWaitForTests(
				GateWaitMilliseconds);
		const int32 LastTurn = FirstTurn + MaximumResidentTurns + 1;
		Writer->RecordTurn(LastTurn, {});
		ASSERT_THAT(IsTrue(PressureRelease.Get()));
		ASSERT_THAT(IsTrue(Writer->IsRecording()));
		ASSERT_THAT(AreEqual(
			MaximumResidentTurns + 1, Writer->GetPersistedTurnCount()));
		ASSERT_THAT(AreEqual(2, Writer->GetPersistedCheckpointCount()));
		ASSERT_THAT(IsFalse(Writer->IsCheckpointAppendPending()));
		ASSERT_THAT(AreEqual(1, Writer->GetResidentTurnCount()));
		ASSERT_THAT(AreEqual(
			MaximumResidentTurns, Writer->GetPeakResidentTurnCount()));
		ASSERT_THAT(IsTrue(
			Writer->GetPeakResidentBytes()
				<= static_cast<uint64>(MaximumResidentTurns)
					* FSeinOpaqueCommandBatch::MaxBytes));
		ASSERT_THAT(IsTrue(
			Writer->GetPersistedBytes() > DurableBytesBeforePressure));
		ASSERT_THAT(AreEqual(
			static_cast<int64>(Writer->GetPersistedBytes()),
			IFileManager::Get().FileSize(*PartialFile.Path)));

		const int32 LastTurnEndTick = LastTurn * TicksPerTurn;
		ASSERT_THAT(IsTrue(Source->StartSimulation()));
		while (ObservedTick < LastTurnEndTick)
		{
			FTSTicker::GetCoreTicker().Tick(
				Source->GetFixedDeltaTimeSeconds());
			++ObservedTick;
			ASSERT_THAT(AreEqual(
				ObservedTick, Source->GetCurrentTick()));
			Writer->ObserveCompletedTick(ObservedTick);
		}
		Source->StopSimulation();
		Writer->FlushAppliedProgressForTests();
		const int32 ExpectedTurnCount = MaximumResidentTurns + 2;
		ASSERT_THAT(IsTrue(Writer->IsRecording()));
		ASSERT_THAT(AreEqual(
			ExpectedTurnCount, Writer->GetPersistedTurnCount()));
		ASSERT_THAT(AreEqual(0, Writer->GetResidentTurnCount()));

		FScopedReplayFile ReplayFile{Writer->FinishRecording()};
		ASSERT_THAT(IsFalse(ReplayFile.Path.IsEmpty()));
		USeinReplayReader* Reader = NewObject<USeinReplayReader>(
			&SourceSpawner.GetWorld());
		ASSERT_THAT(IsTrue(Reader->LoadFromFile(ReplayFile.Path)));
		ASSERT_THAT(AreEqual(ExpectedTurnCount, Reader->GetTurnCount()));
		ASSERT_THAT(AreEqual(LastTurnEndTick, Reader->GetHeader().EndTick));
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
		Turn.Commands[0].IssuerKind =
			ESeinCommandIssuerKind::MatchAdministrator;
		ASSERT_THAT(IsFalse(SeinReplayFormat::ValidateTurnEnvelope(
			Header, Turn, TicksPerTurn, InputDelayTurns, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("neutral player identity"))));
		Turn.Commands[0].PlayerID = FSeinPlayerID::Neutral();
		ASSERT_THAT(IsTrue(SeinReplayFormat::ValidateTurnEnvelope(
			Header, Turn, TicksPerTurn, InputDelayTurns, Error)));

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

		FScopedReplayFile SourceFile{WriteLegacyV8Replay(
			*World, MakeExecutableHeader(*World), {}, /*EndTick=*/0)};
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

		FScopedReplayFile ReplayFile{WriteLegacyV8Replay(
			*World, MakeExecutableHeader(*World), {}, /*EndTick=*/0)};
		ASSERT_THAT(IsFalse(ReplayFile.Path.IsEmpty()));

		USeinReplayReader* Reader = NewObject<USeinReplayReader>(
			&Spawner.GetWorld());
		ASSERT_THAT(IsTrue(Reader->LoadFromFile(ReplayFile.Path)));
		ASSERT_THAT(IsTrue(Reader->Play()));
		ASSERT_THAT(AreEqual(0, World->GetCurrentTick()));
		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
		ASSERT_THAT(IsFalse(Reader->IsPlaying()));
		ASSERT_THAT(IsTrue(World->GetMatchBootstrapState()
			== ESeinMatchBootstrapState::Consumed));
		ASSERT_THAT(IsFalse(
			FSeinWorldSubsystemTestAccess::IsSchedulerReserved(*World)));
	}

	TEST(ReplayNaturallyStopsAtInclusiveEndTickUnderCatchUp,
		"SeinARTS.Integration.Network.Replay")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		BindReplayTestMaterializer(*World);

		FScopedReplayFile ReplayFile{WriteLegacyV8Replay(
			*World, MakeExecutableHeader(*World), {}, /*EndTick=*/2)};
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

		FScopedReplayFile ReplayFile{WriteLegacyV8Replay(
			*World, Header, {}, /*EndTick=*/2)};
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

		FSeinReplayTurnRecord PauseTurn;
		PauseTurn.TurnId = FirstTurn;
		PauseTurn.Commands.Add(Pause);
		FScopedReplayFile ReplayFile{WriteLegacyV8Replay(
			*World, Header, {PauseTurn}, EndTick)};
		ASSERT_THAT(IsFalse(ReplayFile.Path.IsEmpty()));

		USeinReplayReader* Reader = NewObject<USeinReplayReader>(
			&Spawner.GetWorld());
		TestRunner->AddExpectedError(
			TEXT("contains unsupported pause-control"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(Reader->LoadFromFile(ReplayFile.Path)));
	}

	TEST(ReplayV9WriterRejectsPauseCommandsWithoutAFrozenTimeJournal,
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

		USeinReplayWriter* Writer = StartV9Recording(
			*World, MakeOnePlayerMatchSettings());
		ASSERT_THAT(IsNotNull(Writer));
		FScopedReplayFile PartialFile{Writer->GetActivePartialPath()};

		FSeinCommand Pause;
		Pause.PlayerID = FSeinPlayerID(1);
		Pause.IssuerKind = ESeinCommandIssuerKind::Player;
		Pause.CommandType = SeinARTSTags::Command_Type_PauseMatchRequest;
		Pause.Tick = FirstTurn * TicksPerTurn;

		TestRunner->AddExpectedError(
			TEXT("contains unsupported pause-control"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		Writer->RecordTurn(FirstTurn, {Pause});
		ASSERT_THAT(IsFalse(Writer->IsRecording()));
		ASSERT_THAT(IsTrue(IFileManager::Get().FileExists(
			*PartialFile.Path)));

		TestRunner->AddExpectedError(
			TEXT("recording was aborted by an invalid or oversized journal"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsTrue(Writer->FinishRecording().IsEmpty()));
	}

	TEST(ReplayV9TerminalCheckpointReleasesItsDormantScheduler,
		"SeinARTS.Integration.Network.Replay")
	{
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		USeinReplayWriter* Writer = StartV9Recording(
			*Source, MakeOnePlayerMatchSettings());
		ASSERT_THAT(IsNotNull(Writer));
		FScopedReplayFile ReplayFile{Writer->FinishRecording()};
		ASSERT_THAT(IsFalse(ReplayFile.Path.IsEmpty()));

		FActorTestSpawner TargetSpawner;
		USeinWorldSubsystem* Target =
			TargetSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Target));
		ASSERT_THAT(IsFalse(
			FSeinWorldSubsystemTestAccess::IsSchedulerReserved(*Target)));
		USeinReplayReader* Reader = NewObject<USeinReplayReader>(
			&TargetSpawner.GetWorld());
		ASSERT_THAT(IsTrue(Reader->LoadFromFile(ReplayFile.Path)));
		ASSERT_THAT(IsTrue(Reader->Play()));
		ASSERT_THAT(IsFalse(Reader->IsPlaying()));
		ASSERT_THAT(IsFalse(Target->IsSimulationRunning()));
		ASSERT_THAT(IsFalse(
			FSeinWorldSubsystemTestAccess::IsSchedulerReserved(*Target)));
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
		FSeinReplayTurnRecord RecordedTurn;
		RecordedTurn.TurnId = FirstTurn;
		RecordedTurn.Commands.Add(Recorded);
		FScopedReplayFile ReplayFile{WriteLegacyV8Replay(
			*World, Header, {RecordedTurn}, EndTick)};
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

		FScopedReplayFile ActiveFile{WriteLegacyV8Replay(
			*World, MakeExecutableHeader(*World), {}, /*EndTick=*/2)};
		ASSERT_THAT(IsFalse(ActiveFile.Path.IsEmpty()));

		FScopedReplayFile ReplacementFile{WriteLegacyV8Replay(
			*World, MakeExecutableHeader(*World), {}, /*EndTick=*/0)};
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
		FScopedReplayFile InvalidSettingsFile{WriteLegacyV8Replay(
			*World, InvalidSettingsHeader, {}, /*EndTick=*/0)};
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

		FSeinReplayTurnRecord BrokerTurn;
		BrokerTurn.TurnId = FirstTurn;
		BrokerTurn.Commands.Add(BrokerCommand);
		FScopedReplayFile ReplayFile{WriteLegacyV8Replay(
			*World, Header, {BrokerTurn}, EndTick)};
		ASSERT_THAT(IsFalse(ReplayFile.Path.IsEmpty()));

		USeinReplayReader* Reader = NewObject<USeinReplayReader>(
			&Spawner.GetWorld());
		ASSERT_THAT(IsTrue(Reader->LoadFromFile(ReplayFile.Path)));
		ASSERT_THAT(AreEqual(1, Reader->GetTurnCount()));
	}
}

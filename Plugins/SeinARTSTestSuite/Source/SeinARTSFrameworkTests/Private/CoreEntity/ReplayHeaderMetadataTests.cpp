#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Algo/Reverse.h"
#include "HAL/FileManager.h"
#include "Hash/Blake3.h"
#include "Lib/SeinReplayBPFL.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "TestTypes/SeinCommandSchemaTestTypes.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		void FinalizeMetadataHeader(FSeinReplayHeader& Header)
		{
			FSeinMatchSettings CanonicalSettings = Header.SettingsSnapshot;
			SeinCanonicalizeAndDigestMatchSettings(
				CanonicalSettings,
				Header.MatchSettingsDigest,
				nullptr);
			Header.BootstrapReceipt.ContractDigest = Header.MatchSettingsDigest;
			if (!Header.BootstrapReceipt.SimulationContentDigest.IsValid())
			{
				Header.BootstrapReceipt.SimulationContentDigest =
					FGuid(0x8a000000, 0x8b000000, 0x8c000000, 1);
			}
			Header.BootstrapReceipt.StateContractDigest =
				FGuid(0x87000000, 0x88000000, 0x89000000, 1);
			Header.BootstrapReceipt.PlanDigest =
				FGuid(0x81000000, 0x82000000, 0x83000000, 1);
			Header.BootstrapReceipt.InitialStateDigest =
				FGuid(0x84000000, 0x85000000, 0x86000000, 1);
		}

		struct FScopedMetadataFiles
		{
			TArray<FString> Paths;

			~FScopedMetadataFiles()
			{
				for (const FString& Path : Paths)
				{
					IFileManager::Get().Delete(*Path, false, true);
				}
			}
		};

		struct FScopedDynamicMetadataCatalog
		{
			USeinARTSCoreSettings* Settings =
				GetMutableDefault<USeinARTSCoreSettings>();
			TArray<FInstancedStruct> OriginalMatchExtensions = Settings
				? Settings->DefaultMatchExtensions
				: TArray<FInstancedStruct>();
			TArray<FInstancedStruct> OriginalSchemas = Settings
				? Settings->CommandDynamicPayloadSchemas
				: TArray<FInstancedStruct>();
			TArray<FName> OriginalNames = Settings
				? Settings->AdditionalWireNames
				: TArray<FName>();

			~FScopedDynamicMetadataCatalog()
			{
				if (Settings)
				{
					Settings->DefaultMatchExtensions =
						MoveTemp(OriginalMatchExtensions);
					Settings->CommandDynamicPayloadSchemas =
						MoveTemp(OriginalSchemas);
					Settings->AdditionalWireNames = MoveTemp(OriginalNames);
				}
			}

			template <typename T>
			void Remove()
			{
				if (!Settings) return;
				const UScriptStruct* Type = T::StaticStruct();
				auto RemoveType = [Type](TArray<FInstancedStruct>& Values)
				{
					Values.RemoveAll([Type](const FInstancedStruct& Existing)
					{
						return Existing.GetScriptStruct() == Type;
					});
				};
				RemoveType(Settings->DefaultMatchExtensions);
				RemoveType(Settings->CommandDynamicPayloadSchemas);
			}

			template <typename T>
			void Allow()
			{
				if (!Settings) return;
				const UScriptStruct* Type = T::StaticStruct();
				if (!Settings->CommandDynamicPayloadSchemas.ContainsByPredicate(
					[Type](const FInstancedStruct& Existing)
					{
						return Existing.GetScriptStruct() == Type;
					}))
				{
					Settings->CommandDynamicPayloadSchemas.Add(
						FInstancedStruct::Make(T()));
				}
			}

			void AllowName(FName Name)
			{
				if (Settings && !Name.IsNone())
				{
					Settings->AdditionalWireNames.AddUnique(Name);
				}
			}
		};

		FString MakeMetadataPath(const TCHAR* Suffix)
		{
			return FPaths::ProjectSavedDir()
				/ TEXT("Automation/ReplayHeaderMetadata")
				/ FString::Printf(
					TEXT("%s_%s.seinreplayheader"),
					*FGuid::NewGuid().ToString(EGuidFormats::Digits),
					Suffix);
		}

		constexpr int32 MetadataPrefixBytes = 68;
		constexpr int32 MetadataVersionOffset = 8;
		constexpr uint32 MetadataFormatVersion = 6;
		constexpr int32 MetadataDigestOffset = 20;
		constexpr int32 MetadataCatalogDigestOffset = 36;
		constexpr int32 MetadataSimulationContentDigestOffset = 52;

		uint32 ReadUInt32(const TArray<uint8>& Bytes, int32 Offset)
		{
			return (static_cast<uint32>(Bytes[Offset]) << 24)
				| (static_cast<uint32>(Bytes[Offset + 1]) << 16)
				| (static_cast<uint32>(Bytes[Offset + 2]) << 8)
				| static_cast<uint32>(Bytes[Offset + 3]);
		}

		void WriteUInt32(TArray<uint8>& Bytes, int32 Offset, uint32 Value)
		{
			Bytes[Offset] = static_cast<uint8>(Value >> 24);
			Bytes[Offset + 1] = static_cast<uint8>(Value >> 16);
			Bytes[Offset + 2] = static_cast<uint8>(Value >> 8);
			Bytes[Offset + 3] = static_cast<uint8>(Value);
		}

		void WriteGuid(TArray<uint8>& Bytes, int32 Offset, const FGuid& Value)
		{
			WriteUInt32(Bytes, Offset, Value.A);
			WriteUInt32(Bytes, Offset + 4, Value.B);
			WriteUInt32(Bytes, Offset + 8, Value.C);
			WriteUInt32(Bytes, Offset + 12, Value.D);
		}

		void RefreshBodyDigest(TArray<uint8>& FileBytes)
		{
			check(FileBytes.Num() >= MetadataPrefixBytes);
			const FBlake3Hash Hash = FBlake3::HashBuffer(
				FileBytes.GetData() + MetadataPrefixBytes,
				FileBytes.Num() - MetadataPrefixBytes);
			FMemory::Memcpy(
				FileBytes.GetData() + MetadataDigestOffset,
				Hash.GetBytes(), sizeof(FGuid));
		}

		int32 FindSlotsCountOffset(const TArray<uint8>& FileBytes)
		{
			// Two GUIDs, bootstrap receipt (version + five GUIDs), and
			// fingerprint, then framework/game/map strings and seed.
			int32 Offset =
				MetadataPrefixBytes
				+ 16 + 16 + 4 + 16 + 16 + 16 + 16 + 16 + 4;
			auto SkipString = [&FileBytes, &Offset]()
			{
				if (Offset + 4 > FileBytes.Num()) return false;
				const uint32 StringBytes = ReadUInt32(FileBytes, Offset);
				if (StringBytes > static_cast<uint32>(MAX_int32)
					|| Offset + 4 + static_cast<int32>(StringBytes)
						> FileBytes.Num())
				{
					return false;
				}
				Offset += 4 + static_cast<int32>(StringBytes);
				return true;
			};
			if (!SkipString() || !SkipString() || !SkipString())
			{
				return INDEX_NONE;
			}
			Offset += 8;
			return Offset + 4 <= FileBytes.Num() ? Offset : INDEX_NONE;
		}
	}

	TEST(ReplayHeaderMetadataRoundTripsWithoutClaimingExecutablePlayback,
		"SeinARTS.Unit.CoreEntity.Replay")
	{
		FScopedMetadataFiles Files;
		const FString Path = Files.Paths.Add_GetRef(
			MakeMetadataPath(TEXT("roundtrip")));
		const FString SecondPath = Files.Paths.Add_GetRef(
			MakeMetadataPath(TEXT("deterministic")));

		FSeinReplayHeader Expected;
		Expected.CommandProtocolDigest = FGuid(1, 2, 3, 4);
		Expected.ConfigFingerprint = static_cast<int32>(0x89abcdefu);
		Expected.FrameworkVersion = TEXT("framework-test");
		Expected.GameVersion = TEXT("game-test");
		Expected.MapIdentifier = TEXT("MetadataMap");
		Expected.RandomSeed = 314159;
		Expected.StartTick = 0;
		Expected.EndTick = 42;
		Expected.RecordedAt = FDateTime(2026, 7, 18, 12, 34, 56);
		FSeinMatchSlot& Slot =
			Expected.SettingsSnapshot.Slots.AddDefaulted_GetRef();
		Slot.SlotIndex = 1;
		Slot.State = ESeinSlotState::Human;
		FSeinPlayerRegistration& Player = Expected.Players.AddDefaulted_GetRef();
		Player.PlayerID = FSeinPlayerID(1);
		Player.FactionID = FSeinFactionID(2);
		Player.TeamID = 3;
		FinalizeMetadataHeader(Expected);
		ASSERT_THAT(IsTrue(USeinReplayBPFL::SeinSaveReplayHeaderMetadata(
			nullptr, Expected, Path)));
		ASSERT_THAT(IsTrue(USeinReplayBPFL::SeinSaveReplayHeaderMetadata(
			nullptr, Expected, SecondPath)));
		TArray<uint8> FirstBytes;
		TArray<uint8> SecondBytes;
		ASSERT_THAT(IsTrue(FFileHelper::LoadFileToArray(FirstBytes, *Path)));
		ASSERT_THAT(IsTrue(FFileHelper::LoadFileToArray(
			SecondBytes, *SecondPath)));
		ASSERT_THAT(IsTrue(FirstBytes == SecondBytes));
		ASSERT_THAT(AreEqual(
			MetadataFormatVersion,
			ReadUInt32(FirstBytes, MetadataVersionOffset)));

		FSeinReplayHeader Loaded;
		ASSERT_THAT(IsTrue(USeinReplayBPFL::SeinLoadReplayHeaderMetadata(
			nullptr, Path, Loaded)));
		ASSERT_THAT(IsTrue(FSeinReplayHeader::StaticStruct()
			->CompareScriptStruct(&Expected, &Loaded, PPF_None)));
	}

	TEST(ReplayHeaderMetadataRoundTripsPreviouslyUninternedUnicodeMapIdentity,
		"SeinARTS.Unit.CoreEntity.Replay")
	{
		FScopedMetadataFiles Files;
		const FString Path = Files.Paths.Add_GetRef(
			MakeMetadataPath(TEXT("unicode-map")));
		const FString MapIdentity = FString::Printf(
			TEXT("/Game/Maps/Replay_地图_%s"),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		ASSERT_THAT(IsTrue(FName(*MapIdentity, FNAME_Find).IsNone()));

		FSeinReplayHeader Expected;
		Expected.MapIdentifier = MapIdentity;
		FinalizeMetadataHeader(Expected);
		ASSERT_THAT(IsTrue(USeinReplayBPFL::SeinSaveReplayHeaderMetadata(
			nullptr, Expected, Path)));

		FSeinReplayHeader Loaded;
		ASSERT_THAT(IsTrue(USeinReplayBPFL::SeinLoadReplayHeaderMetadata(
			nullptr, Path, Loaded)));
		ASSERT_THAT(AreEqual(MapIdentity, Loaded.MapIdentifier));
		ASSERT_THAT(IsTrue(FName(*MapIdentity, FNAME_Find).IsNone()));
	}

	TEST(BuildReplayHeaderPopulatesCanonicalActivePlayerManifest,
		"SeinARTS.Unit.CoreEntity.Replay")
	{
		FActorTestSpawner Spawner;
		UWorld& TestWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			TestWorld.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FSeinMatchSlot Human;
		Human.SlotIndex = 1;
		Human.State = ESeinSlotState::Human;
		Human.FactionID = FSeinFactionID(4);
		Human.TeamID = 2;
		FSeinMatchSlot AI;
		AI.SlotIndex = 2;
		AI.State = ESeinSlotState::AI;
		AI.FactionID = FSeinFactionID(7);
		AI.TeamID = 3;
		FSeinMatchSlot Open;
		Open.SlotIndex = 3;
		Open.State = ESeinSlotState::Open;
		FSeinMatchSlot Closed;
		Closed.SlotIndex = 4;
		Closed.State = ESeinSlotState::Closed;
		FSeinMatchSettings Settings;
		Settings.Slots = {Closed, AI, Open, Human};
		FString BootstrapError;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(
				FSeinPlayerID(1), Human.FactionID, Human.TeamID);
			World->RegisterPlayer(
				FSeinPlayerID(2), AI.FactionID, AI.TeamID);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			Settings,
			0,
			TEXT("SeinARTS.ReplayHeaderMetadata"),
			&BootstrapError)));
		const FSeinMatchBootstrapReceipt BootstrapReceipt =
			World->GetMatchBootstrapReceipt();

		const FSeinReplayHeader Header =
			USeinReplayBPFL::SeinBuildReplayHeader(&TestWorld, NAME_None);
		ASSERT_THAT(IsTrue(Header.BootstrapReceipt == BootstrapReceipt));
		ASSERT_THAT(AreEqual(2, Header.Players.Num()));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(1), Header.Players[0].PlayerID.Value));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(4), Header.Players[0].FactionID.Value));
		ASSERT_THAT(AreEqual(static_cast<uint8>(2), Header.Players[0].TeamID));
		ASSERT_THAT(IsFalse(Header.Players[0].bIsAI));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(2), Header.Players[1].PlayerID.Value));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(7), Header.Players[1].FactionID.Value));
		ASSERT_THAT(AreEqual(static_cast<uint8>(3), Header.Players[1].TeamID));
		ASSERT_THAT(IsTrue(Header.Players[1].bIsAI));
		ASSERT_THAT(IsFalse(Header.MapIdentifier.IsEmpty()));
	}

	TEST(ReplayHeaderMetadataCanonicalizesLogicalArrayOrdering,
		"SeinARTS.Unit.CoreEntity.Replay")
	{
		FScopedMetadataFiles Files;
		FScopedDynamicMetadataCatalog Catalog;
		ASSERT_THAT(IsNotNull(Catalog.Settings));
		Catalog.Allow<FSeinCommandSchemaAlternateTestPayload>();
		Catalog.Allow<FSeinCommandSchemaWireOrderTestPayload>();
		const FString FirstPath = Files.Paths.Add_GetRef(
			MakeMetadataPath(TEXT("canonical-first")));
		const FString SecondPath = Files.Paths.Add_GetRef(
			MakeMetadataPath(TEXT("canonical-second")));

		FSeinReplayHeader First;
		First.MapIdentifier = TEXT("/Game/Maps/CanonicalMetadataMap");
		FSeinMatchSlot SlotOne;
		SlotOne.SlotIndex = 1;
		SlotOne.State = ESeinSlotState::Human;
		FSeinMatchSlot SlotTwo;
		SlotTwo.SlotIndex = 2;
		SlotTwo.State = ESeinSlotState::AI;
		First.SettingsSnapshot.Slots = {SlotTwo, SlotOne};

		FSeinCommandSchemaAlternateTestPayload Alternate;
		Alternate.Marker = 17;
		FSeinCommandSchemaWireOrderTestPayload WireOrder;
		WireOrder.ZetaDeclaredFirst = 31;
		WireOrder.AlphaDeclaredSecond = 47;
		const FInstancedStruct AlternateExtension =
			FInstancedStruct::Make(Alternate);
		const FInstancedStruct WireOrderExtension =
			FInstancedStruct::Make(WireOrder);
		First.SettingsSnapshot.Extensions = {
			WireOrderExtension, AlternateExtension};

		FSeinPlayerRegistration PlayerOne;
		PlayerOne.PlayerID = FSeinPlayerID(1);
		FSeinPlayerRegistration PlayerTwo;
		PlayerTwo.PlayerID = FSeinPlayerID(2);
		PlayerTwo.bIsAI = true;
		First.Players = {PlayerTwo, PlayerOne};
		FinalizeMetadataHeader(First);

		FSeinReplayHeader Second = First;
		Algo::Reverse(Second.SettingsSnapshot.Slots);
		Algo::Reverse(Second.SettingsSnapshot.Extensions);
		Algo::Reverse(Second.Players);
		ASSERT_THAT(IsTrue(USeinReplayBPFL::SeinSaveReplayHeaderMetadata(
			nullptr, First, FirstPath)));
		ASSERT_THAT(IsTrue(USeinReplayBPFL::SeinSaveReplayHeaderMetadata(
			nullptr, Second, SecondPath)));
		ASSERT_THAT(AreEqual(2, First.SettingsSnapshot.Slots[0].SlotIndex));

		TArray<uint8> FirstBytes;
		TArray<uint8> SecondBytes;
		ASSERT_THAT(IsTrue(FFileHelper::LoadFileToArray(
			FirstBytes, *FirstPath)));
		ASSERT_THAT(IsTrue(FFileHelper::LoadFileToArray(
			SecondBytes, *SecondPath)));
		ASSERT_THAT(IsTrue(FirstBytes == SecondBytes));

		FSeinReplayHeader Loaded;
		ASSERT_THAT(IsTrue(USeinReplayBPFL::SeinLoadReplayHeaderMetadata(
			nullptr, FirstPath, Loaded)));
		ASSERT_THAT(AreEqual(1, Loaded.SettingsSnapshot.Slots[0].SlotIndex));
		ASSERT_THAT(AreEqual(2, Loaded.SettingsSnapshot.Slots[1].SlotIndex));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(1), Loaded.Players[0].PlayerID.Value));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(2), Loaded.Players[1].PlayerID.Value));
		ASSERT_THAT(IsTrue(
			Loaded.SettingsSnapshot.Extensions[0].GetScriptStruct()
				->GetPathName()
			< Loaded.SettingsSnapshot.Extensions[1].GetScriptStruct()
				->GetPathName()));
	}

	TEST(ReplayHeaderMetadataReplacementPreservesThePreviousDocumentOnFailure,
		"SeinARTS.Unit.CoreEntity.Replay")
	{
		FScopedMetadataFiles Files;
		const FString Path = Files.Paths.Add_GetRef(
			MakeMetadataPath(TEXT("atomic-replace")));
		FSeinReplayHeader Original;
		Original.MapIdentifier = TEXT("/Game/Maps/OriginalMetadataMap");
		FinalizeMetadataHeader(Original);
		ASSERT_THAT(IsTrue(USeinReplayBPFL::SeinSaveReplayHeaderMetadata(
			nullptr, Original, Path)));

		FSeinReplayHeader Rejected = Original;
		Rejected.MapIdentifier.Reserve(1025);
		for (int32 CharacterIndex = 0; CharacterIndex < 1025; ++CharacterIndex)
		{
			Rejected.MapIdentifier.AppendChar(TEXT('M'));
		}
		TestRunner->AddExpectedError(
			TEXT("bounded serialization failed"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(USeinReplayBPFL::SeinSaveReplayHeaderMetadata(
			nullptr, Rejected, Path)));

		FSeinReplayHeader Loaded;
		ASSERT_THAT(IsTrue(USeinReplayBPFL::SeinLoadReplayHeaderMetadata(
			nullptr, Path, Loaded)));
		ASSERT_THAT(AreEqual(Original.MapIdentifier, Loaded.MapIdentifier));

		FSeinReplayHeader Replacement = Original;
		Replacement.MapIdentifier = TEXT("/Game/Maps/ReplacementMetadataMap");
		ASSERT_THAT(IsTrue(USeinReplayBPFL::SeinSaveReplayHeaderMetadata(
			nullptr, Replacement, Path)));
		ASSERT_THAT(IsTrue(USeinReplayBPFL::SeinLoadReplayHeaderMetadata(
			nullptr, Path, Loaded)));
		ASSERT_THAT(AreEqual(
			Replacement.MapIdentifier, Loaded.MapIdentifier));
	}

	TEST(ReplayHeaderMetadataRoundTripsRawNamesThroughFrozenCatalog,
		"SeinARTS.Unit.CoreEntity.Replay")
	{
		FScopedMetadataFiles Files;
		FScopedDynamicMetadataCatalog Catalog;
		ASSERT_THAT(IsNotNull(Catalog.Settings));
		Catalog.Allow<FSeinCommandSchemaIdentityWireTestPayload>();
		Catalog.AllowName(TEXT("ReplayMetadataIdentifier"));
		const FString Path = Files.Paths.Add_GetRef(
			MakeMetadataPath(TEXT("raw-name")));

		FSeinCommandSchemaIdentityWireTestPayload Extension;
		Extension.Name = TEXT("REPLAYMETADATAIDENTIFIER");
		FSeinReplayHeader Header;
		Header.MapIdentifier = TEXT("/Game/Maps/RawNameMetadataMap");
		Header.SettingsSnapshot.Extensions.Add(
			FInstancedStruct::Make(Extension));
		FinalizeMetadataHeader(Header);
		ASSERT_THAT(IsTrue(USeinReplayBPFL::SeinSaveReplayHeaderMetadata(
			nullptr, Header, Path)));

		FSeinReplayHeader Loaded;
		ASSERT_THAT(IsTrue(USeinReplayBPFL::SeinLoadReplayHeaderMetadata(
			nullptr, Path, Loaded)));
		ASSERT_THAT(AreEqual(1, Loaded.SettingsSnapshot.Extensions.Num()));
		ASSERT_THAT(AreEqual(
			FName(TEXT("ReplayMetadataIdentifier")),
			Loaded.SettingsSnapshot.Extensions[0]
				.Get<FSeinCommandSchemaIdentityWireTestPayload>().Name));
	}

	TEST(ReplayHeaderMetadataUsesWorldFrozenCatalogAfterSettingsMutation,
		"SeinARTS.Unit.CoreEntity.Replay")
	{
		FScopedMetadataFiles Files;
		FScopedDynamicMetadataCatalog Catalog;
		ASSERT_THAT(IsNotNull(Catalog.Settings));
		const FName FrozenName(TEXT("ReplayMetadataSessionFrozenName"));
		Catalog.Allow<FSeinCommandSchemaIdentityWireTestPayload>();
		Catalog.AllowName(FrozenName);

		// The world snapshots the configured type/name catalog during subsystem
		// initialization. Later editor/config mutations must not redefine that
		// already-running session's metadata format.
		FActorTestSpawner Spawner;
		UWorld& TestWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			TestWorld.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(World->GetCommandProtocolDigest().IsValid()));
		ASSERT_THAT(IsTrue(World->IsSimulationContentReady()));

		Catalog.Remove<FSeinCommandSchemaIdentityWireTestPayload>();
		Catalog.Allow<FSeinCommandSchemaAlternateTestPayload>();
		Catalog.Settings->AdditionalWireNames.Remove(FrozenName);
		Catalog.AllowName(TEXT("ReplayMetadataPostFreezeDriftName"));

		FSeinCommandSchemaIdentityWireTestPayload Extension;
		Extension.Name = TEXT("REPLAYMETADATASESSIONFROZENNAME");
		FSeinReplayHeader Header;
		Header.MapIdentifier = TEXT("/Game/Maps/FrozenMetadataMap");
		Header.SettingsSnapshot.Extensions.Add(
			FInstancedStruct::Make(Extension));
		FinalizeMetadataHeader(Header);
		Header.BootstrapReceipt.SimulationContentDigest =
			World->GetSimulationContentDigest();
		const FString Path = Files.Paths.Add_GetRef(
			MakeMetadataPath(TEXT("world-frozen-catalog")));
		const FString ContentMismatchPath = Files.Paths.Add_GetRef(
			MakeMetadataPath(TEXT("world-content-mismatch")));
		ASSERT_THAT(IsTrue(USeinReplayBPFL::SeinSaveReplayHeaderMetadata(
			&TestWorld, Header, Path)));

		FSeinReplayHeader Loaded;
		ASSERT_THAT(IsTrue(USeinReplayBPFL::SeinLoadReplayHeaderMetadata(
			&TestWorld, Path, Loaded)));
		ASSERT_THAT(AreEqual(1, Loaded.SettingsSnapshot.Extensions.Num()));
		ASSERT_THAT(AreEqual(
			FrozenName,
			Loaded.SettingsSnapshot.Extensions[0]
				.Get<FSeinCommandSchemaIdentityWireTestPayload>().Name));

		TArray<uint8> ContentMismatchBytes;
		ASSERT_THAT(IsTrue(FFileHelper::LoadFileToArray(
			ContentMismatchBytes, *Path)));
		FGuid OtherSimulationContentDigest =
			World->GetSimulationContentDigest();
		OtherSimulationContentDigest.D ^= 1u;
		if (!OtherSimulationContentDigest.IsValid())
		{
			OtherSimulationContentDigest.D = 2;
		}
		WriteGuid(
			ContentMismatchBytes,
			MetadataSimulationContentDigestOffset,
			OtherSimulationContentDigest);
		ASSERT_THAT(IsTrue(FFileHelper::SaveArrayToFile(
			ContentMismatchBytes, *ContentMismatchPath)));
		FSeinReplayHeader Unchanged;
		Unchanged.MapIdentifier = TEXT("ContentMismatchSentinel");
		TestRunner->AddExpectedError(
			TEXT("simulation content digest mismatch"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(USeinReplayBPFL::SeinLoadReplayHeaderMetadata(
			&TestWorld, ContentMismatchPath, Unchanged)));
		ASSERT_THAT(AreEqual(
			FString(TEXT("ContentMismatchSentinel")),
			Unchanged.MapIdentifier));

		// Null remains an explicit worldless-tooling fallback and therefore sees
		// the mutated project defaults rather than borrowing the live session.
		TestRunner->AddExpectedError(
			TEXT("catalog manifest mismatch"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		FSeinReplayHeader WorldlessLoaded;
		ASSERT_THAT(IsFalse(USeinReplayBPFL::SeinLoadReplayHeaderMetadata(
			nullptr, Path, WorldlessLoaded)));
	}

	TEST(ReplayHeaderMetadataRejectsCorruptAndOversizedFilesWithoutMutation,
		"SeinARTS.Unit.CoreEntity.Replay")
	{
		FScopedMetadataFiles Files;
		const FString ValidPath = Files.Paths.Add_GetRef(
			MakeMetadataPath(TEXT("valid")));
		const FString CorruptPath = Files.Paths.Add_GetRef(
			MakeMetadataPath(TEXT("corrupt")));
		const FString OversizedPath = Files.Paths.Add_GetRef(
			MakeMetadataPath(TEXT("oversized")));
		const FString CatalogMismatchPath = Files.Paths.Add_GetRef(
			MakeMetadataPath(TEXT("catalog-mismatch")));

		FSeinReplayHeader Header;
		Header.MapIdentifier = TEXT("OriginalMap");
		FinalizeMetadataHeader(Header);
		ASSERT_THAT(IsTrue(USeinReplayBPFL::SeinSaveReplayHeaderMetadata(
			nullptr, Header, ValidPath)));

		TArray<uint8> CorruptBytes;
		ASSERT_THAT(IsTrue(FFileHelper::LoadFileToArray(
			CorruptBytes, *ValidPath)));
		ASSERT_THAT(IsTrue(!CorruptBytes.IsEmpty()));
		CorruptBytes.Last() ^= 0xff;
		ASSERT_THAT(IsTrue(FFileHelper::SaveArrayToFile(
			CorruptBytes, *CorruptPath)));

		FSeinReplayHeader Unchanged;
		Unchanged.MapIdentifier = TEXT("MustRemainUnchanged");
		Unchanged.FrameworkVersion = TEXT("SentinelFramework");
		Unchanged.EndTick = 777;
		const FSeinReplayHeader ExpectedUnchanged = Unchanged;
		TestRunner->AddExpectedError(
			TEXT("checksum mismatch"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(USeinReplayBPFL::SeinLoadReplayHeaderMetadata(
			nullptr, CorruptPath, Unchanged)));
		ASSERT_THAT(IsTrue(FSeinReplayHeader::StaticStruct()
			->CompareScriptStruct(
				&ExpectedUnchanged, &Unchanged, PPF_None)));

		TArray<uint8> CatalogMismatchBytes;
		ASSERT_THAT(IsTrue(FFileHelper::LoadFileToArray(
			CatalogMismatchBytes, *ValidPath)));
		ASSERT_THAT(IsTrue(
			CatalogMismatchBytes.Num() >= MetadataPrefixBytes));
		CatalogMismatchBytes[MetadataCatalogDigestOffset] ^= 0x80;
		ASSERT_THAT(IsTrue(FFileHelper::SaveArrayToFile(
			CatalogMismatchBytes, *CatalogMismatchPath)));
		TestRunner->AddExpectedError(
			TEXT("catalog manifest mismatch"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(USeinReplayBPFL::SeinLoadReplayHeaderMetadata(
			nullptr, CatalogMismatchPath, Unchanged)));
		ASSERT_THAT(IsTrue(FSeinReplayHeader::StaticStruct()
			->CompareScriptStruct(
				&ExpectedUnchanged, &Unchanged, PPF_None)));

		TArray<uint8> OversizedBytes;
		OversizedBytes.SetNumZeroed(
			1 * 1024 * 1024 + MetadataPrefixBytes + 1);
		ASSERT_THAT(IsTrue(FFileHelper::SaveArrayToFile(
			OversizedBytes, *OversizedPath)));
		TestRunner->AddExpectedError(
			TEXT("rejected file size"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(USeinReplayBPFL::SeinLoadReplayHeaderMetadata(
			nullptr, OversizedPath, Unchanged)));
		ASSERT_THAT(IsTrue(FSeinReplayHeader::StaticStruct()
			->CompareScriptStruct(
				&ExpectedUnchanged, &Unchanged, PPF_None)));
	}

	TEST(ReplayHeaderMetadataRejectsHostileArrayCountBeforeMutation,
		"SeinARTS.Unit.CoreEntity.Replay")
	{
		FScopedMetadataFiles Files;
		const FString ValidPath = Files.Paths.Add_GetRef(
			MakeMetadataPath(TEXT("count-source")));
		const FString HostilePath = Files.Paths.Add_GetRef(
			MakeMetadataPath(TEXT("hostile-count")));

		FSeinReplayHeader Source;
		FinalizeMetadataHeader(Source);
		ASSERT_THAT(IsTrue(USeinReplayBPFL::SeinSaveReplayHeaderMetadata(
			nullptr, Source, ValidPath)));
		TArray<uint8> HostileBytes;
		ASSERT_THAT(IsTrue(FFileHelper::LoadFileToArray(
			HostileBytes, *ValidPath)));
		const int32 SlotsCountOffset = FindSlotsCountOffset(HostileBytes);
		ASSERT_THAT(IsTrue(SlotsCountOffset != INDEX_NONE));
		// One above the metadata codec's frozen aggregate-element cap.
		WriteUInt32(HostileBytes, SlotsCountOffset, 64 * 1024 + 1);
		RefreshBodyDigest(HostileBytes);
		ASSERT_THAT(IsTrue(FFileHelper::SaveArrayToFile(
			HostileBytes, *HostilePath)));

		FSeinReplayHeader Destination;
		Destination.MapIdentifier = TEXT("AtomicSentinelMap");
		Destination.FrameworkVersion = TEXT("AtomicSentinelFramework");
		Destination.EndTick = 123456;
		const FSeinReplayHeader ExpectedDestination = Destination;
		TestRunner->AddExpectedError(
			TEXT("aggregate element limit exceeded"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(USeinReplayBPFL::SeinLoadReplayHeaderMetadata(
			nullptr, HostilePath, Destination)));
		ASSERT_THAT(IsTrue(FSeinReplayHeader::StaticStruct()
			->CompareScriptStruct(
				&ExpectedDestination, &Destination, PPF_None)));
	}

	TEST(ReplayHeaderMetadataRejectsLegacyVersionBeforeDecode,
		"SeinARTS.Unit.CoreEntity.Replay")
	{
		FScopedMetadataFiles Files;
		const FString CurrentPath = Files.Paths.Add_GetRef(
			MakeMetadataPath(TEXT("version-six-source")));
		const FString VersionThreePath = Files.Paths.Add_GetRef(
			MakeMetadataPath(TEXT("version-three")));
		FSeinReplayHeader Header;
		Header.MapIdentifier = TEXT("VersionedMetadataMap");
		FinalizeMetadataHeader(Header);
		ASSERT_THAT(IsTrue(USeinReplayBPFL::SeinSaveReplayHeaderMetadata(
			nullptr, Header, CurrentPath)));

		TArray<uint8> Bytes;
		ASSERT_THAT(IsTrue(FFileHelper::LoadFileToArray(Bytes, *CurrentPath)));
		ASSERT_THAT(IsTrue(Bytes.Num() >= MetadataPrefixBytes));
		WriteUInt32(Bytes, MetadataVersionOffset, 3);
		ASSERT_THAT(IsTrue(FFileHelper::SaveArrayToFile(
			Bytes, *VersionThreePath)));

		FSeinReplayHeader Destination;
		Destination.MapIdentifier = TEXT("VersionRejectionSentinel");
		TestRunner->AddExpectedError(
			TEXT("unsupported replay-header metadata version 3"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(USeinReplayBPFL::SeinLoadReplayHeaderMetadata(
			nullptr, VersionThreePath, Destination)));
		ASSERT_THAT(AreEqual(
			FString(TEXT("VersionRejectionSentinel")),
			Destination.MapIdentifier));
	}
}

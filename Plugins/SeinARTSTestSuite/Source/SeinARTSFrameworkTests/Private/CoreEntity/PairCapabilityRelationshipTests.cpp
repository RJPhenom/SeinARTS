#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Containers/Ticker.h"
#include "Data/SeinReplayHeader.h"
#include "Data/SeinRelationshipTypes.h"
#include "Data/SeinUITypes.h"
#include "Data/SeinWorldSnapshot.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Input/SeinCommand.h"
#include "Input/SeinCommandSchemaRegistry.h"
#include "Lib/SeinUIBPFL.h"
#include "SeinReplayReader.h"
#include "SeinReplayWriter.h"
#include "Serialization/SeinSnapshotTransfer.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"

struct FSeinWorldSubsystemTestAccess
{
	static void CorruptPairCapabilityEffectiveCache(
		USeinWorldSubsystem& World)
	{
		World.PairCapabilityEffectiveRefCounts.Reset();
	}

	static void SetPairCapabilityEffectiveCount(
		USeinWorldSubsystem& World,
		FSeinPlayerID SourcePlayer,
		FSeinPlayerID TargetPlayer,
		FGameplayTag CapabilityTag,
		int32 Count)
	{
		FSeinPairCapabilityKey Key;
		Key.SourcePlayer = SourcePlayer;
		Key.TargetPlayer = TargetPlayer;
		Key.CapabilityTag = CapabilityTag;
		if (Count > 0)
		{
			World.PairCapabilityEffectiveRefCounts.Add(Key, Count);
		}
		else
		{
			World.PairCapabilityEffectiveRefCounts.Remove(Key);
		}
	}

	static bool ValidatePairCapabilityState(
		const USeinWorldSubsystem& World)
	{
		return World.ValidatePairCapabilityState();
	}

	static bool SealRoutineRoot(
		USeinWorldSubsystem& World,
		FGuid& OutRoot,
		FString& OutError)
	{
		return World.SealRoutineCanonicalStateRoot(
			World.GetCurrentTick(),
			/*bForceFullRebuild=*/true,
			OutRoot,
			OutError);
	}
};

namespace UE::SeinARTSTests
{
	namespace
	{
		const FSeinPlayerID P1(1);
		const FSeinPlayerID P2(2);
		const FSeinPlayerID P3(3);
		const FSeinPlayerID P4(4);

		bool MaterializeRelationshipFixture(USeinWorldSubsystem& World)
		{
			return SeinTestMatchBootstrap::Materialize(
				World,
				[&]()
				{
					World.RegisterPlayer(P1, FSeinFactionID(1), 1);
					World.RegisterPlayer(P2, FSeinFactionID(2), 1);
					World.RegisterPlayer(P3, FSeinFactionID(3), 0);
					World.RegisterPlayer(P4, FSeinFactionID(4), 2);
				},
				FSeinMatchSettings(),
				0x52454C31,
				TEXT("PairCapability.Relationships"));
		}

		FSeinCommand MakePairCapabilityCommand(
			FSeinPlayerID SourcePlayer,
			FSeinPlayerID TargetPlayer,
			FGameplayTag CapabilityTag,
			FGameplayTag SourceKindTag,
			bool bGrant,
			int64 SourceInstanceID = 1)
		{
			FSeinSetPairCapabilityCommandPayload Payload;
			Payload.SourcePlayer = SourcePlayer;
			Payload.TargetPlayer = TargetPlayer;
			Payload.CapabilityTag = CapabilityTag;
			Payload.SourceKindTag = SourceKindTag;
			Payload.SourceInstanceID = SourceInstanceID;
			Payload.bGrant = bGrant;

			FSeinCommand Command;
			Command.CommandType =
				SeinARTSTags::Command_Type_SetPairCapability;
			Command.SchemaVersion = 1;
			Command.Payload = FInstancedStruct::Make(Payload);
			return Command;
		}

		FSeinReplayHeader MakeReplayHeader(USeinWorldSubsystem& World)
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
			return Header;
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

		class FScopedParallelMode
		{
		public:
			FScopedParallelMode()
			{
				IConsoleManager& Console = IConsoleManager::Get();
				Parallel = Console.FindConsoleVariable(
					TEXT("Sein.Sim.Parallel"));
				MinBatch = Console.FindConsoleVariable(
					TEXT("Sein.Sim.ParallelMinBatch"));
				if (Parallel && MinBatch)
				{
					SavedParallel = Parallel->GetInt();
					SavedMinBatch = MinBatch->GetInt();
				}
			}

			~FScopedParallelMode()
			{
				if (IsValid())
				{
					Parallel->SetWithCurrentPriority(SavedParallel);
					MinBatch->SetWithCurrentPriority(SavedMinBatch);
				}
			}

			bool IsValid() const
			{
				return Parallel && MinBatch;
			}

			bool Set(bool bParallel)
			{
				if (!IsValid())
				{
					return false;
				}
				Parallel->SetWithCurrentPriority(bParallel ? 1 : 0);
				MinBatch->SetWithCurrentPriority(1);
				return Parallel->GetInt() == (bParallel ? 1 : 0)
					&& MinBatch->GetInt() == 1;
			}

		private:
			IConsoleVariable* Parallel = nullptr;
			IConsoleVariable* MinBatch = nullptr;
			int32 SavedParallel = 0;
			int32 SavedMinBatch = 0;
		};
	}

	TEST(PairCapabilitiesBootstrapTeamDefaultsAndTeamZeroNeutrality,
		"SeinARTS.Unit.CoreEntity.Relationships")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(MaterializeRelationshipFixture(*World)));

		ASSERT_THAT(IsTrue(World->ShouldPresentPlayerAsFriendly(P1, P2)));
		ASSERT_THAT(IsTrue(World->ShouldPresentPlayerAsFriendly(P2, P1)));
		ASSERT_THAT(IsFalse(World->ShouldPresentPlayerAsFriendly(P1, P3)));
		ASSERT_THAT(IsFalse(World->ShouldPresentPlayerAsFriendly(P3, P1)));
		ASSERT_THAT(IsFalse(World->ShouldPresentPlayerAsFriendly(P1, P4)));
		ASSERT_THAT(IsFalse(World->ShouldPresentPlayerAsFriendly(P4, P1)));

		const TArray<FSeinPairCapabilityGrantRecord> Grants =
			World->GetPairCapabilityGrantRecords();
		ASSERT_THAT(AreEqual(2, Grants.Num()));
		ASSERT_THAT(IsTrue(World->HasPairCapability(
			P1, P2, SeinARTSTags::Relationship_Capability_PresentAsFriendly)));
		ASSERT_THAT(IsTrue(World->HasPairCapability(
			P2, P1, SeinARTSTags::Relationship_Capability_PresentAsFriendly)));
	}

	TEST(PairCapabilitiesAreDirectionalAndRevokeExactSources,
		"SeinARTS.Unit.CoreEntity.Relationships")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(MaterializeRelationshipFixture(*World)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		const FGameplayTag ShareVision =
			SeinARTSTags::Relationship_Capability_ShareVision;
		const FGameplayTag SourceKind =
			SeinARTSTags::Relationship_Source_TeamBootstrap;
		constexpr int64 SourceA = 101;
		constexpr int64 SourceB = 202;

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(World->GrantPairCapability(
				P1, P2, ShareVision, SourceKind, SourceA)));
			ASSERT_THAT(IsTrue(World->GrantPairCapability(
				P1, P2, ShareVision, SourceKind, SourceA)));
			ASSERT_THAT(IsTrue(World->GrantPairCapability(
				P1, P2, ShareVision, SourceKind, SourceB)));
			ASSERT_THAT(IsTrue(World->GrantPairCapability(
				P2, P1, ShareVision, SourceKind, SourceB)));
			ASSERT_THAT(IsFalse(World->GrantPairCapability(
				P1, P2, SeinARTSTags::Relationship_Capability,
				SourceKind, 303)));
			ASSERT_THAT(IsFalse(World->GrantPairCapability(
				P1, P2, ShareVision,
				SeinARTSTags::Relationship_Source, 303)));
			ASSERT_THAT(IsFalse(World->GrantPairCapability(
				P1, P2, ShareVision, SourceKind, 0)));
		}

		ASSERT_THAT(IsTrue(World->HasPairCapability(P1, P2, ShareVision)));
		ASSERT_THAT(IsTrue(World->HasPairCapability(P2, P1, ShareVision)));
		ASSERT_THAT(IsFalse(World->HasPairCapability(P1, P3, ShareVision)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(World->RevokePairCapability(
				P1, P2, ShareVision, SourceKind, SourceA)));
		}
		ASSERT_THAT(IsTrue(World->HasPairCapability(P1, P2, ShareVision)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(World->RevokePairCapability(
				P1, P2, ShareVision, SourceKind, SourceA)));
		}
		ASSERT_THAT(IsTrue(World->HasPairCapability(P1, P2, ShareVision)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsFalse(World->RevokePairCapability(
				P1, P2, ShareVision, SourceKind, SourceA)));
			ASSERT_THAT(IsTrue(World->RevokePairCapability(
				P1, P2, ShareVision, SourceKind, SourceB)));
		}
		ASSERT_THAT(IsFalse(World->HasPairCapability(P1, P2, ShareVision)));
		ASSERT_THAT(IsTrue(World->HasPairCapability(P2, P1, ShareVision)));

		World->StopSimulation();
	}

	TEST(PairCapabilitiesSnapshotRestoreAndHashAsAuthoritativeState,
		"SeinARTS.Determinism.CoreEntity.Relationships")
	{
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		ASSERT_THAT(IsTrue(MaterializeRelationshipFixture(*Source)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*Source)));

		const FGameplayTag ShareVision =
			SeinARTSTags::Relationship_Capability_ShareVision;
		const FGameplayTag SourceTag =
			SeinARTSTags::Relationship_Source_TeamBootstrap;
		constexpr int64 PrimarySourceInstance = 401;
		constexpr int64 SecondarySourceInstance = 402;
		FString Error;
		FGuid BaselineRoot;
		ASSERT_THAT(IsTrue(
			Source->ComputeCanonicalStateRoot(BaselineRoot, Error)));
		const int32 BaselineHash = Source->ComputeStateHash();
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Source);
			ASSERT_THAT(IsTrue(Source->GrantPairCapability(
				P1, P3, ShareVision, SourceTag, PrimarySourceInstance)));
			ASSERT_THAT(IsTrue(Source->GrantPairCapability(
				P1, P3, ShareVision, SourceTag, SecondarySourceInstance)));
		}
		const int32 GrantedHash = Source->ComputeStateHash();
		ASSERT_THAT(IsTrue(BaselineHash != GrantedHash));

		FGuid SourceRoot;
		ASSERT_THAT(IsTrue(
			Source->ComputeCanonicalStateRoot(SourceRoot, Error)));
		ASSERT_THAT(IsTrue(BaselineRoot != SourceRoot));

		FSeinWorldSnapshot Snapshot;
		Source->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			Snapshot.SnapshotVersion));
		ASSERT_THAT(IsTrue(Snapshot.PairCapabilityGrants.Num() >= 3));

		FActorTestSpawner DestinationSpawner;
		USeinWorldSubsystem* Destination =
			DestinationSpawner.GetWorld()
				.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));
		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, Snapshot, &Error)));
		ASSERT_THAT(IsTrue(
			Destination->HasPairCapability(P1, P3, ShareVision)));
		ASSERT_THAT(AreEqual(
			GrantedHash, Destination->ComputeStateHash()));

		FGuid DestinationRoot;
		ASSERT_THAT(IsTrue(
			Destination->ComputeCanonicalStateRoot(
				DestinationRoot, Error)));
		ASSERT_THAT(IsTrue(SourceRoot == DestinationRoot));

		{
			auto SourceScope = FSeinSimContextTestAccess::Enter(*Source);
			ASSERT_THAT(IsTrue(Source->RevokePairCapability(
				P1, P3, ShareVision, SourceTag, PrimarySourceInstance)));
		}
		{
			auto DestinationScope =
				FSeinSimContextTestAccess::Enter(*Destination);
			ASSERT_THAT(IsTrue(Destination->RevokePairCapability(
				P1, P3, ShareVision, SourceTag, PrimarySourceInstance)));
		}
		ASSERT_THAT(IsTrue(
			Source->HasPairCapability(P1, P3, ShareVision)));
		ASSERT_THAT(IsTrue(
			Destination->HasPairCapability(P1, P3, ShareVision)));
		ASSERT_THAT(AreEqual(
			Source->ComputeStateHash(), Destination->ComputeStateHash()));

		{
			auto SourceScope = FSeinSimContextTestAccess::Enter(*Source);
			ASSERT_THAT(IsTrue(Source->RevokePairCapability(
				P1, P3, ShareVision, SourceTag, SecondarySourceInstance)));
		}
		{
			auto DestinationScope =
				FSeinSimContextTestAccess::Enter(*Destination);
			ASSERT_THAT(IsTrue(Destination->RevokePairCapability(
				P1, P3, ShareVision, SourceTag, SecondarySourceInstance)));
		}
		ASSERT_THAT(IsFalse(
			Source->HasPairCapability(P1, P3, ShareVision)));
		ASSERT_THAT(IsFalse(
			Destination->HasPairCapability(P1, P3, ShareVision)));
		ASSERT_THAT(AreEqual(
			Source->ComputeStateHash(), Destination->ComputeStateHash()));

		Source->StopSimulation();
		Destination->StopSimulation();
	}

	TEST(PairCapabilityCacheDriftFailsCanonicalRoots,
		"SeinARTS.Determinism.CoreEntity.Relationships")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld()
			.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(MaterializeRelationshipFixture(*World)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(World->GrantPairCapability(
				P3,
				P4,
				SeinARTSTags::Relationship_Capability_ShareVision,
				SeinARTSTags::Relationship_Source_TeamBootstrap,
				7)));
		}

		FGuid Root;
		FString Error;
		ASSERT_THAT(IsTrue(
			World->ComputeCanonicalStateRoot(Root, Error)));
		ASSERT_THAT(IsTrue(FSeinWorldSubsystemTestAccess::SealRoutineRoot(
			*World, Root, Error)));

		FSeinWorldSubsystemTestAccess::
			CorruptPairCapabilityEffectiveCache(*World);
		ASSERT_THAT(IsFalse(World->HasPairCapability(
			P3,
			P4,
			SeinARTSTags::Relationship_Capability_ShareVision)));
		ASSERT_THAT(IsFalse(
			World->ComputeCanonicalStateRoot(Root, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("pair-capability"))));
		ASSERT_THAT(IsFalse(FSeinWorldSubsystemTestAccess::SealRoutineRoot(
			*World, Root, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("pair-capability"))));
		World->StopSimulation();
	}

	TEST(PairCapabilityMutationsRepairDerivedCacheDrift,
		"SeinARTS.Unit.CoreEntity.Relationships")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld()
			.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(MaterializeRelationshipFixture(*World)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		const FGameplayTag ShareVision =
			SeinARTSTags::Relationship_Capability_ShareVision;
		const FGameplayTag SourceTag =
			SeinARTSTags::Relationship_Source_TeamBootstrap;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(World->GrantPairCapability(
				P3, P4, ShareVision, SourceTag, 7)));
			ASSERT_THAT(IsTrue(World->GrantPairCapability(
				P3, P4, ShareVision, SourceTag, 8)));
		}

		TestRunner->AddExpectedError(
			TEXT("repairing inconsistent derived pair-capability cache"),
			EAutomationExpectedErrorFlags::Contains, 3, false);

		FSeinWorldSubsystemTestAccess::SetPairCapabilityEffectiveCount(
			*World, P3, P4, ShareVision, 1);
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(World->RevokePairCapability(
				P3, P4, ShareVision, SourceTag, 7)));
		}
		ASSERT_THAT(IsTrue(World->HasPairCapability(
			P3, P4, ShareVision)));
		ASSERT_THAT(IsTrue(
			FSeinWorldSubsystemTestAccess::ValidatePairCapabilityState(
				*World)));

		FSeinWorldSubsystemTestAccess::SetPairCapabilityEffectiveCount(
			*World, P3, P4, ShareVision, 3);
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(World->RevokePairCapability(
				P3, P4, ShareVision, SourceTag, 8)));
		}
		ASSERT_THAT(IsFalse(World->HasPairCapability(
			P3, P4, ShareVision)));
		ASSERT_THAT(IsTrue(
			FSeinWorldSubsystemTestAccess::ValidatePairCapabilityState(
				*World)));

		FSeinWorldSubsystemTestAccess::SetPairCapabilityEffectiveCount(
			*World, P3, P4, ShareVision, 5);
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(World->GrantPairCapability(
				P3, P4, ShareVision, SourceTag, 9)));
		}
		ASSERT_THAT(IsTrue(World->HasPairCapability(
			P3, P4, ShareVision)));
		ASSERT_THAT(IsTrue(
			FSeinWorldSubsystemTestAccess::ValidatePairCapabilityState(
				*World)));

		FGuid Root;
		FString Error;
		ASSERT_THAT(IsTrue(World->ComputeCanonicalStateRoot(
			Root, Error)));
		World->StopSimulation();
	}

	TEST(PairCapabilityCommandsApplyAtDeterministicTickBoundaries,
		"SeinARTS.Unit.CoreEntity.Relationships")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(MaterializeRelationshipFixture(*World)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		const FGameplayTag ShareVision =
			SeinARTSTags::Relationship_Capability_ShareVision;
		const FGameplayTag SourceTag =
			SeinARTSTags::Relationship_Source_TeamBootstrap;
		const FSeinCommand Grant = MakePairCapabilityCommand(
			P3, P4, ShareVision, SourceTag, true);
		const FSeinCommand Revoke = MakePairCapabilityCommand(
			P3, P4, ShareVision, SourceTag, false);

		TArray<int32> ProcessingTicks;
		TArray<bool> CapabilityBeforeDispatch;
		World->OnCommandsProcessing.AddLambda(
			[&](int32 Tick, const TArray<FSeinCommand>& Commands)
			{
				for (const FSeinCommand& Command : Commands)
				{
					if (Command.CommandType
						== SeinARTSTags::Command_Type_SetPairCapability)
					{
						ProcessingTicks.Add(Tick);
						CapabilityBeforeDispatch.Add(
							World->HasPairCapability(
								P3, P4, ShareVision));
					}
				}
			});

		const int32 InitialTick = World->GetCurrentTick();
		World->SubmitLocalCommandDraft(
			Grant, /*bRequestMatchAdministration=*/true);
		ASSERT_THAT(AreEqual(1, World->GetPendingCommands().Num()));
		ASSERT_THAT(IsFalse(
			World->HasPairCapability(P3, P4, ShareVision)));

		FTSTicker::GetCoreTicker().Tick(
			World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsTrue(
			World->HasPairCapability(P3, P4, ShareVision)));

		World->SubmitLocalCommandDraft(
			Revoke, /*bRequestMatchAdministration=*/true);
		ASSERT_THAT(AreEqual(1, World->GetPendingCommands().Num()));
		ASSERT_THAT(IsTrue(
			World->HasPairCapability(P3, P4, ShareVision)));

		FTSTicker::GetCoreTicker().Tick(
			World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsFalse(
			World->HasPairCapability(P3, P4, ShareVision)));
		ASSERT_THAT(AreEqual(2, ProcessingTicks.Num()));
		ASSERT_THAT(AreEqual(InitialTick + 1, ProcessingTicks[0]));
		ASSERT_THAT(AreEqual(InitialTick + 2, ProcessingTicks[1]));
		ASSERT_THAT(AreEqual(2, CapabilityBeforeDispatch.Num()));
		ASSERT_THAT(IsFalse(CapabilityBeforeDispatch[0]));
		ASSERT_THAT(IsTrue(CapabilityBeforeDispatch[1]));

		World->StopSimulation();
	}

	TEST(PairCapabilityCommandRejectsOrdinaryPlayerAuthority,
		"SeinARTS.Unit.CoreEntity.Relationships")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(MaterializeRelationshipFixture(*World)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		const FGameplayTag ShareVision =
			SeinARTSTags::Relationship_Capability_ShareVision;
		FSeinCommand Grant = MakePairCapabilityCommand(
			P3,
			P4,
			ShareVision,
			SeinARTSTags::Relationship_Source_TeamBootstrap,
			true);
		Grant.PlayerID = P1;
		World->SubmitLocalCommandDraft(
			Grant, /*bRequestMatchAdministration=*/false);
		ASSERT_THAT(AreEqual(1, World->GetPendingCommands().Num()));
		FTSTicker::GetCoreTicker().Tick(
			World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(AreEqual(0, World->GetPendingCommands().Num()));
		ASSERT_THAT(IsFalse(
			World->HasPairCapability(P3, P4, ShareVision)));

		World->SubmitLocalCommandDraft(
			Grant, /*bRequestMatchAdministration=*/true);
		FTSTicker::GetCoreTicker().Tick(
			World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsTrue(
			World->HasPairCapability(P3, P4, ShareVision)));
		World->StopSimulation();
	}

	TEST(PairCapabilityInitialDigestAndUIProjectionFollowLedger,
		"SeinARTS.Determinism.CoreEntity.Relationships")
	{
		const auto MaterializeVariant = [](
			USeinWorldSubsystem& World,
			bool bGrantShareVision,
			FSeinEntityHandle& OutP1Entity,
			FSeinEntityHandle& OutP2Entity,
			FSeinEntityHandle& OutNeutralEntity)
		{
			return SeinTestMatchBootstrap::Materialize(
				World,
				[&]()
				{
					World.RegisterPlayer(P1, FSeinFactionID(1), 1);
					World.RegisterPlayer(P2, FSeinFactionID(2), 1);
					World.RegisterPlayer(P3, FSeinFactionID(3), 0);
					World.RegisterPlayer(P4, FSeinFactionID(4), 2);
					OutP1Entity = World.SpawnAbstractEntity(
						FFixedTransform(), P1);
					OutP2Entity = World.SpawnAbstractEntity(
						FFixedTransform(), P2);
					OutNeutralEntity = World.SpawnAbstractEntity(
						FFixedTransform(), FSeinPlayerID::Neutral());
					if (bGrantShareVision)
					{
						World.GrantPairCapability(
							P3,
							P4,
							SeinARTSTags::Relationship_Capability_ShareVision,
							SeinARTSTags::Relationship_Source_TeamBootstrap,
							31);
					}
				},
				FSeinMatchSettings(),
				0x52454C31,
				TEXT("PairCapability.DigestAndUI"));
		};

		FActorTestSpawner BaselineSpawner;
		FActorTestSpawner DuplicateSpawner;
		FActorTestSpawner VariantSpawner;
		USeinWorldSubsystem* Baseline = BaselineSpawner.GetWorld()
			.GetSubsystem<USeinWorldSubsystem>();
		USeinWorldSubsystem* Duplicate = DuplicateSpawner.GetWorld()
			.GetSubsystem<USeinWorldSubsystem>();
		USeinWorldSubsystem* Variant = VariantSpawner.GetWorld()
			.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Baseline));
		ASSERT_THAT(IsNotNull(Duplicate));
		ASSERT_THAT(IsNotNull(Variant));

		FSeinEntityHandle P1Entity;
		FSeinEntityHandle P2Entity;
		FSeinEntityHandle NeutralEntity;
		FSeinEntityHandle DuplicateP1;
		FSeinEntityHandle DuplicateP2;
		FSeinEntityHandle DuplicateNeutral;
		FSeinEntityHandle VariantP1;
		FSeinEntityHandle VariantP2;
		FSeinEntityHandle VariantNeutral;
		ASSERT_THAT(IsTrue(MaterializeVariant(
			*Baseline, false, P1Entity, P2Entity, NeutralEntity)));
		ASSERT_THAT(IsTrue(MaterializeVariant(
			*Duplicate, false, DuplicateP1, DuplicateP2,
			DuplicateNeutral)));
		ASSERT_THAT(IsTrue(MaterializeVariant(
			*Variant, true, VariantP1, VariantP2, VariantNeutral)));
		ASSERT_THAT(IsTrue(
			Baseline->GetMatchBootstrapReceipt().InitialStateDigest
				== Duplicate->GetMatchBootstrapReceipt().InitialStateDigest));
		ASSERT_THAT(IsTrue(
			Baseline->GetMatchBootstrapReceipt().InitialStateDigest
				!= Variant->GetMatchBootstrapReceipt().InitialStateDigest));

		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*Baseline)));
		ASSERT_THAT(IsTrue(
			USeinUIBPFL::SeinGetEntityRelation(
				&BaselineSpawner.GetWorld(), P1Entity, P2)
				== ESeinRelation::Friendly));
		ASSERT_THAT(IsTrue(
			USeinUIBPFL::SeinGetEntityRelation(
				&BaselineSpawner.GetWorld(), P1Entity, P1)
				== ESeinRelation::Friendly));
		ASSERT_THAT(IsTrue(
			USeinUIBPFL::SeinGetEntityRelation(
				&BaselineSpawner.GetWorld(), NeutralEntity, P1)
				== ESeinRelation::Neutral));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Baseline);
			ASSERT_THAT(IsTrue(Baseline->RevokePairCapability(
				P1,
				P2,
				SeinARTSTags::Relationship_Capability_PresentAsFriendly,
				SeinARTSTags::Relationship_Source_TeamBootstrap,
				1)));
		}
		ASSERT_THAT(IsTrue(
			USeinUIBPFL::SeinGetEntityRelation(
				&BaselineSpawner.GetWorld(), P1Entity, P2)
				== ESeinRelation::Enemy));
		ASSERT_THAT(IsTrue(
			USeinUIBPFL::SeinGetEntityRelation(
				&BaselineSpawner.GetWorld(), P2Entity, P1)
				== ESeinRelation::Friendly));

		Baseline->StopSimulation();
	}

	TEST(PendingPairCapabilityCommandContinuesAcrossSnapshotRestore,
		"SeinARTS.Determinism.CoreEntity.Relationships")
	{
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		ASSERT_THAT(IsTrue(MaterializeRelationshipFixture(*Source)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*Source)));

		const FGameplayTag ShareVision =
			SeinARTSTags::Relationship_Capability_ShareVision;
		const FSeinCommand Grant = MakePairCapabilityCommand(
			P3,
			P4,
			ShareVision,
			SeinARTSTags::Relationship_Source_TeamBootstrap,
			true);
		Source->SubmitLocalCommandDraft(
			Grant, /*bRequestMatchAdministration=*/true);
		ASSERT_THAT(AreEqual(1, Source->GetPendingCommands().Num()));
		ASSERT_THAT(IsFalse(
			Source->HasPairCapability(P3, P4, ShareVision)));

		FSeinWorldSnapshot Checkpoint;
		FSeinWorldSnapshotReferenceGuard CheckpointGuard(Checkpoint);
		Source->CaptureSnapshot(Checkpoint);
		ASSERT_THAT(AreEqual(1, Checkpoint.PendingCommands.Num()));

		FActorTestSpawner DestinationSpawner;
		USeinWorldSubsystem* Destination =
			DestinationSpawner.GetWorld()
				.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));
		FString Error;
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
			*Destination, Checkpoint, &Error)));
		ASSERT_THAT(AreEqual(
			1, Destination->GetPendingCommands().Num()));
		ASSERT_THAT(IsFalse(
			Destination->HasPairCapability(P3, P4, ShareVision)));

		FTSTicker::GetCoreTicker().Tick(
			Source->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsTrue(
			Source->HasPairCapability(P3, P4, ShareVision)));
		ASSERT_THAT(IsTrue(
			Destination->HasPairCapability(P3, P4, ShareVision)));
		ASSERT_THAT(AreEqual(
			Source->GetCurrentTick(), Destination->GetCurrentTick()));
		ASSERT_THAT(AreEqual(
			Source->ComputeStateHash(), Destination->ComputeStateHash()));

		FGuid SourceRoot;
		FGuid DestinationRoot;
		ASSERT_THAT(IsTrue(
			Source->ComputeCanonicalStateRoot(SourceRoot, Error)));
		ASSERT_THAT(IsTrue(Destination->ComputeCanonicalStateRoot(
			DestinationRoot, Error)));
		ASSERT_THAT(IsTrue(SourceRoot == DestinationRoot));

		Source->StopSimulation();
		Destination->StopSimulation();
	}

	TEST(PairCapabilitySnapshotEnvelopePreservesAndContinuesLedger,
		"SeinARTS.Determinism.CoreEntity.Relationships")
	{
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source = SourceSpawner.GetWorld()
			.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		ASSERT_THAT(IsTrue(MaterializeRelationshipFixture(*Source)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*Source)));

		const FGameplayTag ShareVision =
			SeinARTSTags::Relationship_Capability_ShareVision;
		const FGameplayTag SourceTag =
			SeinARTSTags::Relationship_Source_TeamBootstrap;
		FSeinCommand Grant = MakePairCapabilityCommand(
			P3, P4, ShareVision, SourceTag, true);
		Source->SubmitLocalCommandDraft(
			Grant, /*bRequestMatchAdministration=*/true);
		FTSTicker::GetCoreTicker().Tick(
			Source->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsTrue(
			Source->HasPairCapability(P3, P4, ShareVision)));

		FSeinWorldSnapshot Checkpoint;
		Source->CaptureSnapshot(Checkpoint);
		TArray<uint8> EnvelopeBytes;
		FSeinSnapshotEnvelopeMetadata Metadata;
		FString Error;
		ASSERT_THAT(IsTrue(SeinSnapshotTransfer::EncodeCheckpointEnvelope(
			Checkpoint, EnvelopeBytes, Metadata, Error)));
		FSeinWorldSnapshot Transferred;
		FSeinWorldSnapshotReferenceGuard TransferredGuard(Transferred);
		FSeinSnapshotEnvelopeMetadata TransferredMetadata;
		ASSERT_THAT(IsTrue(SeinSnapshotTransfer::DecodeCheckpointEnvelope(
			EnvelopeBytes, Transferred, TransferredMetadata, Error)));

		FActorTestSpawner TargetSpawner;
		USeinWorldSubsystem* Target = TargetSpawner.GetWorld()
			.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Target));
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
			*Target, Transferred, &Error)));
		ASSERT_THAT(IsTrue(
			Target->HasPairCapability(P3, P4, ShareVision)));
		ASSERT_THAT(AreEqual(
			Source->ComputeStateHash(), Target->ComputeStateHash()));

		Source->SubmitLocalCommandDraft(
			MakePairCapabilityCommand(
				P3, P4, ShareVision, SourceTag, false),
			/*bRequestMatchAdministration=*/true);
		Target->SubmitLocalCommandDraft(
			MakePairCapabilityCommand(
				P3, P4, ShareVision, SourceTag, false),
			/*bRequestMatchAdministration=*/true);
		FTSTicker::GetCoreTicker().Tick(
			Source->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsFalse(
			Source->HasPairCapability(P3, P4, ShareVision)));
		ASSERT_THAT(IsFalse(
			Target->HasPairCapability(P3, P4, ShareVision)));
		ASSERT_THAT(AreEqual(
			Source->ComputeStateHash(), Target->ComputeStateHash()));

		FGuid SourceRoot;
		FGuid TargetRoot;
		ASSERT_THAT(IsTrue(Source->ComputeCanonicalStateRoot(
			SourceRoot, Error)));
		ASSERT_THAT(IsTrue(Target->ComputeCanonicalStateRoot(
			TargetRoot, Error)));
		ASSERT_THAT(IsTrue(SourceRoot == TargetRoot));
		Source->StopSimulation();
		Target->StopSimulation();
	}

	TEST(PairCapabilityReplayFileReplaysMatchControlMutation,
		"SeinARTS.Determinism.CoreEntity.Relationships")
	{
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source = SourceSpawner.GetWorld()
			.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		ASSERT_THAT(IsTrue(MaterializeRelationshipFixture(*Source)));
		FString Error;
		ASSERT_THAT(IsTrue(
			SeinTestMatchBootstrap::Authorize(*Source, &Error)));

		USeinReplayWriter* Writer = NewObject<USeinReplayWriter>(Source);
		ASSERT_THAT(IsNotNull(Writer));
		Writer->StartRecording(MakeReplayHeader(*Source));
		ASSERT_THAT(IsTrue(Writer->IsRecording()));
		ASSERT_THAT(IsTrue(
			SeinTestMatchBootstrap::Start(*Source, &Error)));
		ASSERT_THAT(IsTrue(
			Writer->CaptureCheckpoint(/*bRequired=*/true)));

		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		const int32 TicksPerTurn = Settings->TurnRate > 0
			? FMath::Max(
				1, Settings->SimulationTickRate / Settings->TurnRate)
			: 1;
		const int32 FirstTurn = Settings->InputDelayTurns > 0
			? Settings->InputDelayTurns
			: 3;
		const int32 CommandTick = FirstTurn * TicksPerTurn;
		FSeinCommand Grant = MakePairCapabilityCommand(
			P3,
			P4,
			SeinARTSTags::Relationship_Capability_ShareVision,
			SeinARTSTags::Relationship_Source_TeamBootstrap,
			true);
		Grant.Tick = CommandTick;
		Grant.IssuerKind = ESeinCommandIssuerKind::MatchAdministrator;
		Writer->RecordTurn(FirstTurn, {Grant});

		for (int32 ExpectedTick = 1;
			ExpectedTick <= CommandTick;
			++ExpectedTick)
		{
			if (ExpectedTick == CommandTick)
			{
				Source->SubmitLocalCommandDraft(
					Grant, /*bRequestMatchAdministration=*/true);
			}
			FTSTicker::GetCoreTicker().Tick(
				Source->GetFixedDeltaTimeSeconds());
			ASSERT_THAT(AreEqual(
				ExpectedTick, Source->GetCurrentTick()));
			Writer->ObserveCompletedTick(ExpectedTick);
		}
		ASSERT_THAT(IsTrue(Source->HasPairCapability(
			P3,
			P4,
			SeinARTSTags::Relationship_Capability_ShareVision)));
		FGuid SourceRoot;
		ASSERT_THAT(IsTrue(Source->ComputeCanonicalStateRoot(
			SourceRoot, Error)));
		const int32 SourceHash = Source->ComputeStateHash();
		Source->StopSimulation();

		FScopedReplayFile ReplayFile{Writer->FinishRecording()};
		ASSERT_THAT(IsFalse(ReplayFile.Path.IsEmpty()));
		ASSERT_THAT(IsTrue(
			IFileManager::Get().FileExists(*ReplayFile.Path)));
		FActorTestSpawner TargetSpawner;
		USeinWorldSubsystem* Target = TargetSpawner.GetWorld()
			.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Target));
		USeinReplayReader* Reader = NewObject<USeinReplayReader>(
			&TargetSpawner.GetWorld());
		ASSERT_THAT(IsNotNull(Reader));
		ASSERT_THAT(IsTrue(Reader->LoadFromFile(ReplayFile.Path)));
		ASSERT_THAT(IsTrue(Reader->Play()));
		for (int32 Pump = 0;
			Pump < CommandTick * 4 && Reader->IsPlaying();
			++Pump)
		{
			FTSTicker::GetCoreTicker().Tick(
				Target->GetFixedDeltaTimeSeconds());
		}
		ASSERT_THAT(AreEqual(CommandTick, Target->GetCurrentTick()));
		ASSERT_THAT(IsFalse(Reader->IsPlaying()));
		ASSERT_THAT(IsTrue(Target->StartSimulation()));
		ASSERT_THAT(IsTrue(Target->HasPairCapability(
			P3,
			P4,
			SeinARTSTags::Relationship_Capability_ShareVision)));
		FGuid TargetRoot;
		ASSERT_THAT(IsTrue(Target->ComputeCanonicalStateRoot(
			TargetRoot, Error)));
		ASSERT_THAT(IsTrue(SourceRoot == TargetRoot));
		ASSERT_THAT(AreEqual(SourceHash, Target->ComputeStateHash()));
		Target->StopSimulation();
	}

	TEST(PairCapabilitiesPersistAcrossParticipantRoles,
		"SeinARTS.Determinism.CoreEntity.Relationships")
	{
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		ASSERT_THAT(IsTrue(MaterializeRelationshipFixture(*Source)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*Source)));

		const FGameplayTag ShareVision =
			SeinARTSTags::Relationship_Capability_ShareVision;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Source);
			ASSERT_THAT(IsTrue(Source->GrantPairCapability(
				P3,
				P4,
				ShareVision,
				SeinARTSTags::Relationship_Source_TeamBootstrap,
				71)));
		}

		FSeinWorldSnapshot Snapshot;
		Source->CaptureSnapshot(Snapshot);
		Snapshot.PlayerStates.FindChecked(P1).bIsSpectator = true;
		Snapshot.PlayerStates.FindChecked(P3).bIsAI = true;
		Snapshot.PlayerStates.FindChecked(P4).bEliminated = true;

		FActorTestSpawner DestinationSpawner;
		USeinWorldSubsystem* Destination =
			DestinationSpawner.GetWorld()
				.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));
		FString Error;
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
			*Destination, Snapshot, &Error)));
		ASSERT_THAT(IsTrue(
			Destination->GetPlayerState(P1)->bIsSpectator));
		ASSERT_THAT(IsTrue(Destination->GetPlayerState(P3)->bIsAI));
		ASSERT_THAT(IsTrue(
			Destination->GetPlayerState(P4)->bEliminated));
		ASSERT_THAT(IsTrue(
			Destination->HasPairCapability(P3, P4, ShareVision)));

		Source->StopSimulation();
		Destination->StopSimulation();
	}

	TEST(PairCapabilitySnapshotRejectsEffectiveRefCountOverflow,
		"SeinARTS.Determinism.CoreEntity.Relationships")
	{
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		ASSERT_THAT(IsTrue(MaterializeRelationshipFixture(*Source)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*Source)));

		FSeinWorldSnapshot Snapshot;
		Source->CaptureSnapshot(Snapshot);
		FSeinPairCapabilityGrantRecord& First =
			Snapshot.PairCapabilityGrants.AddDefaulted_GetRef();
		First.SourcePlayer = P3;
		First.TargetPlayer = P4;
		First.CapabilityTag =
			SeinARTSTags::Relationship_Capability_ShareVision;
		First.SourceKindTag =
			SeinARTSTags::Relationship_Source_TeamBootstrap;
		First.SourceInstanceID = 101;
		First.RefCount = MAX_int32;
		FSeinPairCapabilityGrantRecord& Second =
			Snapshot.PairCapabilityGrants.AddDefaulted_GetRef();
		Second.SourcePlayer = P3;
		Second.TargetPlayer = P4;
		Second.CapabilityTag =
			SeinARTSTags::Relationship_Capability_ShareVision;
		Second.SourceKindTag =
			SeinARTSTags::Relationship_Source_TeamBootstrap;
		Second.SourceInstanceID = 102;
		Second.RefCount = 1;

		FActorTestSpawner DestinationSpawner;
		USeinWorldSubsystem* Destination =
			DestinationSpawner.GetWorld()
				.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));
		const int32 HashBefore = Destination->ComputeStateHash();
		TestRunner->AddExpectedError(
			TEXT("authoritative sim state failed structural preflight"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		FString Error;
		ASSERT_THAT(IsFalse(SeinTestSnapshotRestore::RestoreTrusted(
			*Destination, Snapshot, &Error)));
		ASSERT_THAT(AreEqual(
			HashBefore, Destination->ComputeStateHash()));

		Source->StopSimulation();
	}

	TEST(PairCapabilitySnapshotRejectsOutOfDomainTags,
		"SeinARTS.Determinism.CoreEntity.Relationships")
	{
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		ASSERT_THAT(IsTrue(MaterializeRelationshipFixture(*Source)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*Source)));

		FSeinWorldSnapshot Snapshot;
		Source->CaptureSnapshot(Snapshot);
		ASSERT_THAT(IsTrue(!Snapshot.PairCapabilityGrants.IsEmpty()));
		Snapshot.PairCapabilityGrants[0].CapabilityTag =
			SeinARTSTags::Relationship_Capability;

		FActorTestSpawner DestinationSpawner;
		USeinWorldSubsystem* Destination =
			DestinationSpawner.GetWorld()
				.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));
		TestRunner->AddExpectedError(
			TEXT("authoritative sim state failed structural preflight"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		FString Error;
		ASSERT_THAT(IsFalse(SeinTestSnapshotRestore::RestoreTrusted(
			*Destination, Snapshot, &Error)));

		Source->StopSimulation();
	}

	TEST(PairCapabilityCommandHashesAgreeSerialAndParallel,
		"SeinARTS.Determinism.CoreEntity.Relationships")
	{
		FScopedParallelMode ParallelMode;
		ASSERT_THAT(IsTrue(ParallelMode.IsValid()));

		const FGameplayTag ShareVision =
			SeinARTSTags::Relationship_Capability_ShareVision;
		const FSeinCommand Grant = MakePairCapabilityCommand(
			P3,
			P4,
			ShareVision,
			SeinARTSTags::Relationship_Source_TeamBootstrap,
			true);
		const auto RunScenario = [&ParallelMode, &Grant, ShareVision](
			bool bParallel,
			int32& OutHash,
			FGuid& OutRoot)
		{
			if (!ParallelMode.Set(bParallel))
			{
				return false;
			}
			FActorTestSpawner Spawner;
			USeinWorldSubsystem* World =
				Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			if (!World || !MaterializeRelationshipFixture(*World)
				|| !SeinTestMatchBootstrap::Start(*World))
			{
				return false;
			}
			World->SubmitLocalCommandDraft(
				Grant, /*bRequestMatchAdministration=*/true);
			FTSTicker::GetCoreTicker().Tick(
				World->GetFixedDeltaTimeSeconds());
			FString Error;
			const bool bSucceeded =
				World->HasPairCapability(P3, P4, ShareVision)
				&& World->ComputeCanonicalStateRoot(OutRoot, Error);
			OutHash = World->ComputeStateHash();
			World->StopSimulation();
			return bSucceeded;
		};

		int32 SerialHash = 0;
		int32 ParallelHash = 0;
		FGuid SerialRoot;
		FGuid ParallelRoot;
		ASSERT_THAT(IsTrue(RunScenario(
			false, SerialHash, SerialRoot)));
		ASSERT_THAT(IsTrue(RunScenario(
			true, ParallelHash, ParallelRoot)));
		ASSERT_THAT(AreEqual(SerialHash, ParallelHash));
		ASSERT_THAT(IsTrue(SerialRoot == ParallelRoot));
	}

	TEST(PairCapabilityCommandSchemaIsFrozenAndPayloadAdmissionRejectsInvalidValues,
		"SeinARTS.Determinism.CoreEntity.Relationships")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(MaterializeRelationshipFixture(*World)));

		const FGameplayTag ShareVision =
			SeinARTSTags::Relationship_Capability_ShareVision;
		const FGameplayTag SourceTag =
			SeinARTSTags::Relationship_Source_TeamBootstrap;
		FSeinSetPairCapabilityCommandPayload Payload;
		Payload.SourcePlayer = P1;
		Payload.TargetPlayer = P4;
		Payload.CapabilityTag = ShareVision;
		Payload.SourceKindTag = SourceTag;
		Payload.SourceInstanceID = 1;
		Payload.bGrant = true;

		FSeinCommandSchemaDescriptor Schema;
		ASSERT_THAT(IsTrue(World->FindCommandSchema(
			SeinARTSTags::Command_Type_SetPairCapability, 1, Schema)));
		ASSERT_THAT(IsTrue(
			Schema.PayloadStruct
			== FSeinSetPairCapabilityCommandPayload::StaticStruct()));
		ASSERT_THAT(IsTrue(
			Schema.AuthorityScope
			== ESeinCommandAuthorityScope::MatchControl));

		FSeinCommand Command;
		Command.CommandType =
			SeinARTSTags::Command_Type_SetPairCapability;
		Command.SchemaVersion = 1;
		Command.Payload = FInstancedStruct::Make(Payload);
		FSeinCommandSchemaDescriptor ValidatedSchema;
		ASSERT_THAT(IsTrue(
			World->ValidateCommandStructure(
				Command, &ValidatedSchema)
			== ESeinCommandStructureResult::Valid));
		ASSERT_THAT(IsTrue(
			ValidatedSchema.PayloadStruct
			== FSeinSetPairCapabilityCommandPayload::StaticStruct()));


		// Structure validation owns the frozen envelope/type contract. Semantic
		// payload values are rejected by deterministic command execution.
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		const auto SubmitInvalidPayload = [&]()
		{
			Command.Payload = FInstancedStruct::Make(Payload);
			ASSERT_THAT(IsTrue(World->ValidateCommandStructure(Command)
				== ESeinCommandStructureResult::Valid));
			World->SubmitLocalCommandDraft(
				Command, /*bRequestMatchAdministration=*/true);
			ASSERT_THAT(AreEqual(1, World->GetPendingCommands().Num()));
			FTSTicker::GetCoreTicker().Tick(
				World->GetFixedDeltaTimeSeconds());
			ASSERT_THAT(AreEqual(0, World->GetPendingCommands().Num()));
			ASSERT_THAT(IsFalse(World->HasPairCapability(
				P1, P4, ShareVision)));
		};

		Payload.SourceInstanceID = 0;
		SubmitInvalidPayload();
		Payload.SourceInstanceID = 1;
		Payload.CapabilityTag = SeinARTSTags::Relationship_Capability;
		SubmitInvalidPayload();
		Payload.CapabilityTag = ShareVision;
		Payload.SourceKindTag = SeinARTSTags::Relationship_Source;
		SubmitInvalidPayload();
		World->StopSimulation();
	}
}

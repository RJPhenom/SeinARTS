#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Components/SeinExtentsComponent.h"
#include "Containers/Ticker.h"
#include "Core/SeinTickPhase.h"
#include "Data/SeinFaction.h"
#include "Data/SeinWorldSnapshot.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "TestTypes/SeinCommandSchemaTestTypes.h"
#include "TestTypes/SeinDeferredDestroyTestTypes.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		constexpr uint8 SnapshotFactionID = 251;

		class FQueueDestroyOncePostTickSystem final : public ISeinSystem
		{
		public:
			virtual void Tick(
				FFixedPoint,
				USeinWorldSubsystem& InWorld) override
			{
				InWorld.GetEntityPool().ForEachEntity(
					[&InWorld](
						FSeinEntityHandle Handle,
						const FSeinEntity&)
				{
					FSeinDeferredDestroyTestComponent* Marker =
						InWorld.GetComponentMutable<
							FSeinDeferredDestroyTestComponent>(
								Handle);
					if (!Marker || !Marker->bArmed)
					{
						return;
					}
					Marker->bArmed = false;
					InWorld.DestroyEntity(Handle);
				});
			}

			virtual FSeinSystemDescriptor DescribeSystem() const override
			{
				return FSeinSystemDescriptor::Stateless(
					FName(TEXT("seinarts.tests.snapshot.deferred_destroy")),
					1u,
					ESeinTickPhase::PostTick,
					0);
			}
		};

		bool CreateRunningCheckpointSource(
			USeinWorldSubsystem& World,
			FSeinEntityHandle& OutEntity,
			FString& OutError,
			TWeakObjectPtr<USeinFaction>* OutFaction = nullptr)
		{
			FSeinMatchSettings Settings;
			bool bAuthoringSucceeded = true;
			const auto AuthorState = [&]()
			{
				World.RegisterFactionsFromSettings();
				UPackage* TransientPackage = GetTransientPackage();
				USeinFaction* SnapshotFaction = NewObject<USeinFaction>(
					TransientPackage,
					MakeUniqueObjectName(
						TransientPackage,
						USeinFaction::StaticClass(),
						TEXT("SeinSnapshotCheckpointFaction")));
				if (!SnapshotFaction)
				{
					OutError = TEXT("Could not create snapshot test faction.");
					bAuthoringSucceeded = false;
					return;
				}
				SnapshotFaction->FactionID = FSeinFactionID(SnapshotFactionID);
				World.RegisterFaction(SnapshotFaction);
				if (OutFaction)
				{
					*OutFaction = SnapshotFaction;
				}

				OutEntity = World.SpawnAbstractEntity(
					FFixedTransform(), FSeinPlayerID::Neutral());
				if (!OutEntity.IsValid())
				{
					OutError = TEXT("Could not create snapshot test entity.");
					bAuthoringSucceeded = false;
					return;
				}
				World.AddComponent(OutEntity, FSeinExtentsComponent());
				World.AddComponent(
					OutEntity,
					FSeinDeferredDestroyTestComponent());

				FSeinCommandSchemaAlternateTestPayload Contribution;
				Contribution.Marker = 73;
				bAuthoringSucceeded =
					World.RegisterCanonicalBootstrapEvidenceValue(
						TEXT("SeinFrameworkTests.SnapshotCheckpoint"),
						1,
						FInstancedStruct::Make(Contribution),
						OutError);
			};
			if (!SeinTestMatchBootstrap::Materialize(
				World,
				AuthorState,
				Settings,
				0x10203040,
				TEXT("SnapshotCheckpoint"),
				&OutError)
				|| !bAuthoringSucceeded
				|| !SeinTestMatchBootstrap::Start(World, &OutError))
			{
				return false;
			}

			FTSTicker::GetCoreTicker().Tick(
				World.GetFixedDeltaTimeSeconds());
			if (World.GetCurrentTick() <= 0)
			{
				OutError = TEXT("Snapshot test source did not advance.");
				return false;
			}
			World.StopSimulation();
			return true;
		}
	}

	TEST(SnapshotCheckpointRestoresIntoPristineAwaitingWorld,
		"SeinARTS.Determinism.Snapshot.Bootstrap")
	{
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));

		FSeinEntityHandle SourceEntity;
		FString Error;
		TWeakObjectPtr<USeinFaction> SnapshotFaction;
		ASSERT_THAT(IsTrue(CreateRunningCheckpointSource(
			*Source, SourceEntity, Error, &SnapshotFaction)));
		ASSERT_THAT(IsTrue(SnapshotFaction.IsValid()));

		// Factions are held in a private runtime registry. Prove that the
		// owning subsystem, rather than this test's stack, keeps a transient
		// registered faction alive across a real GC before capture.
		CollectGarbage(RF_NoFlags);
		ASSERT_THAT(IsTrue(SnapshotFaction.IsValid()));

		FSeinWorldSnapshot Snapshot;
		Source->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion, Snapshot.SnapshotVersion));
		ASSERT_THAT(IsTrue(
			Snapshot.BootstrapCheckpoint.IsValidConsumedCheckpoint()));
		ASSERT_THAT(AreEqual(1,
			Snapshot.BootstrapCheckpoint.InitialStateContributions.Num()));
		ASSERT_THAT(IsTrue(
			Snapshot.BootstrapCheckpoint.FactionRegistrations.ContainsByPredicate(
				[](const FSeinSnapshotFactionRegistration& Registration)
				{
					return Registration.FactionID
						== FSeinFactionID(SnapshotFactionID);
				})));
		const int32 SourceHash = Source->ComputeStateHash();
		ASSERT_THAT(IsTrue(Source->StartSimulation()));
		FGuid SourceRoot;
		FString SourceRootError;
		ASSERT_THAT(IsTrue(Source->ComputeCanonicalStateRoot(
			SourceRoot, SourceRootError)));
		Source->StopSimulation();

		FActorTestSpawner DestinationSpawner;
		USeinWorldSubsystem* Destination =
			DestinationSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinMatchBootstrapState::Awaiting),
			static_cast<uint8>(Destination->GetMatchBootstrapState())));

		const int32 PristineHash = Destination->ComputeStateHash();
		const TArray<FString> PristineSystemOrder =
			Destination->GetRegisteredSystemOrderForTests();
		const TArray<FSeinPlayerID> PristinePlayers =
			Destination->GetRegisteredPlayerIDs();
		const int64 PristineNextAbilityActivationID =
			Destination->GetNextAbilityActivationID();
		const FGuid RootSentinel(
			0x13572468u,
			0x24681357u,
			0x89ABCDEFu,
			0x10293847u);
		FGuid InitialRootProbe = RootSentinel;
		FString PristineRootError;
		ASSERT_THAT(IsFalse(
			Destination->ComputeCanonicalStateRoot(
				InitialRootProbe, PristineRootError)));
		ASSERT_THAT(IsTrue(InitialRootProbe == RootSentinel));
		ASSERT_THAT(IsTrue(!PristineRootError.IsEmpty()));

		const auto AssertDestinationPristine = [&]()
		{
			ASSERT_THAT(IsFalse(
				Destination->IsExecutionTopologyFrozen()));
			ASSERT_THAT(IsTrue(
				Destination->IsExecutionTopologyValid()));
			ASSERT_THAT(IsFalse(
				Destination->GetExecutionTopologyDigest().IsValid()));
			ASSERT_THAT(IsTrue(
				Destination->GetExecutionTopologyManifest().IsEmpty()));
			ASSERT_THAT(IsTrue(
				Destination->GetRegisteredSystemOrderForTests()
					== PristineSystemOrder));
			ASSERT_THAT(AreEqual(
				PristineHash, Destination->ComputeStateHash()));
			ASSERT_THAT(IsTrue(
				Destination->GetRegisteredPlayerIDs()
					== PristinePlayers));
			ASSERT_THAT(AreEqual(
				PristineNextAbilityActivationID,
				Destination->GetNextAbilityActivationID()));
			ASSERT_THAT(AreEqual(
				0, Destination->GetEntityPool().GetActiveCount()));
			ASSERT_THAT(AreEqual(0, Destination->GetCurrentTick()));
			ASSERT_THAT(IsFalse(Destination->IsSimulationRunning()));
			ASSERT_THAT(IsTrue(
				Destination->GetMatchState()
					== ESeinMatchState::Lobby));
			ASSERT_THAT(IsTrue(
				Destination->GetMatchBootstrapState()
					== ESeinMatchBootstrapState::Awaiting));
			ASSERT_THAT(IsFalse(
				Destination->GetMatchBootstrapReceipt().IsValid()));
			ASSERT_THAT(IsFalse(
				Destination
					->GetMatchBootstrapAuthorizationContextDigest()
					.IsValid()));
			ASSERT_THAT(IsTrue(
				Destination->GetMatchBootstrapFailureReason().IsEmpty()));

			FGuid RootProbe = RootSentinel;
			FString RootError;
			ASSERT_THAT(IsFalse(
				Destination->ComputeCanonicalStateRoot(
					RootProbe, RootError)));
			ASSERT_THAT(IsTrue(RootProbe == RootSentinel));
			ASSERT_THAT(AreEqual(PristineRootError, RootError));
		};
		AssertDestinationPristine();

		FSeinWorldSnapshot UnsupportedVersion = Snapshot;
		UnsupportedVersion.SnapshotVersion =
			FSeinWorldSnapshot::CurrentVersion + 1;
		TestRunner->AddExpectedError(
			FString::Printf(
				TEXT("RestoreSnapshot: unsupported version %d (expected %d)."),
				UnsupportedVersion.SnapshotVersion,
				FSeinWorldSnapshot::CurrentVersion),
			EAutomationExpectedErrorFlags::Exact,
			1,
			false);
		ASSERT_THAT(IsFalse(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, UnsupportedVersion)));
		AssertDestinationPristine();

		FSeinWorldSnapshot InvalidPayload = Snapshot;
		FSeinSnapshotComponentStorageBlob* ExtentsBlob =
			InvalidPayload.ComponentStorageBlobs.Find(
				FSeinExtentsComponent::StaticStruct()->GetPathName());
		ASSERT_THAT(IsNotNull(ExtentsBlob));
		ASSERT_THAT(IsTrue(!ExtentsBlob->Bytes.IsEmpty()));
		ExtentsBlob->Bytes.RemoveAt(
			ExtentsBlob->Bytes.Num() - 1);
		TestRunner->AddExpectedError(
			TEXT("RestoreSnapshot: authoritative sim state failed structural preflight."),
			EAutomationExpectedErrorFlags::Exact,
			1,
			false);
		ASSERT_THAT(IsFalse(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, InvalidPayload)));
		AssertDestinationPristine();

		int32 RestoreNotificationCount = 0;
		bool bReentrantStartAccepted = true;
		bool bReentrantRestoreAccepted = true;
		bool bReentrantCaptureAccepted = true;
		FString ReentrantRestoreClaimError;
		Destination->OnRestoreSnapshotPostSim.AddLambda(
			[Destination, &Snapshot, &RestoreNotificationCount,
				&bReentrantStartAccepted,
				&bReentrantRestoreAccepted,
				&bReentrantCaptureAccepted,
				&ReentrantRestoreClaimError](
				const FSeinCameraSnapshotData&)
			{
				++RestoreNotificationCount;
				bReentrantStartAccepted = Destination->StartSimulation();
				Destination->StopSimulation();
				bReentrantRestoreAccepted =
					SeinTestSnapshotRestore::RestoreTrusted(
						*Destination,
						Snapshot,
						&ReentrantRestoreClaimError);
				FSeinWorldSnapshot NestedCapture;
				Destination->CaptureSnapshot(NestedCapture);
				bReentrantCaptureAccepted =
					NestedCapture.SnapshotVersion
						== FSeinWorldSnapshot::CurrentVersion;
			});
		FSeinWorldSnapshot WrongMapSnapshot = Snapshot;
		WrongMapSnapshot.MapIdentifier =
			TEXT("/Game/SeinFrameworkTests/DefinitelyWrongMap");
		TestRunner->AddExpectedError(
			TEXT("RestoreSnapshot: runtime compatibility mismatch"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, WrongMapSnapshot)));
		ASSERT_THAT(AreEqual(0, RestoreNotificationCount));
		ASSERT_THAT(AreEqual(0, Destination->GetCurrentTick()));
		ASSERT_THAT(IsFalse(Destination->IsSimulationRunning()));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinMatchBootstrapState::Awaiting),
			static_cast<uint8>(Destination->GetMatchBootstrapState())));
		AssertDestinationPristine();

		TestRunner->AddExpectedError(
			TEXT("Simulation start is unavailable during snapshot restore."),
			EAutomationExpectedErrorFlags::Exact, 1, false);
		TestRunner->AddExpectedError(
			TEXT("Simulation stop is unavailable during snapshot restore."),
			EAutomationExpectedErrorFlags::Exact, 1, false);
		TestRunner->AddExpectedError(
			TEXT("CaptureSnapshot: recursive or restore-overlapping capture is not permitted."),
			EAutomationExpectedErrorFlags::Exact, 1, false);
		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, Snapshot)));
		ASSERT_THAT(IsTrue(Destination->IsSimulationRunning()));
		ASSERT_THAT(IsFalse(bReentrantStartAccepted));
		ASSERT_THAT(IsFalse(bReentrantRestoreAccepted));
		ASSERT_THAT(IsFalse(bReentrantCaptureAccepted));
		ASSERT_THAT(IsTrue(ReentrantRestoreClaimError.Contains(
			TEXT("Snapshot restore authority is unavailable"))));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinMatchBootstrapState::Consumed),
			static_cast<uint8>(Destination->GetMatchBootstrapState())));
		ASSERT_THAT(IsTrue(
			Destination->GetMatchBootstrapReceipt()
				== Snapshot.BootstrapCheckpoint.Receipt));
		ASSERT_THAT(IsTrue(
			Destination->GetMatchBootstrapAuthorizationContextDigest()
				== Snapshot.BootstrapCheckpoint.AuthorizationContextDigest));
		ASSERT_THAT(IsTrue(
			Destination->IsExecutionTopologyFrozen()));
		ASSERT_THAT(IsTrue(
			Destination->GetExecutionTopologyDigest().IsValid()));
		ASSERT_THAT(IsTrue(
			!Destination->GetExecutionTopologyManifest().IsEmpty()));
		ASSERT_THAT(IsNotNull(
			Destination->GetComponent<FSeinExtentsComponent>(SourceEntity)));
		ASSERT_THAT(AreEqual(SourceHash, Destination->ComputeStateHash()));
		FGuid DestinationRoot;
		FString DestinationRootError;
		ASSERT_THAT(IsTrue(
			Destination->ComputeCanonicalStateRoot(
				DestinationRoot, DestinationRootError)));
		ASSERT_THAT(IsTrue(DestinationRoot == SourceRoot));
		ASSERT_THAT(AreEqual(1, RestoreNotificationCount));
		FSeinWorldSnapshot RoundTripSnapshot;
		Destination->CaptureSnapshot(RoundTripSnapshot);
		ASSERT_THAT(AreEqual(
			Snapshot.BootstrapCheckpoint.FactionRegistrations.Num(),
			RoundTripSnapshot.BootstrapCheckpoint.FactionRegistrations.Num()));
		ASSERT_THAT(IsTrue(
			RoundTripSnapshot.BootstrapCheckpoint.FactionRegistrations
				.ContainsByPredicate(
					[](const FSeinSnapshotFactionRegistration& Registration)
					{
						return Registration.FactionID
							== FSeinFactionID(SnapshotFactionID);
					})));
		const int32 TickBeforeSchedulerCheck = Destination->GetCurrentTick();
		FTSTicker::GetCoreTicker().Tick(
			Destination->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(AreEqual(
			TickBeforeSchedulerCheck + 1, Destination->GetCurrentTick()));
		Destination->StopSimulation();
	}

	TEST(SnapshotCaptureRejectsRecursiveCaptureWithoutCorruptingOuterResult,
		"SeinARTS.Unit.Snapshot.Bootstrap")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FSeinEntityHandle Entity;
		FString Error;
		ASSERT_THAT(IsTrue(CreateRunningCheckpointSource(
			*World, Entity, Error)));
		FSeinWorldSnapshot Baseline;
		World->CaptureSnapshot(Baseline);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			Baseline.SnapshotVersion));

		int32 CallbackCount = 0;
		bool bNestedCaptureAccepted = true;
		bool bNestedRestoreAccepted = true;
		bool bNestedStartAccepted = true;
		FString NestedRestoreClaimError;
		const FDelegateHandle Handle =
			World->OnCaptureSnapshotPostSim.AddLambda(
				[World, &Baseline, &CallbackCount,
					&bNestedCaptureAccepted, &bNestedRestoreAccepted,
					&bNestedStartAccepted, &NestedRestoreClaimError](
					FSeinCameraSnapshotData&)
				{
					++CallbackCount;
					FSeinWorldSnapshot Nested;
					World->CaptureSnapshot(Nested);
					bNestedCaptureAccepted =
						Nested.SnapshotVersion
							== FSeinWorldSnapshot::CurrentVersion;
					bNestedRestoreAccepted =
						SeinTestSnapshotRestore::RestoreTrusted(
							*World,
							Baseline,
							&NestedRestoreClaimError);
					bNestedStartAccepted = World->StartSimulation();
					World->StopSimulation();
				});
		TestRunner->AddExpectedError(
			TEXT("CaptureSnapshot: recursive or restore-overlapping capture is not permitted."),
			EAutomationExpectedErrorFlags::Exact, 1, false);
		TestRunner->AddExpectedError(
			TEXT("Simulation start is unavailable during snapshot capture."),
			EAutomationExpectedErrorFlags::Exact, 1, false);
		TestRunner->AddExpectedError(
			TEXT("Simulation stop is unavailable during snapshot capture."),
			EAutomationExpectedErrorFlags::Exact, 1, false);
		FSeinWorldSnapshot Outer;
		World->CaptureSnapshot(Outer);
		World->OnCaptureSnapshotPostSim.Remove(Handle);

		ASSERT_THAT(AreEqual(1, CallbackCount));
		ASSERT_THAT(IsFalse(bNestedCaptureAccepted));
		ASSERT_THAT(IsFalse(bNestedRestoreAccepted));
		ASSERT_THAT(IsFalse(bNestedStartAccepted));
		ASSERT_THAT(IsTrue(NestedRestoreClaimError.Contains(
			TEXT("Snapshot restore authority is unavailable"))));
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			Outer.SnapshotVersion));
	}

	TEST(SnapshotCheckpointPreflightRefusesInvalidEnvelopeWithoutMutation,
		"SeinARTS.Unit.Snapshot.Bootstrap")
	{
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));

		FSeinEntityHandle SourceEntity;
		FString Error;
		ASSERT_THAT(IsTrue(CreateRunningCheckpointSource(
			*Source, SourceEntity, Error)));
		FSeinWorldSnapshot Snapshot;
		Source->CaptureSnapshot(Snapshot);
		Snapshot.BootstrapCheckpoint.AuthorizationContextDigest.Invalidate();

		FActorTestSpawner DestinationSpawner;
		USeinWorldSubsystem* Destination =
			DestinationSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));
		const int32 HashBefore = Destination->ComputeStateHash();
		TestRunner->AddExpectedError(
			TEXT("RestoreSnapshot: invalid consumed-bootstrap checkpoint envelope."),
			EAutomationExpectedErrorFlags::Exact, 1, false);
		ASSERT_THAT(IsFalse(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, Snapshot)));
		ASSERT_THAT(AreEqual(HashBefore, Destination->ComputeStateHash()));
		ASSERT_THAT(AreEqual(0, Destination->GetCurrentTick()));
		ASSERT_THAT(IsFalse(Destination->IsSimulationRunning()));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinMatchBootstrapState::Awaiting),
			static_cast<uint8>(Destination->GetMatchBootstrapState())));
	}

	TEST(SnapshotCaptureRefusesUnconsumedBootstrapAndClearsOutput,
		"SeinARTS.Unit.Snapshot.Bootstrap")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FSeinWorldSnapshot Snapshot;
		Snapshot.CurrentTick = 99;
		TestRunner->AddExpectedError(
			TEXT("CaptureSnapshot: only a consumed, frozen bootstrap with a valid execution topology can produce a checkpoint."),
			EAutomationExpectedErrorFlags::Exact, 1, false);
		World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(0, Snapshot.SnapshotVersion));
		ASSERT_THAT(AreEqual(0, Snapshot.CurrentTick));
		ASSERT_THAT(IsFalse(
			Snapshot.BootstrapCheckpoint.IsValidConsumedCheckpoint()));
	}

	TEST(SnapshotRequiresQuiescentDeferredQueuesAndRestoreDropsOldTimeline,
		"SeinARTS.Unit.Snapshot.Bootstrap")
	{
		// Declared first so it outlives the spawned world. World teardown clears
		// the registered pointer before this stack object is destroyed.
		FQueueDestroyOncePostTickSystem DestroyProducer;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(World->RegisterSystem(&DestroyProducer)));

		FSeinEntityHandle Entity;
		const auto AuthorState = [&]()
		{
			Entity = World->SpawnAbstractEntity(
				FFixedTransform(), FSeinPlayerID::Neutral());
			World->AddComponent(
				Entity,
				FSeinDeferredDestroyTestComponent());
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0,
			TEXT("SnapshotQuiescentBoundary"))));
		ASSERT_THAT(IsTrue(Entity.IsValid()));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		FTSTicker::GetCoreTicker().Tick(
			World->GetFixedDeltaTimeSeconds());
		World->StopSimulation();

		FSeinWorldSnapshot Baseline;
		World->CaptureSnapshot(Baseline);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion, Baseline.SnapshotVersion));

		FSeinDeferredDestroyTestComponent* DestroyMarker =
			World->GetComponentMutable<FSeinDeferredDestroyTestComponent>(
				Entity);
		ASSERT_THAT(IsNotNull(DestroyMarker));
		DestroyMarker->bArmed = true;
		ASSERT_THAT(IsTrue(World->StartSimulation()));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		World->StopSimulation();

		FSeinWorldSnapshot Refused;
		Refused.CurrentTick = 99;
		TestRunner->AddExpectedError(
			TEXT("CaptureSnapshot: checkpoint capture requires a quiescent fixed-tick boundary with empty deferred queues."),
			EAutomationExpectedErrorFlags::Exact, 1, false);
		World->CaptureSnapshot(Refused);
		ASSERT_THAT(AreEqual(0, Refused.SnapshotVersion));

		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*World, Baseline)));
		ASSERT_THAT(IsTrue(World->IsEntityAlive(Entity)));
		DestroyMarker =
			World->GetComponentMutable<FSeinDeferredDestroyTestComponent>(
				Entity);
		ASSERT_THAT(IsNotNull(DestroyMarker));
		ASSERT_THAT(IsFalse(DestroyMarker->bArmed));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsTrue(World->IsEntityAlive(Entity)));
		World->StopSimulation();
	}
}

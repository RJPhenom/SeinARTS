#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Containers/Ticker.h"
#include "Core/SeinTickPhase.h"
#include "Data/SeinWorldSnapshot.h"
#include "Serialization/SeinCanonicalStateRegistry.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "TestTypes/SeinAIControllerTestTypes.h"
#include "TestTypes/SeinSnapshotValidationTestTypes.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		class FTopologyTestSystem final : public ISeinSystem
		{
		public:
			FTopologyTestSystem(
				FName StableID,
				uint32 Revision,
				ESeinTickPhase Phase,
				int32 Priority,
				TArray<FString>* InTrace = nullptr)
				: Descriptor(FSeinSystemDescriptor::Stateless(
					StableID, Revision, Phase, Priority))
				, Trace(InTrace)
				, TraceToken(StableID.ToString().ToLower())
			{
			}

			explicit FTopologyTestSystem(
				FSeinSystemDescriptor InDescriptor)
				: Descriptor(MoveTemp(InDescriptor))
				, TraceToken(
					Descriptor.StableSystemID.ToString().ToLower())
			{
			}

			virtual void Tick(
				FFixedPoint,
				USeinWorldSubsystem&) override
			{
				if (Trace)
				{
					Trace->Add(TraceToken);
				}
			}

			virtual FSeinSystemDescriptor DescribeSystem() const override
			{
				++DescribeCallCount;
				return Descriptor;
			}

			int32 GetDescribeCallCount() const
			{
				return DescribeCallCount;
			}

		private:
			FSeinSystemDescriptor Descriptor;
			TArray<FString>* Trace = nullptr;
			FString TraceToken;
			mutable int32 DescribeCallCount = 0;
		};
	}

	TEST(RegistrationPermutationProducesOneCanonicalOrder,
		"SeinARTS.Unit.CoreEntity.ExecutionTopology")
	{
		TArray<FString> FirstTrace;
		TArray<FString> SecondTrace;
		FTopologyTestSystem FirstAlpha(
			TEXT("seinarts.tests.order.alpha"), 1u,
			ESeinTickPhase::PostTick, 75, &FirstTrace);
		FTopologyTestSystem FirstBeta(
			TEXT("seinarts.tests.order.beta"), 1u,
			ESeinTickPhase::PostTick, 75, &FirstTrace);
		FTopologyTestSystem SecondAlpha(
			TEXT("seinarts.tests.order.alpha"), 1u,
			ESeinTickPhase::PostTick, 75, &SecondTrace);
		FTopologyTestSystem SecondBeta(
			TEXT("seinarts.tests.order.beta"), 1u,
			ESeinTickPhase::PostTick, 75, &SecondTrace);
		FActorTestSpawner FirstSpawner;
		FActorTestSpawner SecondSpawner;
		USeinWorldSubsystem* First =
			FirstSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		USeinWorldSubsystem* Second =
			SecondSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(First));
		ASSERT_THAT(IsNotNull(Second));

		ASSERT_THAT(IsTrue(First->RegisterSystem(&FirstBeta)));
		ASSERT_THAT(IsTrue(First->RegisterSystem(&FirstAlpha)));
		ASSERT_THAT(IsTrue(First->RegisterSystem(&FirstAlpha)));
		ASSERT_THAT(IsTrue(Second->RegisterSystem(&SecondAlpha)));
		ASSERT_THAT(IsTrue(Second->RegisterSystem(&SecondBeta)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*First, FSeinMatchSettings(), 0,
			TEXT("ExecutionTopologyPermutationA"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*Second, FSeinMatchSettings(), 0,
			TEXT("ExecutionTopologyPermutationB"))));

		ASSERT_THAT(IsTrue(First->IsExecutionTopologyFrozen()));
		ASSERT_THAT(IsTrue(Second->IsExecutionTopologyFrozen()));
		ASSERT_THAT(IsTrue(
			First->GetExecutionTopologyDigest()
				== Second->GetExecutionTopologyDigest()));
		ASSERT_THAT(AreEqual(
			First->GetExecutionTopologyManifest(),
			Second->GetExecutionTopologyManifest()));
		ASSERT_THAT(AreEqual(1, FirstAlpha.GetDescribeCallCount()));
		ASSERT_THAT(AreEqual(1, FirstBeta.GetDescribeCallCount()));
		ASSERT_THAT(AreEqual(1, SecondAlpha.GetDescribeCallCount()));
		ASSERT_THAT(AreEqual(1, SecondBeta.GetDescribeCallCount()));
		ASSERT_THAT(IsTrue(First->RegisterSystem(&FirstAlpha)));
		ASSERT_THAT(AreEqual(1, FirstAlpha.GetDescribeCallCount()));

		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*First)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*Second)));
		FTSTicker::GetCoreTicker().Tick(
			First->GetFixedDeltaTimeSeconds());
		First->StopSimulation();
		Second->StopSimulation();

		ASSERT_THAT(AreEqual(2, FirstTrace.Num()));
		ASSERT_THAT(AreEqual(2, SecondTrace.Num()));
		ASSERT_THAT(AreEqual(
			FString(TEXT("seinarts.tests.order.alpha")),
			FirstTrace[0]));
		ASSERT_THAT(AreEqual(
			FString(TEXT("seinarts.tests.order.beta")),
			FirstTrace[1]));
		ASSERT_THAT(IsTrue(FirstTrace == SecondTrace));
		ASSERT_THAT(AreEqual(1, FirstAlpha.GetDescribeCallCount()));
		ASSERT_THAT(AreEqual(1, FirstBeta.GetDescribeCallCount()));
	}

	TEST(UnspecifiedStateCoveragePoisonsPendingTopology,
		"SeinARTS.Unit.CoreEntity.ExecutionTopology")
	{
		FSeinSystemDescriptor Descriptor;
		Descriptor.StableSystemID =
			FName(TEXT("seinarts.tests.unspecified_state"));
		Descriptor.ImplementationRevision = 1u;
		Descriptor.Phase = ESeinTickPhase::PreTick;
		Descriptor.Priority = 42;
		FTopologyTestSystem Unspecified(MoveTemp(Descriptor));
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		TestRunner->AddExpectedError(
			TEXT("did not declare retained-state recapture coverage"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(World->RegisterSystem(&Unspecified)));
		ASSERT_THAT(IsFalse(World->IsExecutionTopologyValid()));
		ASSERT_THAT(IsTrue(
			World->GetExecutionTopologyFailureReason().Contains(
				TEXT("retained-state recapture coverage"))));
	}

	TEST(MissingCanonicalStateContributorFailsBeforeTickZero,
		"SeinARTS.Unit.CoreEntity.ExecutionTopology")
	{
		FTopologyTestSystem Missing(
			FSeinSystemDescriptor::WithCanonicalState(
				FName(TEXT("seinarts.tests.missing_state")),
				1u,
				ESeinTickPhase::PreTick,
				42,
				{FName(TEXT("seinarts.tests/missing-provider"))}));
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(World->RegisterSystem(&Missing)));

		TestRunner->AddExpectedError(
			TEXT("requires missing canonical-state contributor"),
			EAutomationExpectedErrorFlags::Contains, 2, false);
		TestRunner->AddExpectedError(
			TEXT("transaction closed (failed)"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		FString Error;
		ASSERT_THAT(IsFalse(SeinTestMatchBootstrap::Materialize(
			*World,
			FSeinMatchSettings(),
			0,
			TEXT("ExecutionTopologyMissingState"),
			&Error)));
		ASSERT_THAT(IsTrue(Error.Contains(
			TEXT("seinarts.tests/missing-provider"))));
		ASSERT_THAT(IsFalse(World->IsExecutionTopologyFrozen()));
		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
	}

	TEST(OrphanedUnmarkedContributorFailsBeforeTickZero,
		"SeinARTS.Unit.CoreEntity.ExecutionTopology")
	{
		FSeinCanonicalStateDescriptor Descriptor;
		Descriptor.Key.StableDomainId =
			TEXT("seinarts.tests.orphan");
		Descriptor.Key.StableContributorId =
			TEXT("unclaimed-state");
		Descriptor.SchemaVersion = 1;
		Descriptor.ImplementationRevision = 1;
		Descriptor.Role = ESeinCanonicalStateRole::DerivedCache;

		FSeinCanonicalStateContributorOps Ops;
		Ops.StageDerived = [](
			const FSeinCanonicalStateStageContext&,
			TUniquePtr<ISeinCanonicalStateRestoreStage>&,
			FString&)
			{
				return true;
			};
		Ops.CommitDerived = [](
			FSeinCanonicalStateCommitContext&,
			TUniquePtr<ISeinCanonicalStateRestoreStage>&&)
			{
			};

		FString RegistrationError;
		FSeinCanonicalStateRegistrationHandle Provider =
			FSeinCanonicalStateRegistry::Register(
				FName(TEXT("SeinFrameworkTests.ExecutionTopology")),
				Descriptor,
				MoveTemp(Ops),
				&RegistrationError);
		ASSERT_THAT(IsTrue(Provider.IsValid()));
		ASSERT_THAT(IsTrue(RegistrationError.IsEmpty()));

		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		TestRunner->AddExpectedError(
			TEXT("is claimed by no registered simulation system"),
			EAutomationExpectedErrorFlags::Contains, 2, false);
		TestRunner->AddExpectedError(
			TEXT("transaction closed (failed)"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		FString Error;
		ASSERT_THAT(IsFalse(SeinTestMatchBootstrap::Materialize(
			*World,
			FSeinMatchSettings(),
			0,
			TEXT("ExecutionTopologyOrphanReject"),
			&Error)));
		ASSERT_THAT(IsTrue(Error.Contains(
			TEXT("seinarts.tests.orphan/unclaimed-state"))));
		ASSERT_THAT(IsFalse(World->IsExecutionTopologyFrozen()));
		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
	}

	TEST(ExternallyOwnedUnclaimedContributorBootstraps,
		"SeinARTS.Unit.CoreEntity.ExecutionTopology")
	{
		FSeinCanonicalStateDescriptor Descriptor;
		Descriptor.Key.StableDomainId =
			TEXT("seinarts.tests.orphan");
		Descriptor.Key.StableContributorId =
			TEXT("subsystem-owned-state");
		Descriptor.SchemaVersion = 1;
		Descriptor.ImplementationRevision = 1;
		Descriptor.Role = ESeinCanonicalStateRole::DerivedCache;
		Descriptor.bExternallyOwned = true;

		FSeinCanonicalStateContributorOps Ops;
		Ops.StageDerived = [](
			const FSeinCanonicalStateStageContext&,
			TUniquePtr<ISeinCanonicalStateRestoreStage>&,
			FString&)
			{
				return true;
			};
		Ops.CommitDerived = [](
			FSeinCanonicalStateCommitContext&,
			TUniquePtr<ISeinCanonicalStateRestoreStage>&&)
			{
			};

		FString RegistrationError;
		FSeinCanonicalStateRegistrationHandle Provider =
			FSeinCanonicalStateRegistry::Register(
				FName(TEXT("SeinFrameworkTests.ExecutionTopology")),
				Descriptor,
				MoveTemp(Ops),
				&RegistrationError);
		ASSERT_THAT(IsTrue(Provider.IsValid()));
		ASSERT_THAT(IsTrue(RegistrationError.IsEmpty()));

		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			FSeinMatchSettings(),
			0,
			TEXT("ExecutionTopologyOrphanAccept"))));
		ASSERT_THAT(IsTrue(World->IsExecutionTopologyFrozen()));
	}

	TEST(CanonicalStateKeysAreOrderedAndBoundIntoTopologyDigest,
		"SeinARTS.Unit.CoreEntity.ExecutionTopology")
	{
		const FName NavigationKey(
			TEXT("seinarts.navigation/async-path-continuation"));
		const FName MovementKey(
			TEXT("seinarts.movement/persistent-policy-instances"));
		FTopologyTestSystem FirstSystem(
			FSeinSystemDescriptor::WithCanonicalState(
				FName(TEXT("seinarts.tests.covered_state")),
				1u,
				ESeinTickPhase::PreTick,
				42,
				{MovementKey, NavigationKey}));
		FTopologyTestSystem SecondSystem(
			FSeinSystemDescriptor::WithCanonicalState(
				FName(TEXT("seinarts.tests.covered_state")),
				1u,
				ESeinTickPhase::PreTick,
				42,
				{NavigationKey, MovementKey}));
		FTopologyTestSystem NarrowerSystem(
			FSeinSystemDescriptor::WithCanonicalState(
				FName(TEXT("seinarts.tests.covered_state")),
				1u,
				ESeinTickPhase::PreTick,
				42,
				{NavigationKey}));
		FActorTestSpawner FirstSpawner;
		FActorTestSpawner SecondSpawner;
		FActorTestSpawner NarrowerSpawner;
		USeinWorldSubsystem* First =
			FirstSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		USeinWorldSubsystem* Second =
			SecondSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		USeinWorldSubsystem* Narrower =
			NarrowerSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(First));
		ASSERT_THAT(IsNotNull(Second));
		ASSERT_THAT(IsNotNull(Narrower));
		ASSERT_THAT(IsTrue(First->RegisterSystem(&FirstSystem)));
		ASSERT_THAT(IsTrue(Second->RegisterSystem(&SecondSystem)));
		ASSERT_THAT(IsTrue(Narrower->RegisterSystem(&NarrowerSystem)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*First, FSeinMatchSettings(), 0,
			TEXT("ExecutionTopologyStateCoverageA"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*Second, FSeinMatchSettings(), 0,
			TEXT("ExecutionTopologyStateCoverageB"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*Narrower, FSeinMatchSettings(), 0,
			TEXT("ExecutionTopologyStateCoverageNarrower"))));

		ASSERT_THAT(IsTrue(
			First->GetExecutionTopologyDigest()
				== Second->GetExecutionTopologyDigest()));
		ASSERT_THAT(AreEqual(
			First->GetExecutionTopologyManifest(),
			Second->GetExecutionTopologyManifest()));
		ASSERT_THAT(IsFalse(
			First->GetExecutionTopologyDigest()
				== Narrower->GetExecutionTopologyDigest()));
		ASSERT_THAT(IsTrue(
			First->GetExecutionTopologyManifest().Contains(
				TEXT("seinarts.movement/persistent-policy-instances"))));
		ASSERT_THAT(IsTrue(
			First->GetExecutionTopologyManifest().Contains(
				TEXT("seinarts.navigation/async-path-continuation"))));
	}

	TEST(CaseFoldedDuplicateStableIDPoisonsPendingTopology,
		"SeinARTS.Unit.CoreEntity.ExecutionTopology")
	{
		FTopologyTestSystem Lower(
			TEXT("seinarts.tests.duplicate"), 1u,
			ESeinTickPhase::PreTick, 41);
		FTopologyTestSystem MixedCase(
			TEXT("SeinARTS.Tests.Duplicate"), 1u,
			ESeinTickPhase::PostTick, 42);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		ASSERT_THAT(IsTrue(World->RegisterSystem(&Lower)));
		TestRunner->AddExpectedError(
			TEXT("Duplicate simulation system stable ID"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(World->RegisterSystem(&MixedCase)));
		ASSERT_THAT(IsFalse(World->IsExecutionTopologyValid()));
		ASSERT_THAT(IsFalse(World->IsExecutionTopologyFrozen()));
		ASSERT_THAT(IsFalse(World->GetExecutionTopologyDigest().IsValid()));
		ASSERT_THAT(IsTrue(
			World->GetExecutionTopologyFailureReason().Contains(
				TEXT("seinarts.tests.duplicate"))));
	}

	TEST(ImplementationRevisionChangesTopologyAndStateContract,
		"SeinARTS.Unit.CoreEntity.ExecutionTopology")
	{
		FTopologyTestSystem RevisionOne(
			TEXT("seinarts.tests.revision"), 1u,
			ESeinTickPhase::PreTick, 42);
		FTopologyTestSystem RevisionTwo(
			TEXT("seinarts.tests.revision"), 2u,
			ESeinTickPhase::PreTick, 42);
		FActorTestSpawner FirstSpawner;
		FActorTestSpawner SecondSpawner;
		USeinWorldSubsystem* First =
			FirstSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		USeinWorldSubsystem* Second =
			SecondSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(First));
		ASSERT_THAT(IsNotNull(Second));
		ASSERT_THAT(IsTrue(First->RegisterSystem(&RevisionOne)));
		ASSERT_THAT(IsTrue(Second->RegisterSystem(&RevisionTwo)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*First, FSeinMatchSettings(), 0,
			TEXT("ExecutionTopologyRevisionA"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*Second, FSeinMatchSettings(), 0,
			TEXT("ExecutionTopologyRevisionB"))));

		ASSERT_THAT(IsFalse(
			First->GetExecutionTopologyDigest()
				== Second->GetExecutionTopologyDigest()));
		ASSERT_THAT(IsFalse(
			First->GetMatchBootstrapReceipt().StateContractDigest
				== Second->GetMatchBootstrapReceipt().StateContractDigest));
	}

	TEST(PostLaunchRemovalStopsAndInvalidatesWorld,
		"SeinARTS.Unit.CoreEntity.ExecutionTopology")
	{
		FTopologyTestSystem Removable(
			TEXT("seinarts.tests.removable"), 1u,
			ESeinTickPhase::PreTick, 42);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(World->RegisterSystem(&Removable)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, FSeinMatchSettings(), 0,
			TEXT("ExecutionTopologyLiveRemoval"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		ASSERT_THAT(IsTrue(World->IsSimulationRunning()));
		int32 InvalidationCount = 0;
		FString InvalidationReason;
		const FDelegateHandle InvalidationHandle =
			World->OnExecutionTopologyInvalidated.AddLambda(
			[&InvalidationCount, &InvalidationReason](
				const FString& Reason)
			{
				++InvalidationCount;
				InvalidationReason = Reason;
			});

		TestRunner->AddExpectedError(
			TEXT("unregistered after execution topology freeze"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(World->UnregisterSystem(&Removable)));
		ASSERT_THAT(IsFalse(World->IsExecutionTopologyValid()));
		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
		ASSERT_THAT(IsTrue(
			World->GetExecutionTopologyFailureReason().Contains(
				TEXT("seinarts.tests.removable"))));
		ASSERT_THAT(AreEqual(1, InvalidationCount));
		ASSERT_THAT(IsTrue(
			InvalidationReason.Contains(
				TEXT("seinarts.tests.removable"))));
		FSeinWorldSnapshot Snapshot;
		TestRunner->AddExpectedError(
			TEXT("only a consumed, frozen bootstrap with a valid execution topology"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(0, Snapshot.SnapshotVersion));
		World->OnExecutionTopologyInvalidated.Remove(InvalidationHandle);
	}

	TEST(ModuleUnloadInvalidationStopsAndBroadcastsExactlyOnce,
		"SeinARTS.Unit.CoreEntity.ExecutionTopology")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		USeinSnapshotTestAbility* RetainedAbility = nullptr;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			[&]()
			{
				RetainedAbility =
					NewObject<USeinSnapshotTestAbility>(World);
				RetainedAbility->InitializeAbility(
					FSeinEntityHandle::Invalid(), World);
				World->RegisterAbilityInstance(RetainedAbility);
			},
			FSeinMatchSettings(), 0,
			TEXT("ExecutionTopologyModuleUnload"))));
		ASSERT_THAT(IsNotNull(RetainedAbility));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		USeinAIControllerLifecycleProbe* RetainedController =
			NewObject<USeinAIControllerLifecycleProbe>(World);
		ASSERT_THAT(IsNotNull(RetainedController));
		World->RegisterAIController(
			RetainedController, FSeinPlayerID::Neutral());
		RetainedController->bAttemptReregisterOnUnregister = true;

		int32 InvalidationCount = 0;
		FString InvalidationReason;
		const FDelegateHandle Handle =
			World->OnExecutionTopologyInvalidated.AddLambda(
				[&InvalidationCount, &InvalidationReason](
					const FString& Reason)
				{
					++InvalidationCount;
					InvalidationReason = Reason;
				});

		TestRunner->AddExpectedError(
			TEXT("withdrew live state"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("RegisterAIController rejected on a terminal world"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		World->TerminateAndReleaseForModuleUnload(
			FName(TEXT("SeinARTSTests")),
			TEXT("test provider is unloading"));
		ASSERT_THAT(IsTrue(World->IsTerminalAfterModuleUnload()));
		ASSERT_THAT(IsNull(World->LatentActionManager));
		ASSERT_THAT(IsFalse(World->OnSimTickCompleted.IsBound()));
		ASSERT_THAT(IsFalse(World->OnCaptureSnapshotPostSim.IsBound()));
		ASSERT_THAT(IsFalse(World->GetCommandProtocolDigest().IsValid()));
		ASSERT_THAT(IsFalse(World->GetMatchSettingsDigest().IsValid()));
		ASSERT_THAT(IsFalse(World->IsSimulationContentReady()));
		ASSERT_THAT(IsTrue(World->GetAIControllers().IsEmpty()));
		ASSERT_THAT(IsNull(RetainedAbility->WorldSubsystem.Get()));
		ASSERT_THAT(AreEqual(1, RetainedController->UnregisteredCount));
		ASSERT_THAT(IsTrue(RetainedController->bUnregisteredWithWorld));
		ASSERT_THAT(IsNull(RetainedController->WorldSubsystem.Get()));

		World->OnSimTickCompleted.AddLambda([](int32) {});
		ASSERT_THAT(IsTrue(World->OnSimTickCompleted.IsBound()));
		World->TerminateAndReleaseForModuleUnload(
			FName(TEXT("SeinARTSTests")),
			TEXT("test provider is unloading"));
		ASSERT_THAT(IsFalse(World->OnSimTickCompleted.IsBound()));
		ASSERT_THAT(AreEqual(1, RetainedController->UnregisteredCount));

		ASSERT_THAT(IsFalse(World->IsExecutionTopologyValid()));
		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
		ASSERT_THAT(AreEqual(1, InvalidationCount));
		ASSERT_THAT(IsTrue(InvalidationReason.Contains(
			TEXT("seinartstests"))));
		ASSERT_THAT(IsTrue(InvalidationReason.Contains(
			TEXT("test provider is unloading"))));
		World->OnExecutionTopologyInvalidated.Remove(Handle);
	}

	TEST(MidMatchConfigMutationFailStopsBeforeNextTick,
		"SeinARTS.Unit.CoreEntity.ExecutionTopology")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		USeinARTSCoreSettings* Settings =
			GetMutableDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Settings));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, FSeinMatchSettings(), 0,
			TEXT("ExecutionTopologyConfigFreeze"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		const int32 SavedPathBudget =
			Settings->PathRequestsPerTickBudget;
		ON_SCOPE_EXIT
		{
			Settings->PathRequestsPerTickBudget =
				SavedPathBudget;
		};
		Settings->PathRequestsPerTickBudget =
			SavedPathBudget == MAX_int32
				? SavedPathBudget - 1
				: SavedPathBudget + 1;

		int32 InvalidationCount = 0;
		const FDelegateHandle Handle =
			World->OnExecutionTopologyInvalidated.AddLambda(
				[&InvalidationCount](const FString&)
				{
					++InvalidationCount;
				});
		TestRunner->AddExpectedError(
			TEXT("Lockstep configuration changed after world initialization"),
			EAutomationExpectedErrorFlags::Contains, 1, false);

		const int32 TickBefore = World->GetCurrentTick();
		FTSTicker::GetCoreTicker().Tick(
			World->GetFixedDeltaTimeSeconds());

		ASSERT_THAT(AreEqual(TickBefore, World->GetCurrentTick()));
		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
		ASSERT_THAT(IsFalse(World->IsExecutionTopologyValid()));
		ASSERT_THAT(AreEqual(1, InvalidationCount));
		World->OnExecutionTopologyInvalidated.Remove(Handle);
	}

	TEST(StaticWorldBindingDriftFailStopsBeforeAnyTickConsumer,
		"SeinARTS.Unit.CoreEntity.ExecutionTopology")
	{
		int32 BindingRevision = 1;
		FSeinCanonicalStateDescriptor Descriptor;
		Descriptor.Key.StableDomainId =
			TEXT("seinarts.tests.fixed-tick-binding");
		Descriptor.Key.StableContributorId =
			TEXT("drift-probe");
		Descriptor.SchemaVersion = 1;
		Descriptor.ImplementationRevision = 1;
		Descriptor.Role = ESeinCanonicalStateRole::DerivedCache;
		// The probe simulates a subsystem-owned binding provider; no test
		// system claims it, so it must declare external ownership to pass the
		// orphaned-contributor bootstrap gate.
		Descriptor.bExternallyOwned = true;

		FSeinCanonicalStateContributorOps Ops;
		Ops.FreezeWorldBinding =
			[&BindingRevision](
				const FSeinCanonicalStateWorldBindingContext&,
				FString& OutFrame,
				FString&)
			{
				OutFrame = FString::Printf(
					TEXT("SeinARTSTests.FixedTickBinding\n%d"),
					BindingRevision);
				return true;
			};
		Ops.StageDerived = [](
			const FSeinCanonicalStateStageContext&,
			TUniquePtr<ISeinCanonicalStateRestoreStage>&,
			FString&)
			{
				return true;
			};
		Ops.CommitDerived = [](
			FSeinCanonicalStateCommitContext&,
			TUniquePtr<ISeinCanonicalStateRestoreStage>&&)
			{
			};

		FString RegistrationError;
		FSeinCanonicalStateRegistrationHandle Provider =
			FSeinCanonicalStateRegistry::Register(
				FName(TEXT("SeinFrameworkTests.ExecutionTopology")),
				Descriptor,
				MoveTemp(Ops),
				&RegistrationError);
		ASSERT_THAT(IsTrue(Provider.IsValid()));
		ASSERT_THAT(IsTrue(RegistrationError.IsEmpty()));

		TArray<FString> EarlySystemTrace;
		FTopologyTestSystem EarlySystem(
			TEXT("seinarts.tests.early-static-consumer"),
			1u,
			ESeinTickPhase::PreTick,
			-100,
			&EarlySystemTrace);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(World->RegisterSystem(&EarlySystem)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			FSeinMatchSettings(),
			0,
			TEXT("ExecutionTopologyStaticBindingGuard"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		++BindingRevision;
		int32 InvalidationCount = 0;
		const FDelegateHandle InvalidationHandle =
			World->OnExecutionTopologyInvalidated.AddLambda(
				[&InvalidationCount](const FString&)
				{
					++InvalidationCount;
				});
		TestRunner->AddExpectedError(
			TEXT("Canonical StateContract world bindings changed"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);

		const int32 TickBefore = World->GetCurrentTick();
		FTSTicker::GetCoreTicker().Tick(
			World->GetFixedDeltaTimeSeconds());

		ASSERT_THAT(AreEqual(TickBefore, World->GetCurrentTick()));
		ASSERT_THAT(IsTrue(EarlySystemTrace.IsEmpty()));
		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
		ASSERT_THAT(IsFalse(World->IsExecutionTopologyValid()));
		ASSERT_THAT(AreEqual(1, InvalidationCount));
		World->OnExecutionTopologyInvalidated.Remove(
			InvalidationHandle);
	}

	TEST(ModuleUnloadClosesAlreadyInvalidPendingBootstrap,
		"SeinARTS.Unit.CoreEntity.ExecutionTopology")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinMatchBootstrapState::Awaiting),
			static_cast<uint8>(World->GetMatchBootstrapState())));

		int32 ClosedCount = 0;
		bool bLastAuthorized = true;
		World->OnMatchBootstrapClosed.AddLambda(
			[&ClosedCount, &bLastAuthorized](bool bAuthorized)
			{
				++ClosedCount;
				bLastAuthorized = bAuthorized;
			});

		TestRunner->AddExpectedError(
			TEXT("Execution topology invalid"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(World->RegisterSystem(nullptr)));
		ASSERT_THAT(IsFalse(World->IsExecutionTopologyValid()));

		TestRunner->AddExpectedError(
			TEXT("Match bootstrap failed closed"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("SeinMatchBootstrap: transaction closed (failed)"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		World->TerminateAndReleaseForModuleUnload(
			FName(TEXT("SeinARTSTests")),
			TEXT("test provider is unloading"));

		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinMatchBootstrapState::Failed),
			static_cast<uint8>(World->GetMatchBootstrapState())));
		ASSERT_THAT(AreEqual(1, ClosedCount));
		ASSERT_THAT(IsFalse(bLastAuthorized));
	}

	TEST(PostLaunchAdditionStopsWithoutDescribingLateSystem,
		"SeinARTS.Unit.CoreEntity.ExecutionTopology")
	{
		FTopologyTestSystem Late(
			TEXT("seinarts.tests.late"), 1u,
			ESeinTickPhase::PreTick, 42);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, FSeinMatchSettings(), 0,
			TEXT("ExecutionTopologyLiveAddition"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		ASSERT_THAT(IsTrue(World->IsSimulationRunning()));

		TestRunner->AddExpectedError(
			TEXT("registered after execution topology freeze"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(World->RegisterSystem(&Late)));
		ASSERT_THAT(AreEqual(0, Late.GetDescribeCallCount()));
		ASSERT_THAT(IsFalse(World->IsExecutionTopologyValid()));
		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
	}
}

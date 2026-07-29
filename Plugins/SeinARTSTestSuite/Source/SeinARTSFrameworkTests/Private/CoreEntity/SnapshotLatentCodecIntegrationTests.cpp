#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Abilities/Actions/SeinWaitAction.h"
#include "Abilities/SeinAbility.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Components/SeinAbilityComponent.h"
#include "Containers/Ticker.h"
#include "Data/SeinWorldSnapshot.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "Serialization/SeinCanonicalStateRegistry.h"
#include "Serialization/SeinLatentActionCodecRegistry.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "TestTypes/SeinSnapshotValidationTestTypes.h"

bool USeinSnapshotThirdPartyLatentAction::TickAction(
	FFixedPoint,
	USeinWorldSubsystem&)
{
	++TicksExecuted;
	--RemainingTicks;
	return RemainingTicks <= 0;
}

void USeinSnapshotThirdPartyLatentAction::OnTimelineAbandoned()
{
	++TimelineAbandonCount;
}

namespace UE::SeinARTSTests
{
	namespace
	{
		const FName ThirdPartyOwner(
			TEXT("SeinFrameworkTests.SnapshotThirdParty"));
		const FSeinCanonicalStateKey ThirdPartyNativeKey = []()
		{
			FSeinCanonicalStateKey Key;
			Key.StableDomainId =
				TEXT("SeinFrameworkTest.Snapshot");
			Key.StableContributorId =
				TEXT("ThirdPartyLatentDependency");
			return Key;
		}();
		constexpr int32 ThirdPartyNativeMarker = 0x4A39;

		struct FLatentWorldFixture
		{
			USeinWorldSubsystem* World = nullptr;
			FSeinEntityHandle Entity;
			int32 AbilityID = INDEX_NONE;

			bool Initialize(
				FActorTestSpawner& Spawner,
				const TCHAR* SessionLabel,
				FString& OutError)
			{
				World = Spawner.GetWorld()
					.GetSubsystem<USeinWorldSubsystem>();
				if (!World)
				{
					OutError = TEXT(
						"Snapshot latent test world subsystem is unavailable.");
					return false;
				}

				bool bAuthored = true;
				const auto AuthorState = [&]()
				{
					Entity = World->SpawnAbstractEntity(
						FFixedTransform(),
						FSeinPlayerID::Neutral());
					if (!Entity.IsValid())
					{
						bAuthored = false;
						return;
					}
					World->AddComponent(
						Entity,
						FSeinAbilityComponent());
					AbilityID = USeinAbilityBPFL::SeinGrantAbility(
						World,
						Entity,
						USeinSnapshotTestAbility::StaticClass());
					bAuthored = AbilityID != INDEX_NONE;
				};
				if (!SeinTestMatchBootstrap::Materialize(
						*World,
						AuthorState,
						FSeinMatchSettings(),
						0x4A390117,
						SessionLabel,
						&OutError)
					|| !bAuthored
					|| !SeinTestMatchBootstrap::Start(
						*World, &OutError))
				{
					return false;
				}
				World->StopSimulation();
				return true;
			}

			USeinAbility* ActivateAbility()
			{
				if (!World || AbilityID == INDEX_NONE)
				{
					return nullptr;
				}
				USeinAbility* Ability =
					World->GetAbilityInstance(AbilityID);
				if (!Ability)
				{
					return nullptr;
				}
				auto SimScope =
					FSeinSimContextTestAccess::Enter(*World);
				return Ability->ActivateAbility(
					FSeinEntityHandle::Invalid(),
					FFixedVector::ZeroVector)
					? Ability
					: nullptr;
			}
		};

		void TickRunningWorlds(
			USeinWorldSubsystem& Reference,
			int32 TickCount)
		{
			for (int32 Tick = 0; Tick < TickCount; ++Tick)
			{
				FTSTicker::GetCoreTicker().Tick(
					Reference.GetFixedDeltaTimeSeconds());
			}
		}

		bool ComputeRoot(
			USeinWorldSubsystem& World,
			FGuid& OutRoot)
		{
			FString Error;
			return World.ComputeCanonicalStateRoot(
				OutRoot, Error);
		}

		struct FThirdPartyCodecProbe
		{
			int32 CaptureCalls = 0;
			int32 StageCalls = 0;
			int32 CommitCalls = 0;
			int32 DependencyMarker = 0;
			bool bSawExactCandidateAbility = false;
		};

		struct FThirdPartyNativeProbe
		{
			int32 CaptureCalls = 0;
			int32 StageCalls = 0;
			int32 CommitCalls = 0;
		};

		struct FThirdPartyNativeRestoreStage final
			: ISeinCanonicalStateRestoreStage
		{
			FSeinSnapshotThirdPartyNativeState State;
		};

		struct FThirdPartyLatentRestoreStage final
			: ISeinLatentActionRestoreStage
		{
			FSeinSnapshotThirdPartyLatentState State;
		};

		FSeinCanonicalStateRegistrationHandle
		RegisterThirdPartyNativeContributor(
			const TSharedRef<FThirdPartyNativeProbe>& Probe,
			FString& OutError)
		{
			FSeinCanonicalStateDescriptor Descriptor;
			Descriptor.Key = ThirdPartyNativeKey;
			Descriptor.SchemaVersion = 1;
			Descriptor.ImplementationRevision = 1;
			Descriptor.Role =
				ESeinCanonicalStateRole::Authoritative;
			Descriptor.PayloadStruct =
				FSeinSnapshotThirdPartyNativeState::StaticStruct();
			Descriptor.Limits.MaxRecursionDepth = 4;
			Descriptor.Limits.MaxEncodedBytes = 64;
			Descriptor.Limits.MaxAggregateElements = 8;

			FSeinCanonicalStateContributorOps Ops;
			Ops.Capture =
				[Probe](
					const FSeinCanonicalStateCaptureContext&,
					FInstancedStruct& OutState,
					FString&)
				{
					++Probe->CaptureCalls;
					FSeinSnapshotThirdPartyNativeState State;
					State.Marker = ThirdPartyNativeMarker;
					OutState.InitializeAs<
						FSeinSnapshotThirdPartyNativeState>(
							State);
					return true;
				};
			Ops.StageRestore =
				[Probe](
					const FSeinCanonicalStateStageContext&,
					const FInstancedStruct& Payload,
					TUniquePtr<ISeinCanonicalStateRestoreStage>&
						OutStage,
					FString& OutStageError)
				{
					const FSeinSnapshotThirdPartyNativeState* State =
						Payload.GetPtr<
							FSeinSnapshotThirdPartyNativeState>();
					if (!State
						|| State->Marker != ThirdPartyNativeMarker)
					{
						OutStageError = TEXT(
							"Third-party native staging received invalid state.");
						return false;
					}
					++Probe->StageCalls;
					TUniquePtr<FThirdPartyNativeRestoreStage> Stage =
						MakeUnique<FThirdPartyNativeRestoreStage>();
					Stage->State = *State;
					OutStage = MoveTemp(Stage);
					return true;
				};
			Ops.CommitRestore =
				[Probe](
					FSeinCanonicalStateCommitContext&,
					TUniquePtr<ISeinCanonicalStateRestoreStage>&&
						BaseStage)
				{
					const FThirdPartyNativeRestoreStage* Stage =
						static_cast<
							FThirdPartyNativeRestoreStage*>(
								BaseStage.Get());
					check(Stage
						&& Stage->State.Marker
							== ThirdPartyNativeMarker);
					++Probe->CommitCalls;
				};
			return FSeinCanonicalStateRegistry::Register(
				ThirdPartyOwner,
				Descriptor,
				MoveTemp(Ops),
				&OutError);
		}

		FSeinLatentActionCodecRegistrationHandle
		RegisterThirdPartyLatentCodec(
			const TSharedRef<FThirdPartyCodecProbe>& Probe,
			FString& OutError)
		{
			FGuid SchemaDigest;
			if (!FSeinCanonicalStateCodec::ComputeSchemaDigest(
					FSeinSnapshotThirdPartyLatentState::StaticStruct(),
					SchemaDigest,
					OutError))
			{
				return {};
			}

			FSeinLatentActionCodecDescriptor Descriptor;
			Descriptor.SupportedClass =
				USeinSnapshotThirdPartyLatentAction::StaticClass();
			Descriptor.StableCodecId =
				TEXT("seinframeworktests.snapshot.thirdparty");
			Descriptor.StateSchemaVersion = 1;
			Descriptor.BehaviorRevision = 1;
			Descriptor.CodecRevision = 1;
			Descriptor.PayloadStruct =
				FSeinSnapshotThirdPartyLatentState::StaticStruct();
			Descriptor.PayloadSchemaDigest = SchemaDigest;
			Descriptor.Limits.MaxRecursionDepth = 4;
			Descriptor.Limits.MaxEncodedBytes = 64;
			Descriptor.Limits.MaxAggregateElements = 8;
			Descriptor.RequiredNativeContributors.Add(
				ThirdPartyNativeKey);

			FSeinLatentActionCodecOps Ops;
			Ops.Capture =
				[Probe](
					const FSeinLatentActionCaptureContext& Context,
					FInstancedStruct& OutState,
					FString& OutCaptureError)
				{
					const USeinSnapshotThirdPartyLatentAction* Action =
						Cast<USeinSnapshotThirdPartyLatentAction>(
							&Context.Action);
					if (!Action
						|| Action->GetClass()
							!= USeinSnapshotThirdPartyLatentAction::
								StaticClass())
					{
						OutCaptureError = TEXT(
							"Third-party codec received a non-exact action.");
						return false;
					}
					++Probe->CaptureCalls;
					FSeinSnapshotThirdPartyLatentState State;
					State.RemainingTicks = Action->RemainingTicks;
					State.TicksExecuted = Action->TicksExecuted;
					OutState.InitializeAs<
						FSeinSnapshotThirdPartyLatentState>(
							State);
					return true;
				};
			Ops.StageRestore =
				[Probe](
					const FSeinLatentActionStageContext& Context,
					const FInstancedStruct& Payload,
					TUniquePtr<ISeinLatentActionRestoreStage>&
						OutStage,
					FString& OutStageError)
				{
					const FSeinSnapshotThirdPartyLatentState* State =
						Payload.GetPtr<
							FSeinSnapshotThirdPartyLatentState>();
					const FInstancedStruct* Dependency =
						Context.Dependencies
							? Context.Dependencies->FindStagedPayload(
								ThirdPartyNativeKey)
							: nullptr;
					const FSeinSnapshotThirdPartyNativeState*
						NativeState = Dependency
							? Dependency->GetPtr<
								FSeinSnapshotThirdPartyNativeState>()
							: nullptr;
					const USeinAbility* CandidateAbility =
						Context.Candidate && Context.Record
							? Context.Candidate->FindAbility(
								Context.Record->AbilityPoolID)
							: nullptr;
					if (!State
						|| State->RemainingTicks <= 0
						|| !NativeState
						|| NativeState->Marker
							!= ThirdPartyNativeMarker
						|| !CandidateAbility
						|| !Context.Record
						|| CandidateAbility->GetActivationID()
							!= Context.Record->AbilityActivationID)
					{
						OutStageError = TEXT(
							"Third-party latent staging did not receive its exact candidate and dependency.");
						return false;
					}

					++Probe->StageCalls;
					Probe->DependencyMarker = NativeState->Marker;
					Probe->bSawExactCandidateAbility = true;
					TUniquePtr<FThirdPartyLatentRestoreStage> Stage =
						MakeUnique<FThirdPartyLatentRestoreStage>();
					Stage->State = *State;
					OutStage = MoveTemp(Stage);
					return true;
				};
			Ops.CommitRestore =
				[Probe](
					FSeinLatentActionCommitContext& Context,
					TUniquePtr<ISeinLatentActionRestoreStage>&&
						BaseStage)
				{
					const FThirdPartyLatentRestoreStage* Stage =
						static_cast<FThirdPartyLatentRestoreStage*>(
							BaseStage.Get());
					check(Stage);
					++Probe->CommitCalls;
					USeinSnapshotThirdPartyLatentAction* Action =
						NewObject<
							USeinSnapshotThirdPartyLatentAction>(
								&Context.World);
					check(Action);
					Action->RemainingTicks =
						Stage->State.RemainingTicks;
					Action->TicksExecuted =
						Stage->State.TicksExecuted;
					return Action;
				};
			return FSeinLatentActionCodecRegistry::Register(
				ThirdPartyOwner,
				Descriptor,
				MoveTemp(Ops),
				&OutError);
		}

		USeinSnapshotThirdPartyLatentAction*
		RegisterThirdPartyAction(
			FLatentWorldFixture& Fixture,
			int32 RemainingTicks)
		{
			USeinAbility* Ability = Fixture.ActivateAbility();
			if (!Ability || RemainingTicks <= 0)
			{
				return nullptr;
			}
			USeinSnapshotThirdPartyLatentAction* Action =
				NewObject<USeinSnapshotThirdPartyLatentAction>(
					Fixture.World->LatentActionManager);
			Action->OwningAbility = Ability;
			Action->OwnerEntity = Fixture.Entity;
			Action->RemainingTicks = RemainingTicks;
			return Fixture.World->LatentActionManager
				->RegisterAction(Action)
					? Action
					: nullptr;
		}
	}

	TEST(SnapshotWaitContinuationFreshWorldsPreserveFutureTimeline,
		"SeinARTS.Determinism.Snapshot.Latent.Wait")
	{
		FActorTestSpawner SourceSpawner;
		FLatentWorldFixture SourceFixture;
		FString Error;
		ASSERT_THAT(IsTrue(SourceFixture.Initialize(
			SourceSpawner,
			TEXT("SnapshotWaitFreshSource"),
			Error)));

		ASSERT_THAT(IsTrue(
			SourceFixture.World->StartSimulation()));
		USeinAbility* SourceAbility =
			SourceFixture.ActivateAbility();
		ASSERT_THAT(IsNotNull(SourceAbility));
		const int32 CompletionTicks = 6;
		const int32 PreCaptureTicks = 2;
		const USeinARTSCoreSettings* CoreSettings =
			GetDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(CoreSettings));
		const int32 TickRate =
			CoreSettings->SimulationTickRate;
		ASSERT_THAT(IsTrue(TickRate > 0));
		const FFixedPoint SimDelta =
			FFixedPoint::One / FFixedPoint::FromInt(TickRate);
		USeinWaitAction* SourceWait =
			NewObject<USeinWaitAction>(
				SourceFixture.World->LatentActionManager);
		ASSERT_THAT(IsNotNull(SourceWait));
		SourceWait->OwningAbility = SourceAbility;
		SourceWait->OwnerEntity = SourceFixture.Entity;
		SourceWait->Initialize(
			SimDelta * FFixedPoint::FromInt(CompletionTicks));
		ASSERT_THAT(IsTrue(
			SourceFixture.World->LatentActionManager
				->RegisterAction(SourceWait)));

		TickRunningWorlds(
			*SourceFixture.World, PreCaptureTicks);
		SourceFixture.World->StopSimulation();
		ASSERT_THAT(AreEqual(
			PreCaptureTicks,
			SourceFixture.World->GetCurrentTick()));
		ASSERT_THAT(AreEqual(
			1,
			SourceFixture.World->LatentActionManager
				->GetActiveActionCount()));

		FSeinWorldSnapshot Snapshot;
		SourceFixture.World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			Snapshot.SnapshotVersion));
		ASSERT_THAT(AreEqual(
			1, Snapshot.LatentActionRecords.Num()));

		FActorTestSpawner DestinationSpawner;
		USeinWorldSubsystem* Destination =
			DestinationSpawner.GetWorld()
				.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));
		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, Snapshot)));
		ASSERT_THAT(AreEqual(
			Snapshot.CurrentTick,
			Destination->GetCurrentTick()));
		ASSERT_THAT(AreEqual(
			1,
			Destination->LatentActionManager
				->GetActiveActionCount()));
		USeinWaitAction* DestinationWait =
			Cast<USeinWaitAction>(
				Destination->LatentActionManager
					->GetActiveActions()[0]);
		ASSERT_THAT(IsNotNull(DestinationWait));

		ASSERT_THAT(IsTrue(
			SourceFixture.World->StartSimulation()));
		ASSERT_THAT(IsTrue(Destination->StartSimulation()));
		FGuid SourceRoot;
		FGuid DestinationRoot;
		ASSERT_THAT(IsTrue(ComputeRoot(
			*SourceFixture.World, SourceRoot)));
		ASSERT_THAT(IsTrue(ComputeRoot(
			*Destination, DestinationRoot)));
		ASSERT_THAT(IsTrue(SourceRoot == DestinationRoot));

		const int32 FutureTicks =
			CompletionTicks - PreCaptureTicks;
		for (int32 Step = 1; Step <= FutureTicks; ++Step)
		{
			TickRunningWorlds(*SourceFixture.World, 1);
			ASSERT_THAT(AreEqual(
				Snapshot.CurrentTick + Step,
				SourceFixture.World->GetCurrentTick()));
			ASSERT_THAT(AreEqual(
				SourceFixture.World->GetCurrentTick(),
				Destination->GetCurrentTick()));

			const bool bShouldBeComplete =
				Step == FutureTicks;
			ASSERT_THAT(AreEqual(
				bShouldBeComplete ? 0 : 1,
				SourceFixture.World->LatentActionManager
					->GetActiveActionCount()));
			ASSERT_THAT(AreEqual(
				bShouldBeComplete ? 0 : 1,
				Destination->LatentActionManager
					->GetActiveActionCount()));
			ASSERT_THAT(AreEqual(
				bShouldBeComplete,
				SourceWait->bCompleted));
			ASSERT_THAT(AreEqual(
				bShouldBeComplete,
				DestinationWait->bCompleted));

			ASSERT_THAT(IsTrue(ComputeRoot(
				*SourceFixture.World, SourceRoot)));
			ASSERT_THAT(IsTrue(ComputeRoot(
				*Destination, DestinationRoot)));
			ASSERT_THAT(IsTrue(SourceRoot == DestinationRoot));
		}
		SourceFixture.World->StopSimulation();
		Destination->StopSimulation();
	}

	TEST(SnapshotThirdPartyLatentCodecIsReloadExactAndUnloadSafe,
		"SeinARTS.Integration.Snapshot.Latent.ThirdParty")
	{
		FString Error;
		const int32 NativeRegistryBaseline =
			FSeinCanonicalStateRegistry::
				GetRegisteredContributorCount();
		const int32 CodecRegistryBaseline =
			FSeinLatentActionCodecRegistry::
				GetRegisteredCodecCount();
		const TSharedRef<FThirdPartyNativeProbe> NativeProbe =
			MakeShared<FThirdPartyNativeProbe>();
		FSeinCanonicalStateRegistrationHandle NativeProvider =
			RegisterThirdPartyNativeContributor(
				NativeProbe, Error);
		ASSERT_THAT(IsTrue(NativeProvider.IsValid()));

		const TSharedRef<FThirdPartyCodecProbe> FirstProbe =
			MakeShared<FThirdPartyCodecProbe>();
		FSeinLatentActionCodecRegistrationHandle FirstCodec =
			RegisterThirdPartyLatentCodec(FirstProbe, Error);
		ASSERT_THAT(IsTrue(FirstCodec.IsValid()));

		FActorTestSpawner SourceSpawner;
		FLatentWorldFixture SourceFixture;
		ASSERT_THAT(IsTrue(SourceFixture.Initialize(
			SourceSpawner,
			TEXT("SnapshotThirdPartySource"),
			Error)));
		FActorTestSpawner DestinationSpawner;
		USeinWorldSubsystem* Destination =
			DestinationSpawner.GetWorld()
				.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));

		ASSERT_THAT(IsTrue(
			SourceFixture.World->StartSimulation()));
		USeinSnapshotThirdPartyLatentAction* SourceAction =
			RegisterThirdPartyAction(SourceFixture, 5);
		ASSERT_THAT(IsNotNull(SourceAction));
		const int64 ExpectedActionID =
			SourceAction->GetActionID();
		const int64 ExpectedActivationID =
			SourceAction->GetAbilityActivationID();
		TickRunningWorlds(*SourceFixture.World, 1);
		SourceFixture.World->StopSimulation();

		FSeinWorldSnapshot Snapshot;
		SourceFixture.World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			Snapshot.SnapshotVersion));
		ASSERT_THAT(AreEqual(
			1, Snapshot.LatentActionRecords.Num()));
		ASSERT_THAT(IsTrue(FirstProbe->CaptureCalls > 0));
		ASSERT_THAT(IsTrue(NativeProbe->CaptureCalls > 0));

		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, Snapshot)));
		ASSERT_THAT(AreEqual(1, FirstProbe->StageCalls));
		ASSERT_THAT(AreEqual(1, FirstProbe->CommitCalls));
		ASSERT_THAT(AreEqual(
			ThirdPartyNativeMarker,
			FirstProbe->DependencyMarker));
		ASSERT_THAT(IsTrue(
			FirstProbe->bSawExactCandidateAbility));
		ASSERT_THAT(AreEqual(1, NativeProbe->StageCalls));
		ASSERT_THAT(AreEqual(1, NativeProbe->CommitCalls));

		USeinSnapshotThirdPartyLatentAction* DestinationAction =
			Cast<USeinSnapshotThirdPartyLatentAction>(
				Destination->LatentActionManager
					->GetActiveActions()[0]);
		ASSERT_THAT(IsNotNull(DestinationAction));
		ASSERT_THAT(AreEqual(
			ExpectedActionID,
			DestinationAction->GetActionID()));
		ASSERT_THAT(AreEqual(
			ExpectedActivationID,
			DestinationAction->GetAbilityActivationID()));
		ASSERT_THAT(AreEqual(
			SourceAction->RemainingTicks,
			DestinationAction->RemainingTicks));
		ASSERT_THAT(AreEqual(
			SourceAction->TicksExecuted,
			DestinationAction->TicksExecuted));

		ASSERT_THAT(IsTrue(
			SourceFixture.World->StartSimulation()));
		ASSERT_THAT(IsTrue(Destination->StartSimulation()));
		FGuid SourceRoot;
		FGuid DestinationRoot;
		ASSERT_THAT(IsTrue(ComputeRoot(
			*SourceFixture.World, SourceRoot)));
		ASSERT_THAT(IsTrue(ComputeRoot(
			*Destination, DestinationRoot)));
		ASSERT_THAT(IsTrue(SourceRoot == DestinationRoot));
		TickRunningWorlds(*SourceFixture.World, 1);
		SourceFixture.World->StopSimulation();
		Destination->StopSimulation();
		ASSERT_THAT(AreEqual(
			SourceAction->RemainingTicks,
			DestinationAction->RemainingTicks));
		ASSERT_THAT(AreEqual(
			SourceAction->TicksExecuted,
			DestinationAction->TicksExecuted));
		ASSERT_THAT(IsTrue(
			SourceFixture.World->StartSimulation()));
		ASSERT_THAT(IsTrue(Destination->StartSimulation()));
		ASSERT_THAT(IsTrue(ComputeRoot(
			*SourceFixture.World, SourceRoot)));
		ASSERT_THAT(IsTrue(ComputeRoot(
			*Destination, DestinationRoot)));
		ASSERT_THAT(IsTrue(SourceRoot == DestinationRoot));
		SourceFixture.World->StopSimulation();
		Destination->StopSimulation();

		const TSharedRef<FThirdPartyCodecProbe> ReloadedProbe =
			MakeShared<FThirdPartyCodecProbe>();
		FSeinLatentActionCodecRegistrationHandle ReloadedCodec =
			RegisterThirdPartyLatentCodec(
				ReloadedProbe, Error);
		ASSERT_THAT(IsTrue(ReloadedCodec.IsValid()));

		const int32 FirstCapturesBeforeOldWorldProbe =
			FirstProbe->CaptureCalls;
		FSeinWorldSnapshot OldGenerationSnapshot;
		SourceFixture.World->CaptureSnapshot(
			OldGenerationSnapshot);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			OldGenerationSnapshot.SnapshotVersion));
		ASSERT_THAT(IsTrue(
			FirstProbe->CaptureCalls
				> FirstCapturesBeforeOldWorldProbe));
		ASSERT_THAT(AreEqual(
			0, ReloadedProbe->CaptureCalls));

		FActorTestSpawner ReloadedSpawner;
		FLatentWorldFixture ReloadedFixture;
		ASSERT_THAT(IsTrue(ReloadedFixture.Initialize(
			ReloadedSpawner,
			TEXT("SnapshotThirdPartyReloaded"),
			Error)));
		ASSERT_THAT(IsTrue(
			ReloadedFixture.World->StartSimulation()));
		USeinSnapshotThirdPartyLatentAction* ReloadedAction =
			RegisterThirdPartyAction(ReloadedFixture, 5);
		ASSERT_THAT(IsNotNull(ReloadedAction));
		TickRunningWorlds(*ReloadedFixture.World, 1);
		ReloadedFixture.World->StopSimulation();
		FSeinWorldSnapshot ReloadedSnapshot;
		ReloadedFixture.World->CaptureSnapshot(
			ReloadedSnapshot);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			ReloadedSnapshot.SnapshotVersion));
		ASSERT_THAT(IsTrue(
			ReloadedProbe->CaptureCalls > 0));

		TestRunner->AddExpectedError(
			TEXT("withdrew live state"),
			EAutomationExpectedErrorFlags::Contains,
			2,
			false);
		ASSERT_THAT(IsTrue(
			FSeinLatentActionCodecRegistry::Unregister(
				FirstCodec)));
		ASSERT_THAT(IsFalse(FirstCodec.IsValid()));
		ASSERT_THAT(IsTrue(
			SourceFixture.World
				->IsTerminalAfterModuleUnload()));
		ASSERT_THAT(IsTrue(
			Destination->IsTerminalAfterModuleUnload()));
		ASSERT_THAT(IsFalse(
			ReloadedFixture.World
				->IsTerminalAfterModuleUnload()));
		ASSERT_THAT(AreEqual(
			1, SourceAction->TimelineAbandonCount));
		ASSERT_THAT(AreEqual(
			1, DestinationAction->TimelineAbandonCount));
		ASSERT_THAT(AreEqual(
			0, ReloadedAction->TimelineAbandonCount));
		ASSERT_THAT(IsNull(
			SourceFixture.World->LatentActionManager));
		ASSERT_THAT(IsNull(
			Destination->LatentActionManager));
		ASSERT_THAT(IsNotNull(
			ReloadedFixture.World->LatentActionManager));

		TestRunner->AddExpectedError(
			TEXT("withdrew live state"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		ASSERT_THAT(IsTrue(
			FSeinCanonicalStateRegistry::Unregister(
				NativeProvider)));
		ASSERT_THAT(IsFalse(NativeProvider.IsValid()));
		ASSERT_THAT(IsTrue(
			ReloadedFixture.World
				->IsTerminalAfterModuleUnload()));
		ASSERT_THAT(AreEqual(
			1, ReloadedAction->TimelineAbandonCount));
		ASSERT_THAT(IsNull(
			ReloadedFixture.World->LatentActionManager));

		// Terminal cleanup released the frozen manifest, so withdrawing the
		// replacement codec is now a quiet registry-only operation.
		ASSERT_THAT(IsTrue(
			FSeinLatentActionCodecRegistry::Unregister(
				ReloadedCodec)));
		ASSERT_THAT(IsFalse(ReloadedCodec.IsValid()));
		ASSERT_THAT(AreEqual(
			NativeRegistryBaseline,
			FSeinCanonicalStateRegistry::
				GetRegisteredContributorCount()));
		ASSERT_THAT(AreEqual(
			CodecRegistryBaseline,
			FSeinLatentActionCodecRegistry::
				GetRegisteredCodecCount()));
	}
}

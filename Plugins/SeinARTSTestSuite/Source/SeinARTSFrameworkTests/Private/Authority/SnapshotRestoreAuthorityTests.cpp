#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Containers/Ticker.h"
#include "Data/SeinMatchSettings.h"
#include "Data/SeinWorldSnapshot.h"
#include "GameFramework/Actor.h"
#include "Simulation/SeinSnapshotRestoreAuthority.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"

namespace
{
	const FName SnapshotAuthorityTestID(
		TEXT("SeinARTSFrameworkTests.SnapshotRestoreAuthority"));

	struct FObservedWorldState
	{
		explicit FObservedWorldState(const USeinWorldSubsystem& World)
			: BootstrapState(World.GetMatchBootstrapState())
			, CurrentTick(World.GetCurrentTick())
			, StateHash(World.ComputeStateHash())
			, PendingCommandCount(World.GetPendingCommands().Num())
			, bRunning(World.IsSimulationRunning())
		{
		}

		bool Matches(const USeinWorldSubsystem& World) const
		{
			return BootstrapState == World.GetMatchBootstrapState()
				&& CurrentTick == World.GetCurrentTick()
				&& StateHash == World.ComputeStateHash()
				&& PendingCommandCount
					== World.GetPendingCommands().Num()
				&& bRunning == World.IsSimulationRunning();
		}

		ESeinMatchBootstrapState BootstrapState;
		int32 CurrentTick = 0;
		int32 StateHash = 0;
		int32 PendingCommandCount = 0;
		bool bRunning = false;
	};

	struct FSnapshotRestoreAuthorityFixture
	{
		FActorTestSpawner SourceSpawner;
		FActorTestSpawner DestinationSpawner;
		USeinWorldSubsystem* Source = nullptr;
		USeinWorldSubsystem* Destination = nullptr;
		FSeinEntityHandle Entity;
		FSeinWorldSnapshot Snapshot;
		FString Error;

		FSnapshotRestoreAuthorityFixture()
		{
			Source =
				SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			Destination =
				DestinationSpawner.GetWorld()
					.GetSubsystem<USeinWorldSubsystem>();
			if (!Source || !Destination)
			{
				Error = TEXT("Snapshot authority fixture has no world subsystem.");
				return;
			}

			const auto AuthorState = [this]()
			{
				Source->RegisterPlayer(
					FSeinPlayerID(1), FSeinFactionID(1));
				Entity = Source->SpawnAbstractEntity(
					FFixedTransform(), FSeinPlayerID(1));
			};
			if (!SeinTestMatchBootstrap::Materialize(
					*Source,
					AuthorState,
					FSeinMatchSettings(),
					0x534E4150,
					SnapshotAuthorityTestID,
					&Error)
				|| !SeinTestMatchBootstrap::Start(*Source, &Error))
			{
				return;
			}

			FTSTicker::GetCoreTicker().Tick(
				Source->GetFixedDeltaTimeSeconds());
			if (Source->GetCurrentTick() <= 0)
			{
				Error =
					TEXT("Snapshot authority fixture did not advance.");
				return;
			}

			FSeinCommand PlayerCommand =
				FSeinCommand::MakePingCommand(
					FSeinPlayerID(1), FFixedVector());
			PlayerCommand.Tick = Source->GetCurrentTick() + 11;
			Source->SubmitLocalCommandDraft(PlayerCommand);

			FSeinCommand AdministratorCommand;
			AdministratorCommand.CommandType =
				SeinARTSTags::Command_Type_EndMatch;
			AdministratorCommand.Tick =
				Source->GetCurrentTick() + 12;
			FSeinEndMatchCommandPayload EndMatchPayload;
			EndMatchPayload.Winner = FSeinPlayerID(1);
			AdministratorCommand.Payload
				.InitializeAs<FSeinEndMatchCommandPayload>(
					EndMatchPayload);
			Source->SubmitLocalCommandDraft(
				AdministratorCommand,
				/*bRequestMatchAdministration=*/true);

			FSeinCommand SystemCommand =
				FSeinCommand::MakeAbilityCommand(
					FSeinPlayerID(1),
					Entity,
					SeinARTSTags::Command_Context_AbilityTriggered);
			SystemCommand.Tick = Source->GetCurrentTick() + 13;
			SystemCommand.DerivedResourcePayer = FSeinPlayerID(1);
			{
				auto SimScope =
					FSeinSimContextTestAccess::Enter(*Source);
				Source->EnqueueDerivedCommand(SystemCommand);
			}

			if (Source->GetPendingCommands().Num() != 3)
			{
				Error =
					TEXT("Snapshot authority fixture could not stage player, administrator, and deterministic-system continuations.");
				return;
			}

			Source->StopSimulation();
			Source->CaptureSnapshot(Snapshot);
			if (Snapshot.SnapshotVersion
					!= FSeinWorldSnapshot::CurrentVersion
				|| Snapshot.PendingCommands.Num() != 3)
			{
				Error =
					TEXT("Snapshot authority fixture capture was refused or incomplete.");
			}
		}

		~FSnapshotRestoreAuthorityFixture()
		{
			if (Source)
			{
				Source->StopSimulation();
			}
			if (Destination)
			{
				Destination->StopSimulation();
			}
		}

		bool IsReady() const
		{
			return Source && Destination && Error.IsEmpty()
				&& Snapshot.SnapshotVersion
					== FSeinWorldSnapshot::CurrentVersion;
		}

		bool Claim(
			USeinWorldSubsystem& World,
			const UObject* Owner,
			FSeinSnapshotRestoreAuthorityHandle& OutAuthority,
			FName AuthorityID = SnapshotAuthorityTestID)
		{
			Error.Reset();
			return World.ClaimSnapshotRestoreAuthority(
				AuthorityID, Owner, OutAuthority, Error);
		}
	};

	const FSeinSnapshotRestoreOptions PreserveAndStop(
		ESeinSnapshotLocalStateRestorePolicy::PreserveCurrent,
		ESeinSnapshotResumePolicy::RemainStopped);
}

namespace UE::SeinARTSTests
{
	TEST(SnapshotRestoreAuthorityRejectsInvalidForeignReleasedAndConsumedAttempts,
		"SeinARTS.Unit.Authority.SnapshotRestore")
	{
		FSnapshotRestoreAuthorityFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.IsReady()));

		FSeinWorldSnapshot Malformed = Fixture.Snapshot;
		Malformed.SnapshotVersion = 0;
		const FObservedWorldState Pristine(*Fixture.Destination);

		FSeinSnapshotRestoreAuthorityHandle Invalid;
		Assert.ExpectError(TEXT(
			"RestoreSnapshot: rejected without this world's exact trusted-envelope authority."));
		ASSERT_THAT(IsFalse(Fixture.Destination->RestoreSnapshot(
			MoveTemp(Invalid), Malformed, PreserveAndStop)));
		ASSERT_THAT(IsTrue(Pristine.Matches(*Fixture.Destination)));

		FSeinSnapshotRestoreAuthorityHandle Foreign;
		ASSERT_THAT(IsTrue(Fixture.Claim(
			*Fixture.Source, Fixture.Source, Foreign)));
		Assert.ExpectError(TEXT(
			"RestoreSnapshot: rejected without this world's exact trusted-envelope authority."));
		ASSERT_THAT(IsFalse(Fixture.Destination->RestoreSnapshot(
			MoveTemp(Foreign), Malformed, PreserveAndStop)));
		ASSERT_THAT(IsTrue(Pristine.Matches(*Fixture.Destination)));
		FString ReleaseError;
		ASSERT_THAT(IsTrue(Fixture.Source->ReleaseSnapshotRestoreAuthority(
			MoveTemp(Foreign), ReleaseError)));

		FSeinSnapshotRestoreAuthorityHandle Released;
		ASSERT_THAT(IsTrue(Fixture.Claim(
			*Fixture.Destination, Fixture.Destination, Released)));
		ASSERT_THAT(IsTrue(
			Fixture.Destination->ReleaseSnapshotRestoreAuthority(
				MoveTemp(Released), ReleaseError)));
		ASSERT_THAT(IsFalse(Released.IsValid()));
		Assert.ExpectError(TEXT(
			"RestoreSnapshot: rejected without this world's exact trusted-envelope authority."));
		ASSERT_THAT(IsFalse(Fixture.Destination->RestoreSnapshot(
			MoveTemp(Released), Malformed, PreserveAndStop)));
		ASSERT_THAT(IsTrue(Pristine.Matches(*Fixture.Destination)));

		FSeinSnapshotRestoreAuthorityHandle Consumed;
		ASSERT_THAT(IsTrue(Fixture.Claim(
			*Fixture.Destination, Fixture.Destination, Consumed)));
		Assert.ExpectError(TEXT("RestoreSnapshot: unsupported version"));
		ASSERT_THAT(IsFalse(Fixture.Destination->RestoreSnapshot(
			MoveTemp(Consumed), Malformed, PreserveAndStop)));
		ASSERT_THAT(IsFalse(Consumed.IsValid()));
		ASSERT_THAT(IsTrue(Pristine.Matches(*Fixture.Destination)));

		Assert.ExpectError(TEXT(
			"RestoreSnapshot: rejected without this world's exact trusted-envelope authority."));
		ASSERT_THAT(IsFalse(Fixture.Destination->RestoreSnapshot(
			MoveTemp(Consumed), Fixture.Snapshot, PreserveAndStop)));
		ASSERT_THAT(IsTrue(Pristine.Matches(*Fixture.Destination)));

		FSeinSnapshotRestoreAuthorityHandle InvalidPolicyAuthority;
		ASSERT_THAT(IsTrue(Fixture.Claim(
			*Fixture.Destination,
			Fixture.Destination,
			InvalidPolicyAuthority)));
		const FSeinSnapshotRestoreOptions InvalidPolicy(
			static_cast<ESeinSnapshotLocalStateRestorePolicy>(255),
			ESeinSnapshotResumePolicy::RemainStopped);
		Assert.ExpectError(TEXT(
			"RestoreSnapshot: invalid local-state restore policy."));
		ASSERT_THAT(IsFalse(Fixture.Destination->RestoreSnapshot(
			MoveTemp(InvalidPolicyAuthority),
			Fixture.Snapshot,
			InvalidPolicy)));
		ASSERT_THAT(IsFalse(InvalidPolicyAuthority.IsValid()));
		ASSERT_THAT(IsTrue(Pristine.Matches(*Fixture.Destination)));
	}

	TEST(SnapshotRestoreAuthorityClaimIsIdempotentForOneOwnerAndRejectsCompetitors,
		"SeinARTS.Unit.Authority.SnapshotRestore")
	{
		FSnapshotRestoreAuthorityFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.IsReady()));
		const FObservedWorldState Pristine(*Fixture.Destination);

		FSeinSnapshotRestoreAuthorityHandle OccupiedOutput;
		ASSERT_THAT(IsTrue(Fixture.Claim(
			*Fixture.Source, Fixture.Source, OccupiedOutput)));
		FString OccupiedOutputError;
		ASSERT_THAT(IsFalse(
			Fixture.Destination->ClaimSnapshotRestoreAuthority(
				SnapshotAuthorityTestID,
				Fixture.Destination,
				OccupiedOutput,
				OccupiedOutputError)));
		ASSERT_THAT(IsTrue(OccupiedOutput.IsValid()));
		ASSERT_THAT(IsTrue(OccupiedOutputError.Contains(
			TEXT("output handle must be invalid"))));
		FString ReleaseError;
		ASSERT_THAT(IsTrue(
			Fixture.Source->ReleaseSnapshotRestoreAuthority(
				MoveTemp(OccupiedOutput), ReleaseError)));

		FSeinSnapshotRestoreAuthorityHandle First;
		FSeinSnapshotRestoreAuthorityHandle Retry;
		ASSERT_THAT(IsTrue(Fixture.Claim(
			*Fixture.Destination, Fixture.Destination, First)));
		ASSERT_THAT(IsTrue(Fixture.Claim(
			*Fixture.Destination, Fixture.Destination, Retry)));
		ASSERT_THAT(IsTrue(First.IsValid()));
		ASSERT_THAT(IsTrue(Retry.IsValid()));

		FSeinSnapshotRestoreAuthorityHandle Competitor;
		FString ClaimError;
		ASSERT_THAT(IsFalse(
			Fixture.Destination->ClaimSnapshotRestoreAuthority(
				FName(TEXT("SeinARTSFrameworkTests.CompetingRestoreOwner")),
				&Fixture.DestinationSpawner.GetWorld(),
				Competitor,
				ClaimError)));
		ASSERT_THAT(IsFalse(Competitor.IsValid()));
		ASSERT_THAT(IsTrue(ClaimError.Contains(
			TEXT("already claimed"))));

		ASSERT_THAT(IsTrue(
			Fixture.Destination->ReleaseSnapshotRestoreAuthority(
				MoveTemp(Retry), ReleaseError)));
		ASSERT_THAT(IsFalse(Retry.IsValid()));

		// Same-owner retries receive the exact world token. Releasing either
		// copy therefore makes every other copy stale.
		Assert.ExpectError(TEXT(
			"RestoreSnapshot: rejected without this world's exact trusted-envelope authority."));
		ASSERT_THAT(IsFalse(Fixture.Destination->RestoreSnapshot(
			MoveTemp(First), Fixture.Snapshot, PreserveAndStop)));
		ASSERT_THAT(IsTrue(Pristine.Matches(*Fixture.Destination)));
	}

	TEST(SnapshotRestoreAuthorityOwnerExpiryPermitsANewClaim,
		"SeinARTS.Unit.Authority.SnapshotRestore")
	{
		FSnapshotRestoreAuthorityFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.IsReady()));

		AActor& EphemeralOwner =
			Fixture.DestinationSpawner.SpawnActor<AActor>();
		FSeinSnapshotRestoreAuthorityHandle Orphaned;
		ASSERT_THAT(IsTrue(Fixture.Claim(
			*Fixture.Destination, &EphemeralOwner, Orphaned)));
		ASSERT_THAT(IsTrue(EphemeralOwner.Destroy()));

		FSeinSnapshotRestoreAuthorityHandle Replacement;
		ASSERT_THAT(IsTrue(Fixture.Claim(
			*Fixture.Destination,
			Fixture.Destination,
			Replacement)));
		FString ReleaseError;
		ASSERT_THAT(IsTrue(
			Fixture.Destination->ReleaseSnapshotRestoreAuthority(
				MoveTemp(Replacement), ReleaseError)));
	}

	TEST(SnapshotRestoreAuthorityClaimAndReleaseAreNonCanonical,
		"SeinARTS.Unit.Authority.SnapshotRestore")
	{
		FSnapshotRestoreAuthorityFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.IsReady()));
		ASSERT_THAT(IsTrue(Fixture.Source->StartSimulation()));

		FGuid RootBefore;
		FGuid RootAfter;
		FString RootError;
		ASSERT_THAT(IsTrue(
			Fixture.Source->ComputeCanonicalStateRoot(
				RootBefore, RootError)));
		FSeinSnapshotRestoreAuthorityHandle Authority;
		ASSERT_THAT(IsTrue(Fixture.Claim(
			*Fixture.Source, Fixture.Source, Authority)));
		FString ReleaseError;
		ASSERT_THAT(IsTrue(
			Fixture.Source->ReleaseSnapshotRestoreAuthority(
				MoveTemp(Authority), ReleaseError)));
		ASSERT_THAT(IsTrue(
			Fixture.Source->ComputeCanonicalStateRoot(
				RootAfter, RootError)));
		ASSERT_THAT(IsTrue(RootBefore == RootAfter));
	}

	TEST(SnapshotRestorePreserveCurrentSkipsLocalButFiresAuthoritativeReconcile,
		"SeinARTS.Unit.Authority.SnapshotRestore")
	{
		FSnapshotRestoreAuthorityFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.IsReady()));

		int32 RestoreDelegateCalls = 0;
		int32 AuthoritativeReconcileCalls = 0;
		const FDelegateHandle RestoreHandle =
			Fixture.Destination->OnRestoreSnapshotPostSim.AddLambda(
				[&RestoreDelegateCalls](const FSeinCameraSnapshotData&)
				{
					++RestoreDelegateCalls;
				});
		const FDelegateHandle ReconcileHandle =
			Fixture.Destination->OnAuthoritativeStateRestored.AddLambda(
				[&AuthoritativeReconcileCalls]()
				{
					++AuthoritativeReconcileCalls;
				});

		FSeinSnapshotRestoreAuthorityHandle Authority;
		ASSERT_THAT(IsTrue(Fixture.Claim(
			*Fixture.Destination, Fixture.Destination, Authority)));
		const FSeinSnapshotRestoreOptions Options(
			ESeinSnapshotLocalStateRestorePolicy::PreserveCurrent,
			ESeinSnapshotResumePolicy::ResumeImmediately);
		ASSERT_THAT(IsTrue(Fixture.Destination->RestoreSnapshot(
			MoveTemp(Authority), Fixture.Snapshot, Options)));
		ASSERT_THAT(IsFalse(Authority.IsValid()));
		ASSERT_THAT(AreEqual(0, RestoreDelegateCalls));
		ASSERT_THAT(AreEqual(1, AuthoritativeReconcileCalls));
		ASSERT_THAT(IsTrue(
			Fixture.Destination->IsSimulationRunning()));

		Fixture.Destination->OnRestoreSnapshotPostSim.Remove(
			RestoreHandle);
		Fixture.Destination->OnAuthoritativeStateRestored.Remove(
			ReconcileHandle);
	}

	TEST(SnapshotRestoreCanRemainStoppedThenResumeWithExactPendingCommands,
		"SeinARTS.Unit.Authority.SnapshotRestore")
	{
		FSnapshotRestoreAuthorityFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.IsReady()));
		ASSERT_THAT(AreEqual(3, Fixture.Snapshot.PendingCommands.Num()));
		ASSERT_THAT(IsTrue(
			Fixture.Snapshot.PendingCommands[0].IssuerKind
				== ESeinCommandIssuerKind::Player));
		ASSERT_THAT(IsTrue(
			Fixture.Snapshot.PendingCommands[1].IssuerKind
				== ESeinCommandIssuerKind::MatchAdministrator));
		ASSERT_THAT(IsTrue(
			Fixture.Snapshot.PendingCommands[2].IssuerKind
				== ESeinCommandIssuerKind::DeterministicSystem));
		ASSERT_THAT(IsTrue(
			Fixture.Snapshot.PendingCommands[2].DerivedResourcePayer
				== FSeinPlayerID(1)));

		FSeinSnapshotRestoreAuthorityHandle Authority;
		ASSERT_THAT(IsTrue(Fixture.Claim(
			*Fixture.Destination, Fixture.Destination, Authority)));
		ASSERT_THAT(IsTrue(Fixture.Destination->RestoreSnapshot(
			MoveTemp(Authority), Fixture.Snapshot, PreserveAndStop)));

		ASSERT_THAT(IsFalse(
			Fixture.Destination->IsSimulationRunning()));
		ASSERT_THAT(AreEqual(
			Fixture.Snapshot.CurrentTick,
			Fixture.Destination->GetCurrentTick()));
		ASSERT_THAT(AreEqual(
			3, Fixture.Destination->GetPendingCommands().Num()));
		const TArray<FSeinCommand>& RestoredCommands =
			Fixture.Destination->GetPendingCommands().GetCommands();
		for (int32 Index = 0;
			Index < Fixture.Snapshot.PendingCommands.Num(); ++Index)
		{
			const FSeinCommand& Captured =
				Fixture.Snapshot.PendingCommands[Index];
			const FSeinCommand& Restored = RestoredCommands[Index];
			ASSERT_THAT(AreEqual(Captured.Tick, Restored.Tick));
			ASSERT_THAT(IsTrue(
				Captured.PlayerID == Restored.PlayerID));
			ASSERT_THAT(IsTrue(
				Captured.IssuerKind == Restored.IssuerKind));
			ASSERT_THAT(IsTrue(
				Captured.DerivedResourcePayer
					== Restored.DerivedResourcePayer));
			ASSERT_THAT(IsTrue(
				FSeinCommand::StaticStruct()->CompareScriptStruct(
					&Captured, &Restored, PPF_None)));
		}

		// Exercise the registered callback while the adopted timeline is
		// deliberately dormant. It must remain registered for a later
		// reconnect/catch-up activation without advancing canonical time.
		FTSTicker::GetCoreTicker().Tick(
			Fixture.Destination->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsFalse(
			Fixture.Destination->IsSimulationRunning()));
		ASSERT_THAT(AreEqual(
			Fixture.Snapshot.CurrentTick,
			Fixture.Destination->GetCurrentTick()));
		FGuid DormantRoot;
		FString RootError;
		ASSERT_THAT(IsTrue(
			Fixture.Destination->ComputeCanonicalStateRoot(
				DormantRoot, RootError)));

		ASSERT_THAT(IsTrue(Fixture.Destination->StartSimulation()));
		ASSERT_THAT(IsTrue(
			Fixture.Destination->IsSimulationRunning()));
		ASSERT_THAT(AreEqual(
			3, Fixture.Destination->GetPendingCommands().Num()));
		FGuid ActivatedRoot;
		ASSERT_THAT(IsTrue(
			Fixture.Destination->ComputeCanonicalStateRoot(
				ActivatedRoot, RootError)));
		ASSERT_THAT(IsTrue(DormantRoot == ActivatedRoot));
		FTSTicker::GetCoreTicker().Tick(
			Fixture.Destination->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsTrue(
			Fixture.Destination->GetCurrentTick()
				> Fixture.Snapshot.CurrentTick));
	}

	TEST(SnapshotRemainStoppedReservationCanBeExplicitlyAbandoned,
		"SeinARTS.Unit.Authority.SnapshotRestore")
	{
		FSnapshotRestoreAuthorityFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.IsReady()));

		FSeinSnapshotRestoreAuthorityHandle Authority;
		ASSERT_THAT(IsTrue(Fixture.Claim(
			*Fixture.Destination, Fixture.Destination, Authority)));
		ASSERT_THAT(IsTrue(Fixture.Destination->RestoreSnapshot(
			MoveTemp(Authority), Fixture.Snapshot, PreserveAndStop)));
		FTSTicker::GetCoreTicker().Tick(
			Fixture.Destination->GetFixedDeltaTimeSeconds());

		// Explicit stop abandons the dormant readiness reservation. The world
		// remains a valid consumed checkpoint and may later acquire a fresh
		// scheduler if the coordinator elects to continue after all.
		Fixture.Destination->StopSimulation();
		ASSERT_THAT(IsFalse(
			Fixture.Destination->IsSimulationRunning()));
		ASSERT_THAT(IsTrue(Fixture.Destination->StartSimulation()));
		FTSTicker::GetCoreTicker().Tick(
			Fixture.Destination->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsTrue(
			Fixture.Destination->GetCurrentTick()
				> Fixture.Snapshot.CurrentTick));
	}
}

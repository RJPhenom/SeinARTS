#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Containers/Ticker.h"
#include "Input/SeinCommand.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "TestTypes/SeinAIControllerTestTypes.h"

struct FSeinCommandIngressTestAccess
{
	static void EnqueueAuthenticated(
		USeinWorldSubsystem& World,
		const FSeinCommand& Command)
	{
		World.EnqueueAuthenticatedCommand(
			Command,
			Command.PlayerID,
			ESeinCommandIssuerKind::Player);
	}

	static void EnqueueReplay(
		USeinWorldSubsystem& World,
		const FSeinCommand& Command)
	{
		World.EnqueueCommand(Command);
	}

	static bool BeginReplay(USeinWorldSubsystem& World)
	{
		FString Error;
		return World.BeginReplayExclusiveCommandIngress(Error);
	}

	static void EndReplay(USeinWorldSubsystem& World)
	{
		World.EndReplayExclusiveCommandIngress();
	}

	static bool RouteAsActiveAI(
		USeinWorldSubsystem& World,
		USeinAIController* Controller,
		const FSeinCommand& Command)
	{
		TGuardValue<USeinAIController*> ActiveEmitterGuard(
			World.ActiveAICommandEmitter, Controller);
		return World.RouteAICommandFromController(Controller, Command);
	}
};

namespace UE::SeinARTSTests
{
	TEST(AICommandIngressRequiresExactActiveControllerTick,
		"SeinARTS.Unit.Authority.AICommandIngress")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID FirstPlayer(1);
		const FSeinPlayerID SecondPlayer(2);
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(FirstPlayer, FSeinFactionID(1));
			World->RegisterPlayer(SecondPlayer, FSeinFactionID(2));
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0,
			TEXT("SeinARTS.AICommandIngress.ExactEmitter"))));

		USeinAIControllerEmissionProbe* First =
			NewObject<USeinAIControllerEmissionProbe>(World);
		USeinAIControllerEmissionProbe* Second =
			NewObject<USeinAIControllerEmissionProbe>(World);
		ASSERT_THAT(IsNotNull(First));
		ASSERT_THAT(IsNotNull(Second));
		World->RegisterAIController(First, FirstPlayer);
		World->RegisterAIController(Second, SecondPlayer);

		First->Command = FSeinCommand::MakePingCommand(
			FirstPlayer, FFixedVector());
		First->ForeignController = Second;
		First->bEmitForeignCommand = true;

		int32 InterceptedCount = 0;
		FSeinPlayerID InterceptedSlot;
		FSeinAIEmitInterceptor Interceptor;
		Interceptor.BindLambda(
			[&](FSeinPlayerID Slot, const FSeinCommand&)
			{
				++InterceptedCount;
				InterceptedSlot = Slot;
				return false;
			});
		World->SetAIEmitInterceptor(MoveTemp(Interceptor));

		TestRunner->AddExpectedError(
			TEXT("is not the exact active registered AI tick callback"),
			EAutomationExpectedErrorFlags::Contains, 2, false);
		TestRunner->AddExpectedError(
			TEXT("active topology adapter declined AI slot"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("topology adapter is active without an AI authority hook"),
			EAutomationExpectedErrorFlags::Contains, 1, false);

		// Registration alone is not emission authority.
		First->EmitCommand(First->Command);
		ASSERT_THAT(AreEqual(0, InterceptedCount));

		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(AreEqual(0, InterceptedCount));

		// The exact active controller reaches the topology adapter.
		First->bEmitForeignCommand = false;
		First->bEmitOwnCommand = true;
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(AreEqual(1, InterceptedCount));
		ASSERT_THAT(IsTrue(InterceptedSlot == FirstPlayer));
		ASSERT_THAT(AreEqual(0, World->GetPendingCommands().Num()));

		// A topology with an ordinary-local hook but no AI hook fails closed.
		World->ClearAIEmitInterceptor();
		bool bLocalSubmitterCalled = false;
		FSeinLocalCommandSubmitter LocalSubmitter;
		LocalSubmitter.BindLambda(
			[&](const FSeinCommand&, bool)
			{
				bLocalSubmitterCalled = true;
				return true;
			});
		World->SetLocalCommandSubmitter(MoveTemp(LocalSubmitter));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsFalse(bLocalSubmitterCalled));

		// Unbound topology hooks are the explicit standalone path.
		World->ClearLocalCommandSubmitter();
		TestRunner->AddExpectedError(
			TEXT("while replay owns external ingress"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsTrue(
			FSeinCommandIngressTestAccess::BeginReplay(*World)));
		ASSERT_THAT(IsFalse(
			FSeinCommandIngressTestAccess::RouteAsActiveAI(
				*World, First, First->Command)));
		FSeinCommandIngressTestAccess::EndReplay(*World);
		ASSERT_THAT(AreEqual(0, World->GetPendingCommands().Num()));

		int32 ObservedStandaloneCommands = 0;
		FSeinPlayerID ObservedStandalonePlayer;
		World->OnCommandsProcessing.AddLambda(
			[&](int32, const TArray<FSeinCommand>& Commands)
			{
				ObservedStandaloneCommands += Commands.Num();
				if (!Commands.IsEmpty())
				{
					ObservedStandalonePlayer = Commands[0].PlayerID;
				}
			});
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(AreEqual(1, ObservedStandaloneCommands));
		ASSERT_THAT(IsTrue(ObservedStandalonePlayer == FirstPlayer));
		ASSERT_THAT(AreEqual(4, First->TickCount));
		World->StopSimulation();
	}

	TEST(LocalDraftLifecycleAndNotificationsFailClosed,
		"SeinARTS.Unit.Authority.CommandIngress")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		const FSeinCommand Command = FSeinCommand::MakePingCommand(
			Player, FFixedVector());
		TestRunner->AddExpectedError(
			TEXT("ordinary ingress requires a launched, running match"),
			EAutomationExpectedErrorFlags::Contains, 4, false);
		TestRunner->AddExpectedError(
			TEXT("Rejected local command draft 'SeinARTS.Command.Type.Ping' from a read-only observer"),
			EAutomationExpectedErrorFlags::Contains, 2, false);
		TestRunner->AddExpectedError(
			TEXT("Rejected deterministic-system command"),
			EAutomationExpectedErrorFlags::Contains, 2, false);
		TestRunner->AddExpectedError(
			TEXT("transport-authenticated command"),
			EAutomationExpectedErrorFlags::Contains, 5, false);
		TestRunner->AddExpectedError(
			TEXT("replay command"),
			EAutomationExpectedErrorFlags::Contains, 5, false);
		TestRunner->AddExpectedError(
			TEXT("SpawnAbstractEntity rejected outside bootstrap Applying or deterministic simulation context"),
			EAutomationExpectedErrorFlags::Contains, 2, false);

		const auto ProbeClosedExternalIngress = [&]()
		{
			FSeinCommand Canonical = Command;
			Canonical.IssuerKind = ESeinCommandIssuerKind::Player;
			FSeinCommandIngressTestAccess::EnqueueAuthenticated(
				*World, Canonical);
			const bool bReplayAcquired =
				FSeinCommandIngressTestAccess::BeginReplay(*World);
			if (bReplayAcquired)
			{
				FSeinCommandIngressTestAccess::EnqueueReplay(
					*World, Canonical);
				FSeinCommandIngressTestAccess::EndReplay(*World);
			}
			return bReplayAcquired;
		};

		World->SubmitLocalCommandDraft(Command); // Awaiting
		ASSERT_THAT(IsTrue(ProbeClosedExternalIngress()));
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0,
			TEXT("SeinARTS.CommandIngress.Lifecycle"))));
		World->SubmitLocalCommandDraft(Command); // LocallyReady
		ASSERT_THAT(IsTrue(ProbeClosedExternalIngress()));

		FSeinMatchBootstrapAuthorityHandle BootstrapAuthority;
		FString BootstrapAuthorityError;
		ASSERT_THAT(IsTrue(World->ClaimMatchBootstrapAuthority(
			FName(TEXT("SeinARTSTestSupport")),
			World,
			BootstrapAuthority,
			BootstrapAuthorityError)));
		int32 BootstrapNotifications = 0;
		bool bBootstrapAuthorized = false;
		bool bReentrantFailureAccepted = true;
		bool bReentrantLaunchAccepted = true;
		World->OnMatchBootstrapClosed.AddLambda(
			[&](bool bAuthorized)
			{
				++BootstrapNotifications;
				bBootstrapAuthorized = bAuthorized;
				World->SpawnAbstractEntity(FFixedTransform(), Player);
				World->SubmitLocalCommandDraft(Command);
				World->EnqueueDerivedCommand(Command);
				FString ReentrantError;
				bReentrantFailureAccepted = World->FailMatchBootstrap(
					BootstrapAuthority,
					TEXT("Reentrant observer failure."),
					ReentrantError);
				bReentrantLaunchAccepted =
					World->LaunchAuthorizedMatchBootstrap(
						BootstrapAuthority,
						ReentrantError);
			});
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Authorize(*World)));
		World->SubmitLocalCommandDraft(Command); // Authorized
		ASSERT_THAT(IsTrue(ProbeClosedExternalIngress()));
		ASSERT_THAT(AreEqual(1, BootstrapNotifications));
		ASSERT_THAT(IsTrue(bBootstrapAuthorized));
		ASSERT_THAT(IsFalse(bReentrantFailureAccepted));
		ASSERT_THAT(IsFalse(bReentrantLaunchAccepted));
		ASSERT_THAT(IsTrue(World->GetMatchBootstrapState()
			== ESeinMatchBootstrapState::Authorized));

		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		int32 TickNotifications = 0;
		const FDelegateHandle TickNotificationHandle =
			World->OnSimTickCompleted.AddLambda(
			[&](int32)
			{
				++TickNotifications;
				World->SpawnAbstractEntity(FFixedTransform(), Player);
				World->SubmitLocalCommandDraft(Command);
				World->EnqueueDerivedCommand(Command);
				FSeinCommand Canonical = Command;
				Canonical.IssuerKind = ESeinCommandIssuerKind::Player;
				FSeinCommandIngressTestAccess::EnqueueAuthenticated(
					*World, Canonical);
				FSeinCommandIngressTestAccess::EnqueueReplay(
					*World, Canonical);
			});
		FSeinCommand Canonical = Command;
		Canonical.IssuerKind = ESeinCommandIssuerKind::Player;
		FSeinCommandIngressTestAccess::EnqueueAuthenticated(
			*World, Canonical);
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		World->OnSimTickCompleted.Remove(TickNotificationHandle);
		ASSERT_THAT(AreEqual(1, TickNotifications));
		ASSERT_THAT(AreEqual(0, World->GetPendingCommands().Num()));

		ASSERT_THAT(IsTrue(
			FSeinCommandIngressTestAccess::BeginReplay(*World)));
		FSeinCommandIngressTestAccess::EnqueueReplay(*World, Canonical);
		ASSERT_THAT(AreEqual(
			1, World->GetPendingReplayCommandCountForTests()));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(AreEqual(
			0, World->GetPendingReplayCommandCountForTests()));
		FSeinCommandIngressTestAccess::EndReplay(*World);

		World->StopSimulation();
		World->SubmitLocalCommandDraft(Command); // Consumed but stopped
		ASSERT_THAT(IsTrue(ProbeClosedExternalIngress()));
	}

	TEST(FailedBootstrapNotificationIsReadOnly,
		"SeinARTS.Unit.Authority.CommandIngress")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FSeinMatchBootstrapAuthorityHandle Authority;
		FString Error;
		ASSERT_THAT(IsTrue(World->ClaimMatchBootstrapAuthority(
			FName(TEXT("SeinARTS.Tests.FailedBootstrapObserver")),
			World,
			Authority,
			Error)));

		const FSeinPlayerID Player(1);
		const FSeinCommand Command = FSeinCommand::MakePingCommand(
			Player, FFixedVector());
		bool bObservedFailure = false;
		World->OnMatchBootstrapClosed.AddLambda(
			[&](bool bAuthorized)
			{
				bObservedFailure = !bAuthorized;
				World->SpawnAbstractEntity(FFixedTransform(), Player);
				World->SubmitLocalCommandDraft(Command);
			});
		TestRunner->AddExpectedError(
			TEXT("Match bootstrap failed closed"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("transaction closed (failed)"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("SpawnAbstractEntity rejected outside bootstrap Applying or deterministic simulation context"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("from a read-only observer"),
			EAutomationExpectedErrorFlags::Contains, 1, false);

		ASSERT_THAT(IsTrue(World->FailMatchBootstrap(
			Authority,
			TEXT("Intentional observer test failure."),
			Error)));
		ASSERT_THAT(IsTrue(bObservedFailure));
		ASSERT_THAT(IsTrue(World->GetMatchBootstrapState()
			== ESeinMatchBootstrapState::Failed));
	}
}

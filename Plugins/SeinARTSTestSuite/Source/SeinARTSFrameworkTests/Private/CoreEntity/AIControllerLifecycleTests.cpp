#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Containers/Ticker.h"
#include "Input/SeinCommand.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "TestTypes/SeinAIControllerTestTypes.h"

void USeinAIControllerLifecycleProbe::OnUnregistered_Implementation()
{
	++UnregisteredCount;
	LastUnregisteredSequence = ++CallbackSequence;
	LastUnregisteredPlayer = OwnedPlayerID;
	bUnregisteredWithWorld = WorldSubsystem != nullptr;
	if (bAttemptReregisterOnUnregister && WorldSubsystem)
	{
		WorldSubsystem->RegisterAIController(this, OwnedPlayerID);
	}
}

void USeinAIControllerMutationProbe::Tick_Implementation(
	const FSeinAITickContext&)
{
	++TickCount;
	DirectMutationResult = WorldSubsystem->SpawnAbstractEntity(
		FFixedTransform(), OwnedPlayerID);
	EmitCommand(FSeinCommand::MakeCancelCommand(
		OwnedPlayerID, CommandEntity));
}

namespace UE::SeinARTSTests
{
	TEST(AIControllerRegistrationBalancesLifecycle, "SeinARTS.Unit.Entity.AI")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		USeinAIControllerLifecycleProbe* Controller =
			NewObject<USeinAIControllerLifecycleProbe>(World);
		ASSERT_THAT(IsNotNull(Controller));

		const FSeinPlayerID FirstPlayer(1);
		const FSeinPlayerID SecondPlayer(2);
		World->RegisterAIController(Controller, FirstPlayer);
		ASSERT_THAT(AreEqual(1, Controller->RegisteredCount));
		ASSERT_THAT(AreEqual(0, Controller->UnregisteredCount));
		ASSERT_THAT(AreEqual(1, Controller->LastRegisteredSequence));
		ASSERT_THAT(IsTrue(Controller->LastRegisteredPlayer == FirstPlayer));
		ASSERT_THAT(IsTrue(Controller->bRegisteredWithWorld));
		ASSERT_THAT(IsTrue(Controller->WorldSubsystem == World));
		ASSERT_THAT(IsTrue(World->GetAIControllerForPlayer(FirstPlayer) == Controller));
		ASSERT_THAT(AreEqual(1, World->GetAIControllers().Num()));

		// An identical registration is a true no-op, including lifecycle hooks.
		World->RegisterAIController(Controller, FirstPlayer);
		ASSERT_THAT(AreEqual(1, Controller->RegisteredCount));
		ASSERT_THAT(AreEqual(0, Controller->UnregisteredCount));
		ASSERT_THAT(AreEqual(1, Controller->CallbackSequence));
		ASSERT_THAT(AreEqual(1, World->GetAIControllers().Num()));

		// Rebinding exposes the old context during teardown, then the new context
		// during registration, without duplicating the tick-list entry.
		World->RegisterAIController(Controller, SecondPlayer);
		ASSERT_THAT(AreEqual(2, Controller->RegisteredCount));
		ASSERT_THAT(AreEqual(1, Controller->UnregisteredCount));
		ASSERT_THAT(AreEqual(2, Controller->LastUnregisteredSequence));
		ASSERT_THAT(AreEqual(3, Controller->LastRegisteredSequence));
		ASSERT_THAT(IsTrue(Controller->LastUnregisteredPlayer == FirstPlayer));
		ASSERT_THAT(IsTrue(Controller->LastRegisteredPlayer == SecondPlayer));
		ASSERT_THAT(IsTrue(Controller->bUnregisteredWithWorld));
		ASSERT_THAT(IsTrue(Controller->WorldSubsystem == World));
		ASSERT_THAT(IsNull(World->GetAIControllerForPlayer(FirstPlayer)));
		ASSERT_THAT(IsTrue(World->GetAIControllerForPlayer(SecondPlayer) == Controller));
		ASSERT_THAT(AreEqual(1, World->GetAIControllers().Num()));

		World->UnregisterAIController(Controller);
		ASSERT_THAT(AreEqual(2, Controller->RegisteredCount));
		ASSERT_THAT(AreEqual(2, Controller->UnregisteredCount));
		ASSERT_THAT(AreEqual(4, Controller->LastUnregisteredSequence));
		ASSERT_THAT(IsTrue(Controller->LastUnregisteredPlayer == SecondPlayer));
		ASSERT_THAT(IsNull(Controller->WorldSubsystem.Get()));
		ASSERT_THAT(AreEqual(0, World->GetAIControllers().Num()));

		// Teardown remains safe to repeat.
		World->UnregisterAIController(Controller);
		ASSERT_THAT(AreEqual(2, Controller->UnregisteredCount));
		ASSERT_THAT(AreEqual(4, Controller->CallbackSequence));
	}

	TEST(AIControllerTickCanEmitButCannotMutateStateDirectly,
		"SeinARTS.Unit.Entity.AI")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID AIPlayer(1);
		FSeinEntityHandle CommandEntity;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(AIPlayer, FSeinFactionID(1));
			CommandEntity = World->SpawnAbstractEntity(
				FFixedTransform(), AIPlayer);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		USeinAIControllerMutationProbe* Controller =
			NewObject<USeinAIControllerMutationProbe>(World);
		ASSERT_THAT(IsNotNull(Controller));
		Controller->CommandEntity = CommandEntity;
		World->RegisterAIController(Controller, AIPlayer);

		int32 ObservedCommandCount = 0;
		World->OnCommandsProcessing.AddLambda(
			[World, CommandEntity, AIPlayer, &ObservedCommandCount](
				int32, const TArray<FSeinCommand>& Commands)
			{
				ObservedCommandCount += Commands.Num();
				const FSeinCommand ObserverDraft =
					FSeinCommand::MakeCancelCommand(AIPlayer, CommandEntity);
				World->SubmitLocalCommandDraft(ObserverDraft);
				World->EnqueueDerivedCommand(ObserverDraft);
			});
		TestRunner->AddExpectedError(
			TEXT("SpawnAbstractEntity rejected outside bootstrap Applying or deterministic simulation context"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("Rejected local command draft"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("Rejected deterministic-system command"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		World->UnregisterAIController(Controller);
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());

		ASSERT_THAT(AreEqual(1, Controller->TickCount));
		ASSERT_THAT(IsFalse(Controller->DirectMutationResult.IsValid()));
		ASSERT_THAT(AreEqual(1, ObservedCommandCount));
		World->StopSimulation();
	}
}

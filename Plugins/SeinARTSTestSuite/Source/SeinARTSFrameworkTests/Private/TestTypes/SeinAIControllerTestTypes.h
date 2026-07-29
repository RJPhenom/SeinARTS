#pragma once

#include "CoreMinimal.h"
#include "AI/SeinAIController.h"
#include "Input/SeinCommand.h"

#include "SeinAIControllerTestTypes.generated.h"

/** Records registration callbacks and the context visible inside each one. */
UCLASS()
class USeinAIControllerLifecycleProbe : public USeinAIController
{
	GENERATED_BODY()

public:
	int32 RegisteredCount = 0;
	int32 UnregisteredCount = 0;
	int32 CallbackSequence = 0;
	int32 LastRegisteredSequence = 0;
	int32 LastUnregisteredSequence = 0;
	FSeinPlayerID LastRegisteredPlayer;
	FSeinPlayerID LastUnregisteredPlayer;
	bool bRegisteredWithWorld = false;
	bool bUnregisteredWithWorld = false;
	bool bAttemptReregisterOnUnregister = false;

	virtual void OnRegistered_Implementation() override
	{
		++RegisteredCount;
		LastRegisteredSequence = ++CallbackSequence;
		LastRegisteredPlayer = OwnedPlayerID;
		bRegisteredWithWorld = WorldSubsystem != nullptr;
	}

	virtual void OnUnregistered_Implementation() override;
};

/** Host-AI tick probe: direct state writes must fail, command emission remains. */
UCLASS()
class USeinAIControllerMutationProbe : public USeinAIController
{
	GENERATED_BODY()

public:
	FSeinEntityHandle CommandEntity;
	FSeinEntityHandle DirectMutationResult;
	int32 TickCount = 0;

	virtual void Tick_Implementation(const FSeinAITickContext& Context) override;
};

/** Emits either as itself or through another registered controller. */
UCLASS()
class USeinAIControllerEmissionProbe : public USeinAIController
{
	GENERATED_BODY()

public:
	FSeinCommand Command;

	UPROPERTY(Transient)
	TObjectPtr<USeinAIController> ForeignController;

	bool bEmitOwnCommand = false;
	bool bEmitForeignCommand = false;
	int32 TickCount = 0;

	virtual void Tick_Implementation(const FSeinAITickContext&) override
	{
		++TickCount;
		if (bEmitOwnCommand)
		{
			EmitCommand(Command);
		}
		if (bEmitForeignCommand && ForeignController)
		{
			ForeignController->EmitCommand(Command);
		}
	}
};

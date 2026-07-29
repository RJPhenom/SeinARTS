#pragma once

#include "CoreMinimal.h"
#include "AI/SeinAIController.h"
#include "Input/SeinCommand.h"

#include "SeinReplayTestTypes.generated.h"

/** Probe that would emit every sim tick if replay playback failed to suppress AI. */
UCLASS()
class USeinReplayEmittingAIController : public USeinAIController
{
	GENERATED_BODY()

public:
	int32 TickCount = 0;

	virtual void Tick_Implementation(const FSeinAITickContext& Context) override
	{
		++TickCount;
		EmitCommand(FSeinCommand::MakePingCommand(
			Context.OwnedPlayerID, FFixedVector()));
	}
};

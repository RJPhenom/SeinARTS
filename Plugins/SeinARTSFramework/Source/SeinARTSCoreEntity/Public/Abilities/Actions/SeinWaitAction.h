#pragma once

#include "CoreMinimal.h"
#include "Abilities/SeinLatentAction.h"
#include "SeinWaitAction.generated.h"

/** Exact future-affecting state used by the built-in wait continuation codec. */
USTRUCT(meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinWaitActionCanonicalState
{
	GENERATED_BODY()

	UPROPERTY()
	FFixedPoint Duration;

	UPROPERTY()
	FFixedPoint Elapsed;
};

/**
 * Latent action that waits for a specified number of sim seconds.
 * Use this for delays, cooldown periods, or timed phases within abilities.
 */
UCLASS()
class SEINARTSCOREENTITY_API USeinWaitAction : public USeinLatentAction
{
	GENERATED_BODY()

public:
	/** Total duration to wait (sim seconds) */
	FFixedPoint Duration;

	/** Time elapsed so far */
	FFixedPoint Elapsed;

	/** Initialize the wait with a target duration */
	void Initialize(FFixedPoint InDuration);

	virtual bool TickAction(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override;
};

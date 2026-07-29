#pragma once

#include "Abilities/SeinAbility.h"
#include "Abilities/SeinLatentAction.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "SeinSnapshotValidationTestTypes.generated.h"

class USeinWorldSubsystem;

/** Ordinary serializable ability used by snapshot-pool validation tests. */
UCLASS()
class USeinSnapshotTestAbility : public USeinAbility
{
	GENERATED_BODY()
};

/** Passive used to validate the component index's passive role across restore. */
UCLASS()
class USeinSnapshotPassiveTestAbility : public USeinAbility
{
	GENERATED_BODY()

public:
	USeinSnapshotPassiveTestAbility();
};

/** Valid pluggable ability whose production-compatible outer is required. */
UCLASS(Within = SeinWorldSubsystem)
class USeinSnapshotWithinTestAbility : public USeinAbility
{
	GENERATED_BODY()
};

/** Valid pluggable resolver whose production-compatible outer is required. */
UCLASS(Within = SeinWorldSubsystem)
class USeinSnapshotWithinTestResolver : public USeinCommandBrokerResolver
{
	GENERATED_BODY()
};

/** Invalid replacement class used to prove deprecated abilities are rejected. */
UCLASS(Deprecated)
class UDEPRECATED_SeinSnapshotObsoleteTestAbility : public USeinAbility
{
	GENERATED_BODY()
};

/** Invalid replacement class used to prove deprecated resolvers are rejected. */
UCLASS(Deprecated)
class UDEPRECATED_SeinSnapshotObsoleteTestResolver : public USeinCommandBrokerResolver
{
	GENERATED_BODY()
};

/** Canonical payload for the fake third-party continuation codec. */
USTRUCT(meta = (SeinDeterministic))
struct FSeinSnapshotThirdPartyLatentState
{
	GENERATED_BODY()

	UPROPERTY()
	int32 RemainingTicks = 0;

	UPROPERTY()
	int32 TicksExecuted = 0;
};

/** Required native contributor payload consumed during latent staging. */
USTRUCT(meta = (SeinDeterministic))
struct FSeinSnapshotThirdPartyNativeState
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Marker = 0;
};

/** Exact non-Core action class used to exercise the public codec registry. */
UCLASS()
class USeinSnapshotThirdPartyLatentAction : public USeinLatentAction
{
	GENERATED_BODY()

public:
	int32 RemainingTicks = 0;
	int32 TicksExecuted = 0;
	int32 TimelineAbandonCount = 0;

	virtual bool TickAction(
		FFixedPoint DeltaTime,
		USeinWorldSubsystem& World) override;
	virtual void OnTimelineAbandoned() override;
};

#pragma once

#include "Abilities/SeinAbility.h"
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

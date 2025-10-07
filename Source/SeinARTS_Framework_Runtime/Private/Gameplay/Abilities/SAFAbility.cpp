#include "Gameplay/Abilities/SAFAbility.h"

// Constructor sets defaults on replication policies for SAFAbility GAS Blueprints
USAFAbility::USAFAbility(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
  ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateYes;
  InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
  NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
  NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnly;
}
#include "Gameplay/Abilities/SAFAbility.h"
#include "Classes/SAFPlayerState.h"
#include "AbilitySystemComponent.h"
#include "Debug/SAFDebugTool.h"

// Constructor sets defaults on replication policies for SAFAbility GAS Blueprints
USAFAbility::USAFAbility(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
	ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateYes;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnly;
}

bool USAFAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, 
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags
) const {
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)) return false;
	if (!bRequiresPlayerResources) return true;

	// Check player resource availability
	ASAFPlayerState* PlayerState = GetOwningPlayerState();
	if (!PlayerState) { SAFDEBUG_WARNING(FORMATSTR("SAFAbility '%s' cannot activate: no PlayerState found", *GetName())); return false; }

	FSAFResourceBundle RequiredCost = GetEffectivePlayerResourceCost();
	bool bCanAfford = PlayerState->CheckResourcesAvailable(RequiredCost);
	
	if (!bCanAfford) SAFDEBUG_INFO(FORMATSTR(
		"SAFAbility '%s' cannot activate: insufficient player resources. Required: %s", 
		*GetName(), *RequiredCost.ToString()
	));

	return bCanAfford;
}

void USAFAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, 
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData
) {
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// Handle player resource deduction if required
	if (bRequiresPlayerResources) {
		ASAFPlayerState* PlayerState = GetOwningPlayerState();
		if (!PlayerState) {
			SAFDEBUG_ERROR(FORMATSTR("SAFAbility '%s' failed: no PlayerState found during activation", *GetName()));
			CancelAbility(Handle, ActorInfo, ActivationInfo, true);
			return;
		}

		FSAFResourceBundle RequiredCost = GetEffectivePlayerResourceCost();
		if (PlayerState->RequestResources(RequiredCost)) {
			// Player resources successfully deducted
			SAFDEBUG_SUCCESS(FORMATSTR("SAFAbility '%s' paid player resource cost: %s", *GetName(), *RequiredCost.ToString()));
			ReceivePlayerResourceCostPaid();
		} else {
			// Failed to deduct player resources (race condition or other issue)
			FSAFResourceBundle CurrentResources = PlayerState->GetResources();
			SAFDEBUG_WARNING(FORMATSTR(
				"SAFAbility '%s' failed to pay player resource cost during activation. Required: %s, Available: %s", 
				*GetName(), *RequiredCost.ToString(), *CurrentResources.ToString()
			));
			
			ReceiveInsufficientPlayerResources(RequiredCost, CurrentResources);
			CancelAbility(Handle, ActorInfo, ActivationInfo, true);
			return;
		}
	} else ReceivePlayerResourceCostPaid();
}

ASAFPlayerState* USAFAbility::GetOwningPlayerState() const {
	if (const AActor* OwningActor = GetOwningActorFromActorInfo()) {
		// Try to get player state from the owning actor
		if (const APawn* OwningPawn = Cast<APawn>(OwningActor)) 
			return Cast<ASAFPlayerState>(OwningPawn->GetPlayerState());
		
		// For non-pawn actors, try to get via player controller
		if (const APlayerController* PC = OwningActor->GetInstigatorController<APlayerController>()) 
			return Cast<ASAFPlayerState>(PC->PlayerState);
		
		// Last resort: check if the actor itself is a player state
		return Cast<ASAFPlayerState>(const_cast<AActor*>(OwningActor));
	}
	
	return nullptr;
}

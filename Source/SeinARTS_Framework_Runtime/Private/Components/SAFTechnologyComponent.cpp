#include "Components/SAFTechnologyComponent.h"
#include "Components/SAFPlayerTechnologyComponent.h"
#include "Classes/SAFUnit.h"
#include "Classes/SAFPlayerState.h"
#include "Assets/SAFUnitAsset.h"
#include "Assets/SAFTechnologyAsset.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "AttributeSet.h"
#include "GameplayTagsManager.h"
#include "Interfaces/SAFActorInterface.h"
#include "Debug/SAFDebugTool.h"

USAFTechnologyComponent::USAFTechnologyComponent() {
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false); // Technology state is driven by player component
}

void USAFTechnologyComponent::BeginPlay() {
	Super::BeginPlay();
	
	// Bind to player technology changes if we have authority
	AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
		if (USAFPlayerTechnologyComponent* PlayerTechComp = GetPlayerTechnologyComponent())
			PlayerTechComp->OnTechnologyTagChanged.AddDynamic(this, &USAFTechnologyComponent::OnPlayerTechnologyChanged);
}

void USAFTechnologyComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	ClearAllTechnologyEffects();
	if (USAFPlayerTechnologyComponent* PlayerTechComp = GetPlayerTechnologyComponent()) PlayerTechComp->OnTechnologyTagChanged.RemoveAll(this);
	Super::EndPlay(EndPlayReason);
}

// Technology Application API
// =======================================================================================================
void USAFTechnologyComponent::ApplyTechnologyBundleAtSpawn(const USAFUnitAsset* UnitAsset) {
    AActor* Owner = GetOwner(); 
	if (!Owner || !Owner->HasAuthority() || !UnitAsset) return;
	
	USAFPlayerTechnologyComponent* PlayerTechComp = GetPlayerTechnologyComponent();
	if (!PlayerTechComp) { SAFDEBUG_WARNING("ApplyTechnologyBundleAtSpawn: No player technology component found."); return; }
	
	// Build and apply technology bundle
	FSAFTechnologyBundle TechBundle = PlayerTechComp->BuildTechnologyBundleForUnit(UnitAsset);
	ApplyTechnologyBundle(TechBundle);
	
	SAFDEBUG_SUCCESS(FORMATSTR("Applied technology bundle at spawn for unit '%s'.", *UnitAsset->GetName()));
}

void USAFTechnologyComponent::RefreshTechnologyEffects(FGameplayTag ChangedTechnologyTag) {
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority()) return;
	
	// Check if the changed technology affects this unit
	const USAFUnitAsset* UnitAsset = GetUnitAsset();
	if (!UnitAsset) return;
	
	// For now, do a full refresh on any technology change
	// TODO: Optimize to only refresh if the changed technology actually affects this unit
	RebuildAndApplyTechnologyBundle();
	
	SAFDEBUG_INFO(FORMATSTR("Refreshed technology effects for changed tag: %s", *ChangedTechnologyTag.ToString()));
}

void USAFTechnologyComponent::RefreshTechnologyModifications() {
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority()) return;
	
	// Clear existing modifications first
	ClearAllTechnologyEffects();
	
	// Get fresh technology bundle from player technology component
	if (USAFPlayerTechnologyComponent* PlayerTechComp = GetPlayerTechnologyComponent()) {
		if (const USAFUnitAsset* UnitAsset = GetUnitAsset()) {
			FSAFTechnologyBundle NewBundle = PlayerTechComp->BuildTechnologyBundleForUnit(UnitAsset);
			ApplyTechnologyBundle(NewBundle);
			
			SAFDEBUG_SUCCESS(FORMATSTR("Refreshed technology modifications for unit '%s'.", 
				*UnitAsset->GetName()));
		}
	}
}

void USAFTechnologyComponent::ApplyTechnologyBundle(const FSAFTechnologyBundle& TechnologyBundle) {
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority()) return;
	
	UAbilitySystemComponent* UnitASC = GetUnitAbilitySystemComponent();
	if (!UnitASC) { SAFDEBUG_WARNING("ApplyTechnologyBundle: Unit has no ability system component."); return; }
	
	// Apply each type of modification
	ApplyTechnologyEffects(TechnologyBundle.GameplayEffects);
	ApplyTechnologyAbilities(TechnologyBundle.GameplayAbilities);
	ApplyTechnologyAttributeSets(TechnologyBundle.AttributeSets);
	ApplyTechnologyTags(TechnologyBundle.GameplayTags);
	
	SAFDEBUG_SUCCESS("Successfully applied complete technology bundle.");
}

void USAFTechnologyComponent::ClearAllTechnologyEffects() {
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority()) return;
	
	ClearAllTechnologyModifications();
	SAFDEBUG_INFO("Cleared all technology effects.");
}

// Internal Application Logic
// ============================================================================================================================
UAbilitySystemComponent* USAFTechnologyComponent::GetUnitAbilitySystemComponent() const {
	if (AActor* Owner = GetOwner())
		if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Owner)) 
            return ASI->GetAbilitySystemComponent();
	return nullptr;
}

const USAFUnitAsset* USAFTechnologyComponent::GetUnitAsset() const {
	if (ASAFUnit* OwnerUnit = Cast<ASAFUnit>(GetOwner())) return OwnerUnit->GetUnitAsset();
	return nullptr;
}

void USAFTechnologyComponent::ApplyTechnologyEffects(const TArray<FGameplayEffectSpecHandle>& EffectSpecs) {
	UAbilitySystemComponent* UnitASC = GetUnitAbilitySystemComponent();
	if (!UnitASC) return;
	
	for (const FGameplayEffectSpecHandle& SpecHandle : EffectSpecs)
		if (SpecHandle.IsValid()) UnitASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
	
	SAFDEBUG_INFO(FORMATSTR("Applied %d technology effects.", EffectSpecs.Num()));
}

void USAFTechnologyComponent::ApplyTechnologyAbilities(const TArray<FGameplayAbilitySpec>& AbilitySpecs) {
	UAbilitySystemComponent* UnitASC = GetUnitAbilitySystemComponent();
	if (!UnitASC) return;
	
	for (const FGameplayAbilitySpec& AbilitySpec : AbilitySpecs) UnitASC->GiveAbility(AbilitySpec);
	SAFDEBUG_INFO(FORMATSTR("Applied %d technology abilities.", AbilitySpecs.Num()));
}

void USAFTechnologyComponent::ApplyTechnologyAttributeSets(const TArray<TSubclassOf<UAttributeSet>>& AttributeSetClasses) {
	UAbilitySystemComponent* UnitASC = GetUnitAbilitySystemComponent();
	if (!UnitASC) return;
	
    // Add each attribute set if not already present
	for (const TSubclassOf<UAttributeSet>& AttributeSetClass : AttributeSetClasses) {
		if (AttributeSetClass) {
			const UAttributeSet* ExistingSet = UnitASC->GetAttributeSet(AttributeSetClass);
			if (!ExistingSet) {
				UAttributeSet* NewAttributeSet = NewObject<UAttributeSet>(GetOwner(), AttributeSetClass);
				if (NewAttributeSet) UnitASC->AddAttributeSetSubobject(NewAttributeSet);
			}
		}
	}
	
	SAFDEBUG_INFO(FORMATSTR("Applied %d technology attribute sets.", AttributeSetClasses.Num()));
}

void USAFTechnologyComponent::ApplyTechnologyTags(const FGameplayTagContainer& Tags) {
	UAbilitySystemComponent* UnitASC = GetUnitAbilitySystemComponent();
	if (!UnitASC) return;
	
	for (const FGameplayTag& Tag : Tags)
		if (Tag.IsValid() && !UnitASC->HasMatchingGameplayTag(Tag)) UnitASC->AddLooseGameplayTag(Tag);
	
	SAFDEBUG_INFO(FORMATSTR("Applied %d technology tags.", Tags.Num()));
}



void USAFTechnologyComponent::ClearAllTechnologyModifications() {
	UAbilitySystemComponent* UnitASC = GetUnitAbilitySystemComponent();
	if (!UnitASC) return;
	
	// Clear effects with technology source tag
	FGameplayTagContainer TechSourceTags;
	TechSourceTags.AddTag(TechnologySourceTag);
	UnitASC->RemoveActiveEffectsWithTags(TechSourceTags);
	
	// Clear loose gameplay tags that match technology source
	FGameplayTagContainer AllOwnedTags;
	UnitASC->GetOwnedGameplayTags(AllOwnedTags);
	for (const FGameplayTag& Tag : AllOwnedTags) if (Tag.MatchesTag(TechnologySourceTag)) UnitASC->RemoveLooseGameplayTag(Tag);
	
	// Note: Attribute sets are intentionally not removed as this can be unsafe during gameplay
	// Technology-granted attribute sets will be automatically cleaned up when the unit is destroyed
	SAFDEBUG_INFO("Cleared all technology modifications using GAS-native queries.");
}

// Player Technology Integration
// ============================================================================================================================
USAFPlayerTechnologyComponent* USAFTechnologyComponent::GetPlayerTechnologyComponent() const {
	if (AActor* Owner = GetOwner()) {
		if (ISAFActorInterface* ActorInterface = Cast<ISAFActorInterface>(Owner)) {
			if (ASAFPlayerState* PlayerState = ISAFActorInterface::Execute_GetOwningPlayer(Owner)) {
				return PlayerState->GetComponentByClass<USAFPlayerTechnologyComponent>();
			}
		}
	}

	return nullptr;
}

void USAFTechnologyComponent::OnPlayerTechnologyChanged(FGameplayTag ChangedTag, int32 NewCount) {
	if (NewCount > 0) RefreshTechnologyEffects(ChangedTag);
	else RebuildAndApplyTechnologyBundle();
}

void USAFTechnologyComponent::RebuildAndApplyTechnologyBundle() {
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority()) return;
	
	const USAFUnitAsset* UnitAsset = GetUnitAsset();
	if (!UnitAsset) return;
	
	ClearAllTechnologyModifications();
	ApplyTechnologyBundleAtSpawn(UnitAsset);
}


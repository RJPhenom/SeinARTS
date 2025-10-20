#include "Components/SAFPlayerTechnologyComponent.h"
#include "Components/SAFTechnologyComponent.h"
#include "Components/SAFProductionComponent.h"
#include "Classes/SAFPlayerState.h"
#include "Assets/SAFTechnologyAsset.h"
#include "Assets/SAFUnitAsset.h"
#include "Gameplay/Abilities/SAFAbility.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagsManager.h"
#include "EngineUtils.h"
#include "Interfaces/SAFActorInterface.h"
#include "Net/UnrealNetwork.h"
#include "Utils/SAFLibrary.h"
#include "Debug/SAFDebugTool.h"

USAFPlayerTechnologyComponent::USAFPlayerTechnologyComponent() {
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
}

void USAFPlayerTechnologyComponent::BeginPlay() {
	Super::BeginPlay();
	
	AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority()) {
		InitAbilityActorInfo(GetOwner(), GetOwner());
		FGameplayTag TechTag = FGameplayTag::RequestGameplayTag(TEXT("Technology.Unlocked"));
		TechnologyTagChangedHandle = RegisterGameplayTagEvent(TechTag, EGameplayTagEventType::AnyCountChange)
			.AddUObject(this, &USAFPlayerTechnologyComponent::HandleTechnologyTagChanged);
	}
}

void USAFPlayerTechnologyComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	// Clean up tag change delegates
	if (TechnologyTagChangedHandle.IsValid()) {
		FGameplayTag TechTag = FGameplayTag::RequestGameplayTag(TEXT("Technology.Unlocked"));
		UnregisterGameplayTagEvent(TechnologyTagChangedHandle, TechTag, EGameplayTagEventType::AnyCountChange);
		TechnologyTagChangedHandle.Reset();
	}
	
	Super::EndPlay(EndPlayReason);
}

// Technology State Query API
// =======================================================================================================
bool USAFPlayerTechnologyComponent::HasTechnology(FGameplayTag TechnologyTag) const {
	if (!TechnologyTag.IsValid()) return false;
	return HasMatchingGameplayTag(TechnologyTag);
}

bool USAFPlayerTechnologyComponent::CanResearchTechnology(const USAFTechnologyAsset* TechnologyAsset) const {
	if (!TechnologyAsset) return false;
	if (HasTechnology(TechnologyAsset->TechnologyUnlockTag)) return false;
	
	// Check prerequisites
	for (const FGameplayTag& PrereqTag : TechnologyAsset->PrerequisiteTechnologies)
		if (!HasTechnology(PrereqTag)) return false;
	
	return true;
}

void USAFPlayerTechnologyComponent::GetResearchedTechnologies(FGameplayTagContainer& OutTechnologies) const {
	OutTechnologies.Reset();
	
	FGameplayTagContainer PlayerTags;
	GetOwnedGameplayTags(PlayerTags);
	
	// Filter for technology tags
	FGameplayTag TechParentTag = FGameplayTag::RequestGameplayTag(TEXT("Technology.Unlocked"));
	for (const FGameplayTag& Tag : PlayerTags) if (Tag.MatchesTag(TechParentTag)) OutTechnologies.AddTag(Tag);
}

// Technology Modification API  
// =======================================================================================================
FSAFResourceBundle USAFPlayerTechnologyComponent::ResolveEffectiveResourceCosts(const USAFAsset* Asset) const {
	if (!Asset) return FSAFResourceBundle{};
	
	// Get base costs from asset
	FSAFResourceBundle BaseCosts;
	
	if (const USAFTechnologyAsset* TechAsset = Cast<USAFTechnologyAsset>(Asset)) {
		BaseCosts = TechAsset->ResearchCost;
	} else {
		// For unit assets, get base costs from asset data
		// Since USAFAsset doesn't have GetBaseCosts method yet, use a fallback approach
		// TODO: This should be properly implemented when USAFAsset has cost resolution methods
		BaseCosts = FSAFResourceBundle{}; // Will be resolved by production component's ResolveCostsByData
	}
	
	// Apply technology-based cost modifications using GAS
	FSAFResourceBundle ModifiedCosts = BaseCosts;
	
	// Check for cost reduction effects from researched technologies
	FGameplayTagContainer AssetTypeTags;
	if (const USAFUnitAsset* UnitAsset = Cast<USAFUnitAsset>(Asset)) AssetTypeTags = UnitAsset->Tags;
	else if (const USAFTechnologyAsset* TechAsset = Cast<USAFTechnologyAsset>(Asset)) AssetTypeTags = TechAsset->Tags;
	
	// Apply percentage-based cost reductions from technology effects
	// This would use MMC calculations in a real implementation
	float CostMultiplier = CalculateCostMultiplier(AssetTypeTags);
	
	// Only apply multiplier if we have actual costs to modify
	if (!BaseCosts.IsZero()) {
		ModifiedCosts.Resource1 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource1 * CostMultiplier);
		ModifiedCosts.Resource2 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource2 * CostMultiplier);
		ModifiedCosts.Resource3 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource3 * CostMultiplier);
		ModifiedCosts.Resource4 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource4 * CostMultiplier);
		ModifiedCosts.Resource5 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource5 * CostMultiplier);
		ModifiedCosts.Resource6 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource6 * CostMultiplier);
		ModifiedCosts.Resource7 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource7 * CostMultiplier);
		ModifiedCosts.Resource8 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource8 * CostMultiplier);
		ModifiedCosts.Resource9 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource9 * CostMultiplier);
		ModifiedCosts.Resource10 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource10 * CostMultiplier);
		ModifiedCosts.Resource11 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource11 * CostMultiplier);
		ModifiedCosts.Resource12 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource12 * CostMultiplier);
	}
	
	return ModifiedCosts;
}

float USAFPlayerTechnologyComponent::ResolveEffectiveBuildTime(const USAFAsset* Asset) const {
	if (!Asset) return 1.0f;
	
	// Get base build time from asset
	float BaseBuildTime = 1.0f;
	
	if (const USAFTechnologyAsset* TechAsset = Cast<USAFTechnologyAsset>(Asset)) {
		BaseBuildTime = TechAsset->ResearchTime;
	} else {
		// For unit assets, get base build time from asset data
		// Since USAFAsset doesn't have GetBaseBuildTime method yet, use fallback
		// TODO: This should be properly implemented when USAFAsset has time resolution methods
		BaseBuildTime = 1.0f; // Will be resolved by production component's ResolveBuildTime
	}
	
	// Apply technology-based time modifications using GAS
	FGameplayTagContainer AssetTypeTags;
	if (const USAFUnitAsset* UnitAsset = Cast<USAFUnitAsset>(Asset)) AssetTypeTags = UnitAsset->Tags;
	else if (const USAFTechnologyAsset* TechAsset = Cast<USAFTechnologyAsset>(Asset)) AssetTypeTags = TechAsset->Tags;
	
	// Apply percentage-based time reductions from technology effects
	// This would use MMC calculations in a real implementation
	float TimeMultiplier = CalculateTimeMultiplier(AssetTypeTags);
	float ModifiedBuildTime = BaseBuildTime * TimeMultiplier;
	return FMath::Max(ModifiedBuildTime, 0.1f);
}

FSAFTechnologyBundle USAFPlayerTechnologyComponent::BuildTechnologyBundleForUnit(const USAFUnitAsset* UnitAsset) const {
	FSAFTechnologyBundle Bundle;
	if (!UnitAsset) return Bundle;
	
	// Get all researched technology tags and resolve their assets for bundle building
	FGameplayTagContainer PlayerTags;
	GetOwnedGameplayTags(PlayerTags);
	
	// Find all technology-related tags  
	FGameplayTag TechUnlockedParent = FGameplayTag::RequestGameplayTag("Technology.Unlocked");
	for (const FGameplayTag& Tag : PlayerTags) {
		if (Tag.MatchesTag(TechUnlockedParent)) {
			// Try to find the technology asset associated with this tag
			// For now, we'll use a simple registry approach - this could be optimized with a lookup table
			if (USAFTechnologyAsset* TechAsset = FindTechnologyAssetByUnlockTag(Tag)) {
				if (DoesTechnologyApplyToUnit(TechAsset, UnitAsset)) {
					TArray<FGameplayEffectSpecHandle> EffectSpecs;
					BuildGameplayEffectSpecs(TechAsset->GrantedGameplayEffects, EffectSpecs);
					Bundle.GameplayEffects.Append(EffectSpecs);
					
					for (const FSAFTaggedAbility& TaggedAbility : TechAsset->GrantedGameplayAbilities) {
						if (TaggedAbility.AbilityClass) {
							FGameplayAbilitySpec AbilitySpec(TaggedAbility.AbilityClass, TaggedAbility.Level, TaggedAbility.InputID, nullptr);
							AbilitySpec.GetDynamicSpecSourceTags().AppendTags(TaggedAbility.AbilityTags);
							Bundle.GameplayAbilities.Add(AbilitySpec);
						}
					}
					
					Bundle.AttributeSets.Append(TechAsset->GrantedAttributeSets);
					Bundle.GameplayTags.AppendTags(TechAsset->GrantedGameplayTags);
				}
			}
		}
	}
	
	return Bundle;
}

void USAFPlayerTechnologyComponent::NotifyProductionRecipeUnlocks(const USAFTechnologyAsset* TechnologyAsset) {
	if (!TechnologyAsset || TechnologyAsset->UnlockedProductionRecipes.Num() == 0) return;

	UWorld* World = GetWorld();
	if (!World) return;
	
	ASAFPlayerState* OwnerPlayerState = Cast<ASAFPlayerState>(GetOwner());
	if (!OwnerPlayerState) return;
	
	for (TActorIterator<AActor> ActorIterator(World); ActorIterator; ++ActorIterator) {
		AActor* Actor = *ActorIterator;
		if (!Actor || !SAFLibrary::IsActorPtrValidSeinARTSUnit(Actor)) continue;
		
		// Check if this actor is owned by our player and has a production component.
		// If so, add or enable the unlocked recipes
		ASAFPlayerState* ActorOwner = ISAFActorInterface::Execute_GetOwningPlayer(Actor);
		if (ActorOwner == OwnerPlayerState)
			if (USAFProductionComponent* ProdComp = Actor->GetComponentByClass<USAFProductionComponent>())
				ProdComp->AddOrEnableProductionRecipes(TechnologyAsset->UnlockedProductionRecipes);
	}
	
	SAFDEBUG_SUCCESS(FORMATSTR("Technology '%s' notified production components about %d unlocked recipes.", 
		*TechnologyAsset->GetName(), TechnologyAsset->UnlockedProductionRecipes.Num()));
}

void USAFPlayerTechnologyComponent::NotifyTechnologyUnitsRefresh(const USAFTechnologyAsset* TechnologyAsset) {
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || !TechnologyAsset) return;
	
	UWorld* World = GetWorld();
	if (!World) return;
	
	ASAFPlayerState* OwnerPlayerState = Cast<ASAFPlayerState>(GetOwner());
	if (!OwnerPlayerState) return;
	
	int32 RefreshedUnits = 0;
	
	for (TActorIterator<AActor> ActorIterator(World); ActorIterator; ++ActorIterator) {
		AActor* Actor = *ActorIterator;
		if (!Actor || !SAFLibrary::IsActorPtrValidSeinARTSUnit(Actor)) continue;
		
		// Check if this actor is owned by our player and has a technology component.
		// If so, refresh its technology modifications
		ASAFPlayerState* ActorOwner = ISAFActorInterface::Execute_GetOwningPlayer(Actor);
		if (ActorOwner == OwnerPlayerState) {
			if (USAFTechnologyComponent* TechComp = Actor->GetComponentByClass<USAFTechnologyComponent>()) {
				TechComp->RefreshTechnologyModifications();
				RefreshedUnits++;
			}
		}
	}
	
	SAFDEBUG_SUCCESS(FORMATSTR("Technology '%s' refreshed %d units with new modifications.", 
		*TechnologyAsset->GetName(), RefreshedUnits));
}

// Technology Research API
// =======================================================================================================
bool USAFPlayerTechnologyComponent::ResearchTechnology(const USAFTechnologyAsset* TechnologyAsset) {
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || !TechnologyAsset) return false;
	if (!CanResearchTechnology(TechnologyAsset)) { SAFDEBUG_WARNING(FORMATSTR("Cannot research '%s': prerequisites not met.", 
		*TechnologyAsset->GetName())); return false; }
	
	AddLooseGameplayTag(TechnologyAsset->TechnologyUnlockTag);
	ApplyTechnologyEffects(TechnologyAsset);
	NotifyProductionRecipeUnlocks(TechnologyAsset);
	
	SAFDEBUG_SUCCESS(FORMATSTR("Successfully researched technology '%s'.", *TechnologyAsset->GetName()));
	return true;
}

void USAFPlayerTechnologyComponent::InstantResearchTechnology(const USAFTechnologyAsset* TechnologyAsset) {
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || !TechnologyAsset) return;
	
	// Force research without prerequisite checks
	AddLooseGameplayTag(TechnologyAsset->TechnologyUnlockTag);
	ApplyTechnologyEffects(TechnologyAsset);
	NotifyProductionRecipeUnlocks(TechnologyAsset);
	
	SAFDEBUG_SUCCESS(FORMATSTR("Instantly researched technology '%s' (debug).", *TechnologyAsset->GetName()));
}

void USAFPlayerTechnologyComponent::BuildGameplayEffectSpecs(
	const TArray<TSubclassOf<UGameplayEffect>>& EffectClasses, 
	TArray<FGameplayEffectSpecHandle>& OutSpecs
) const {
	FGameplayEffectContextHandle Context = MakeEffectContext();
	Context.AddSourceObject(GetOwner());
	
	for (const TSubclassOf<UGameplayEffect>& EffectClass : EffectClasses) {
		if (EffectClass) {
			FGameplayEffectSpecHandle SpecHandle = MakeOutgoingSpec(EffectClass, 1.0f, Context);
			if (SpecHandle.IsValid()) {
				OutSpecs.Add(SpecHandle);
			}
		}
	}
}

void USAFPlayerTechnologyComponent::HandleTechnologyTagChanged(const FGameplayTag Tag, int32 NewCount) {
	// Broadcast the change to interested listeners (like unit technology components)
	OnTechnologyTagChanged.Broadcast(Tag, NewCount);
	
	SAFDEBUG_INFO(FORMATSTR("Technology tag changed: %s, Count: %d", *Tag.ToString(), NewCount));
}

void USAFPlayerTechnologyComponent::ApplyTechnologyEffects(const USAFTechnologyAsset* TechnologyAsset) {
	if (!TechnologyAsset) return;
	
	// Apply any player-level effects from the technology
	// This could include global buffs, resource generation bonuses, etc.
	
	NotifyProductionRecipeUnlocks(TechnologyAsset);
	NotifyTechnologyUnitsRefresh(TechnologyAsset);
	
	SAFDEBUG_SUCCESS(FORMATSTR("Applied technology effects for '%s'.", *TechnologyAsset->GetName()));
}

bool USAFPlayerTechnologyComponent::DoesTechnologyApplyToUnit(const USAFTechnologyAsset* TechnologyAsset, const USAFUnitAsset* UnitAsset) const {
	if (!TechnologyAsset || !UnitAsset) return false;
	if (TechnologyAsset->bAppliesToAllUnits) return true;
	
	// Check if unit's tags match any of the technology's target scopes
	for (const FGameplayTag& TargetScope : TechnologyAsset->UnitScopeTags)
		if (UnitAsset->Tags.HasTag(TargetScope)) return true;
	
	return false;
}

float USAFPlayerTechnologyComponent::CalculateCostMultiplier(const FGameplayTagContainer& AssetTypeTags) const {
	float CostMultiplier = 1.0f;
	
	// Check for cost reduction effects from researched technologies
	// This is a simplified implementation - in a real MMC system, you would:
	// 1. Create custom MMCs that read from technology-granted attribute sets
	// 2. Use gameplay effects with MMCs to calculate dynamic cost reductions
	// 3. Apply effects to temporary attribute sets for calculation
	
	// For each researched technology, check if it provides cost reduction for these asset types
	TArray<USAFTechnologyAsset*> ResearchedAssets = GetAllResearchedTechnologyAssets();
	for (USAFTechnologyAsset* TechAsset : ResearchedAssets) {
		if (TechAsset) {
			// Check if this technology affects the asset type
			if (AssetTypeTags.HasAnyExact(TechAsset->UnitScopeTags)) {
				// Apply cost reduction (example: 10% reduction per relevant tech)
				// In real implementation, this would come from the technology's effects
				CostMultiplier *= 0.9f; // 10% reduction
			}
		}
	}
	
	// Ensure minimum cost (prevent free units/technologies)
	return FMath::Max(CostMultiplier, 0.1f);
}

float USAFPlayerTechnologyComponent::CalculateTimeMultiplier(const FGameplayTagContainer& AssetTypeTags) const {
	float TimeMultiplier = 1.0f;
	
	// Check for build time reduction effects from researched technologies
	// Similar to cost multiplier but for build/research time
	
	TArray<USAFTechnologyAsset*> ResearchedAssets = GetAllResearchedTechnologyAssets();
	for (USAFTechnologyAsset* TechAsset : ResearchedAssets) {
		if (TechAsset) {
			// Check if this technology affects the asset type
			if (AssetTypeTags.HasAnyExact(TechAsset->UnitScopeTags)) {
				// Apply time reduction (example: 15% reduction per relevant tech)
				// In real implementation, this would come from the technology's effects
				TimeMultiplier *= 0.85f; // 15% reduction
			}
		}
	}
	
	// Ensure minimum time multiplier (prevent instant builds)
	return FMath::Max(TimeMultiplier, 0.2f);
}

// Helper Methods for GAS-Based Technology Resolution
// =======================================================================================================
USAFTechnologyAsset* USAFPlayerTechnologyComponent::FindTechnologyAssetByUnlockTag(const FGameplayTag& UnlockTag) const {
	// This is a simplified implementation - in production you might want to use an asset registry lookup table
	// or cache technology assets for better performance
	
	// For now, we'll do a basic search through loaded technology assets
	// This could be optimized with a static lookup table or asset registry queries
	for (TObjectIterator<USAFTechnologyAsset> AssetIterator; AssetIterator; ++AssetIterator) {
		USAFTechnologyAsset* TechAsset = *AssetIterator;
		if (TechAsset && TechAsset->TechnologyUnlockTag == UnlockTag) {
			return TechAsset;
		}
	}
	
	return nullptr;
}

TArray<USAFTechnologyAsset*> USAFPlayerTechnologyComponent::GetAllResearchedTechnologyAssets() const {
	TArray<USAFTechnologyAsset*> ResearchedAssets;
	
	// Get all owned technology tags
	FGameplayTagContainer PlayerTags;
	GetOwnedGameplayTags(PlayerTags);
	
	FGameplayTag TechUnlockedParent = FGameplayTag::RequestGameplayTag("Technology.Unlocked");
	for (const FGameplayTag& Tag : PlayerTags) {
		if (Tag.MatchesTag(TechUnlockedParent)) {
			if (USAFTechnologyAsset* TechAsset = FindTechnologyAssetByUnlockTag(Tag)) {
				ResearchedAssets.AddUnique(TechAsset);
			}
		}
	}
	
	return ResearchedAssets;
}

// Replication
// =======================================================================================================
void USAFPlayerTechnologyComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// No longer replicating ResearchedTechnologies array - using GAS-native tag replication instead
}
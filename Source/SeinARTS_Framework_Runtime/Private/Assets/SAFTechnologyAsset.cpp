#include "Assets/SAFTechnologyAsset.h"
#include "GameplayTagsManager.h"
#include "Debug/SAFDebugTool.h"

USAFTechnologyAsset::USAFTechnologyAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer) {
	
	// Default technology unlock tag pattern
	if (TechnologyUnlockTag.IsValid()) {
		TechnologyUnlockTag = FGameplayTag::RequestGameplayTag(TEXT("Technology.Unlocked.Default"));
	}
}

void USAFTechnologyAsset::PostLoad() {
	Super::PostLoad();
	ValidateTechnologyConfiguration();
}

void USAFTechnologyAsset::ValidateTechnologyConfiguration() {
	// Validate technology unlock tag
	if (!TechnologyUnlockTag.IsValid()) {
		SAFDEBUG_WARNING(FORMATSTR("Technology asset '%s' has no valid TechnologyUnlockTag set.", *GetName()));
	}

	// Validate that unlock tag follows expected pattern
	if (TechnologyUnlockTag.IsValid() && !TechnologyUnlockTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Technology.Unlocked")))) {
		SAFDEBUG_WARNING(FORMATSTR("Technology asset '%s' has TechnologyUnlockTag '%s' that doesn't follow 'Technology.Unlocked.*' pattern.", 
			*GetName(), *TechnologyUnlockTag.ToString()));
	}

	// Validate target scopes if not global
	if (!bAppliesToAllUnits && UnitScopeTags.IsEmpty()) {
		SAFDEBUG_WARNING(FORMATSTR("Technology asset '%s' has no target unit scopes and is not set to apply to all units.", *GetName()));
	}

	// Validate that we have some form of modification
	bool bHasModifications = 
        GrantedGameplayEffects.Num() > 0 
        || GrantedAttributeSets.Num() > 0 
        || !GrantedGameplayTags.IsEmpty() 
        || UnlockedProductionRecipes.Num() > 0;

	if (!bHasModifications) {
		SAFDEBUG_WARNING(FORMATSTR("Technology asset '%s' has no modifications defined - it will have no effect when researched.", *GetName()));
	}
}
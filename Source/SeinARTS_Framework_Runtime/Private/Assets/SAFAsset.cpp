#include "Assets/SAFAsset.h"
#include "Classes/SAFActor.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"

const FPrimaryAssetType USAFAsset::PrimaryAssetType(TEXT("SAFAsset"));

USAFAsset::USAFAsset(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
	const FGameplayTag Root = FGameplayTag::RequestGameplayTag(TEXT("SeinARTS"));
	if (!Tags.HasTagExact(Root)) Tags.AddTag(Root);
	InstanceClass = ASAFActor::StaticClass();
}

void USAFAsset::PostLoad() {
	Super::PostLoad();
	const FGameplayTag Root = FGameplayTag::RequestGameplayTag(TEXT("SeinARTS"));
	if (!Tags.HasTagExact(Root)) Tags.AddTag(Root);
	if (!InstanceClass)	InstanceClass = ASAFActor::StaticClass();
}

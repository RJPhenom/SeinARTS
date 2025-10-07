#include "Assets/SAFAsset.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"

const FPrimaryAssetType USAFAsset::PrimaryAssetType(TEXT("SAFAsset"));

USAFAsset::USAFAsset(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
  const FGameplayTag Root = FGameplayTag::RequestGameplayTag(TEXT("SeinARTS"));
  if (!Tags.HasTagExact(Root)) Tags.AddTag(Root);
}

void USAFAsset::PostLoad() {
  Super::PostLoad();
  const FGameplayTag Root = FGameplayTag::RequestGameplayTag(TEXT("SeinARTS"));
  if (!Tags.HasTagExact(Root)) Tags.AddTag(Root);
}

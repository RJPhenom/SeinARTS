#include "SeinARTS_Framework_EditorStyle.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleRegistry.h"
#include "Brushes/SlateImageBrush.h"

TSharedPtr<FSlateStyleSet> FSeinARTS_Framework_EditorStyle::StyleSet;

#define IMAGE_BRUSH(RelativePath, Size) FSlateImageBrush(StyleSet->RootToContentDir(RelativePath, TEXT(".png")), Size)

void FSeinARTS_Framework_EditorStyle::Initialize() {
  if (StyleSet.IsValid()) return;

  StyleSet = MakeShared<FSlateStyleSet>("SeinARTS_Framework_Editor");
  // Point to <YourPlugin>/Resources
  const FString BaseDir = IPluginManager::Get().FindPlugin(TEXT("SeinARTS_Framework"))->GetBaseDir();
  StyleSet->SetContentRoot(BaseDir / TEXT("Resources"));

  const FVector2D Icon16(16.f, 16.f);
  const FVector2D Icon64(64.f, 64.f);

  // NOTE: use class names WITHOUT the U/A prefix
  StyleSet->Set("ClassIcon.SAFAsset",      new IMAGE_BRUSH(TEXT("BrandKit/SeinARTS_GrayPlusIcon16"), Icon16));
  StyleSet->Set("ClassThumbnail.SAFAsset", new IMAGE_BRUSH(TEXT("BrandKit/SeinARTS_GrayPlusIcon92"), Icon64));

  StyleSet->Set("ClassIcon.SAFActor",      new IMAGE_BRUSH(TEXT("BrandKit/SeinARTS_GrayClassIcon16"), Icon16));
  StyleSet->Set("ClassThumbnail.SAFActor", new IMAGE_BRUSH(TEXT("BrandKit/SeinARTS_GrayClassIcon92"), Icon64));

  FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
}

void FSeinARTS_Framework_EditorStyle::Shutdown() {
  if (!StyleSet.IsValid()) return;
  FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
  StyleSet.Reset();
}

#undef IMAGE_BRUSH

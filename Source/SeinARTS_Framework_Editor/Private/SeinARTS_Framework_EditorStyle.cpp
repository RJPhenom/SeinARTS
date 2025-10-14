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

  // Branding images
  StyleSet->Set("SeinARTS.Wordmark", new IMAGE_BRUSH(TEXT("BrandKit/SeinARTS_Wordmark_White_Small"), FVector2D(165, 52)));
  StyleSet->Set("SeinARTS.GrayIcon", new IMAGE_BRUSH(TEXT("BrandKit/SeinARTS_GrayIcon16"), Icon16));

  // Custom button styles - use default Button style with gray tint
  FButtonStyle GrayButton = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
  GrayButton.Normal.TintColor = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("444444")));
  GrayButton.Hovered.TintColor = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("555555")));
  GrayButton.Pressed.TintColor = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("333333")));
  StyleSet->Set("SeinARTS.GrayButton", GrayButton);

  FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
}

void FSeinARTS_Framework_EditorStyle::Shutdown() {
  if (!StyleSet.IsValid()) return;
  FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
  StyleSet.Reset();
}

const FSlateStyleSet& FSeinARTS_Framework_EditorStyle::Get()
{
  return *StyleSet;
}

#undef IMAGE_BRUSH

#include "CQTest.h"
#include "SeinARTSEditorStyle.h"
#include "Engine/Texture2D.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyleRegistry.h"

namespace UE::SeinARTSTests
{
	struct FScopedStyleReinitializer
	{
		~FScopedStyleReinitializer()
		{
			FSeinARTSEditorStyle::Initialize();
		}
	};

	TEST(EditorStyleTextureOwnership, "SeinARTS.Editor.Style")
	{
		FScopedStyleReinitializer Reinitialize;
		UTexture2D* Original = FSeinARTSEditorStyle::GetIconTexture(TEXT("SeinBlueprintIcon92"));
		ASSERT_THAT(IsNotNull(Original));
		ASSERT_THAT(IsTrue(Original->IsRooted()));
		const ISlateStyle* AppStyle = FSlateStyleRegistry::FindSlateStyle(FAppStyle::GetAppStyleSetName());
		ASSERT_THAT(IsNotNull(AppStyle));
		const FSlateBrush* OriginalShowFlagBrush = AppStyle->GetOptionalBrush(
			TEXT("ShowFlagsMenu.FogOfWar"), nullptr, nullptr);
		ASSERT_THAT(IsNotNull(OriginalShowFlagBrush));

		FSeinARTSEditorStyle::Shutdown();
		ASSERT_THAT(IsFalse(Original->IsRooted()));
		FSeinARTSEditorStyle::Shutdown();

		FSeinARTSEditorStyle::Initialize();
		UTexture2D* Reloaded = FSeinARTSEditorStyle::GetIconTexture(TEXT("SeinBlueprintIcon92"));
		ASSERT_THAT(IsNotNull(Reloaded));
		ASSERT_THAT(IsTrue(Reloaded->IsRooted()));
		const FSlateBrush* ReloadedShowFlagBrush = AppStyle->GetOptionalBrush(
			TEXT("ShowFlagsMenu.FogOfWar"), nullptr, nullptr);
		ASSERT_THAT(IsTrue(ReloadedShowFlagBrush == OriginalShowFlagBrush));
	}
}

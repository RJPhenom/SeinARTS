/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinARTSEditorStyle.cpp
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of the SeinARTS editor Slate style set.
 */

#include "SeinARTSEditorStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyle.h"
#include "Interfaces/IPluginManager.h"
#include "ImageUtils.h"

TSharedPtr<FSlateStyleSet> FSeinARTSEditorStyle::StyleSet = nullptr;
FString FSeinARTSEditorStyle::IconsDir;
TMap<FName, TObjectPtr<UTexture2D>> FSeinARTSEditorStyle::IconTextures;

void FSeinARTSEditorStyle::Initialize()
{
	if (StyleSet.IsValid())
	{
		return;
	}

	StyleSet = MakeShared<FSlateStyleSet>(FName(TEXT("SeinARTSEditorStyle")));

	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SeinARTSFramework"));
	if (!Plugin.IsValid())
	{
		return;
	}

	const FString BrandKitDir = Plugin->GetBaseDir() / TEXT("Resources") / TEXT("BrandKit");
	IconsDir = BrandKitDir;
	StyleSet->SetContentRoot(BrandKitDir);

	// ==================== Entity (SeinActor) ====================
	// Single factory shipped (USeinEntityFactory — bare template). Combat is
	// designer-overridden per DESIGN §11, so no pre-seeded Unit starter kit.

	StyleSet->Set(
		"ClassIcon.SeinActor",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinEntityIcon16"), TEXT(".png")), FVector2D(16.0f, 16.0f))
	);

	StyleSet->Set(
		"ClassThumbnail.SeinActor",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinEntityIcon92"), TEXT(".png")), FVector2D(92.0f, 92.0f))
	);

	// ==================== Ability (SeinAbility) ====================

	StyleSet->Set(
		"ClassIcon.SeinAbility",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinAbilityIcon16"), TEXT(".png")), FVector2D(16.0f, 16.0f))
	);

	StyleSet->Set(
		"ClassThumbnail.SeinAbility",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinAbilityIcon92"), TEXT(".png")), FVector2D(92.0f, 92.0f))
	);

	// ==================== Effect (SeinEffect) ====================

	StyleSet->Set(
		"ClassIcon.SeinEffect",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinEffectIcon16"), TEXT(".png")), FVector2D(16.0f, 16.0f))
	);

	StyleSet->Set(
		"ClassThumbnail.SeinEffect",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinEffectIcon92"), TEXT(".png")), FVector2D(92.0f, 92.0f))
	);

	// ==================== Formation (SeinFormation) ====================

	StyleSet->Set(
		"ClassIcon.SeinFormation",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinFormationIcon16"), TEXT(".png")), FVector2D(16.0f, 16.0f))
	);

	StyleSet->Set(
		"ClassThumbnail.SeinFormation",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinFormationIcon92"), TEXT(".png")), FVector2D(92.0f, 92.0f))
	);

	// ==================== Movement Mode (SeinMovement) ====================
	// Movement modes are plain UBlueprints parented to USeinMovement; the corner
	// badge resolves via the parent-class chain, so registering under the parent
	// class name catches every authored mode BP.

	StyleSet->Set(
		"ClassIcon.SeinMovement",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinMovementIcon16"), TEXT(".png")), FVector2D(16.0f, 16.0f))
	);

	StyleSet->Set(
		"ClassThumbnail.SeinMovement",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinMovementIcon92"), TEXT(".png")), FVector2D(92.0f, 92.0f))
	);

	// ==================== Balance Data Asset (SeinBalanceProfile) ====================

	StyleSet->Set(
		"ClassIcon.SeinBalanceProfile",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinDataIcon16"), TEXT(".png")), FVector2D(16.0f, 16.0f))
	);

	StyleSet->Set(
		"ClassThumbnail.SeinBalanceProfile",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinDataIcon92"), TEXT(".png")), FVector2D(92.0f, 92.0f))
	);

	// ==================== Generic Sein Asset ====================
	// Fallback icon for any SeinARTS asset lacking a type-specific thumbnail.

	StyleSet->Set(
		"ClassIcon.SeinAsset",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinAssetIcon16"), TEXT(".png")), FVector2D(16.0f, 16.0f))
	);

	StyleSet->Set(
		"ClassThumbnail.SeinAsset",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinAssetIcon92"), TEXT(".png")), FVector2D(92.0f, 92.0f))
	);

	// ==================== Component ====================

	// Per §2, "components" split into two tracks: (a) the UDS factory's
	// authoring thumbnail (`SeinSimComponent` — used by USeinSimComponentFactory's
	// GetNewAssetThumbnailOverride), and (b) the base AC class icon
	// (`SeinActorComponent` — UE walks the parent chain for the corner badge,
	// so this catches typed wrappers AND USeinStructComponent instances).
	auto RegisterComponentIcon = [&](const FName& Key)
	{
		StyleSet->Set(
			Key,
			new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinComponentIcon16"), TEXT(".png")), FVector2D(16.0f, 16.0f))
		);
	};
	auto RegisterComponentThumb = [&](const FName& Key)
	{
		StyleSet->Set(
			Key,
			new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinComponentIcon92"), TEXT(".png")), FVector2D(92.0f, 92.0f))
		);
	};

	RegisterComponentIcon(TEXT("ClassIcon.SeinSimComponent"));
	// Direct UActorComponent subclasses in the SeinARTS ClassGroup. UE looks
	// up component icons by exact class name; each entry needs its own
	// registration. SeinEntityComponent is the actor-bridge default subobject
	// on every ASeinActor. The render-side ACs (SeinConstructionRenderComponent)
	// are designer-droppable plain UActorComponents. Squad-slot preview meshes
	// now spawn directly from USeinEntityComponent under WITH_EDITOR — no
	// separate AC needed.
	RegisterComponentIcon(TEXT("ClassIcon.SeinEntityComponent"));
	RegisterComponentIcon(TEXT("ClassIcon.SeinConstructionRenderComponent"));

	RegisterComponentThumb(TEXT("ClassThumbnail.SeinSimComponent"));
	RegisterComponentThumb(TEXT("ClassThumbnail.SeinEntityComponent"));
	RegisterComponentThumb(TEXT("ClassThumbnail.SeinConstructionRenderComponent"));

	// ==================== Widget ====================

	// Same multi-key pattern as Component — the content browser corner badge
	// resolves via the BPGC's parent class chain (USeinUserWidget), not the
	// asset class, so we register under both the asset class AND the parent class.
	auto RegisterWidgetIcon = [&](const FName& Key)
	{
		StyleSet->Set(
			Key,
			new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinWidgetIcon16"), TEXT(".png")), FVector2D(16.0f, 16.0f))
		);
	};
	auto RegisterWidgetThumb = [&](const FName& Key)
	{
		StyleSet->Set(
			Key,
			new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinWidgetIcon92"), TEXT(".png")), FVector2D(92.0f, 92.0f))
		);
	};

	RegisterWidgetIcon(TEXT("ClassIcon.SeinWidgetBlueprint"));
	RegisterWidgetIcon(TEXT("ClassIcon.SeinUserWidget"));

	RegisterWidgetThumb(TEXT("ClassThumbnail.SeinWidgetBlueprint"));
	RegisterWidgetThumb(TEXT("ClassThumbnail.SeinUserWidget"));

	// ==================== Branding ====================

	// Native PNG is 488x126 (aspect ~3.873). Keep that ratio when registering
	// the brush so anything that samples its ImageSize gets correct proportions.
	StyleSet->Set(
		"SeinARTS.Wordmark",
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SeinARTSWordmarkVectorized"), TEXT(".png")), FVector2D(244.0f, 63.0f))
	);

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);

	// ==================== Custom Show Flag Icons ====================
	// UE's FShowFlagMenuCommands::GetShowFlagIcon resolves icons as
	// `FSlateIcon(FAppStyle::GetAppStyleSetName(), "ShowFlagsMenu.<FlagName>")`,
	// so plugin-authored show flags need their icons registered INTO the app
	// style set — not our own. Starship's app style is an FSlateStyleSet, so
	// casting the const registry result back to mutable is safe.
	if (const ISlateStyle* AppStyleBase = FSlateStyleRegistry::FindSlateStyle(FAppStyle::GetAppStyleSetName()))
	{
		FSlateStyleSet* AppStyle = const_cast<FSlateStyleSet*>(static_cast<const FSlateStyleSet*>(AppStyleBase));
		// SVG brushes auto-tint to the theme's foreground colour — matches how UE's
		// own Navigation / Collision / etc. show-flag icons render. Flag name in the
		// style key matches the UE-internal ShowFlag identifier (the same string the
		// custom show flag is registered with via TCustomShowFlag), prefixed with
		// "ShowFlagsMenu." per FShowFlagMenuCommands::GetShowFlagIcon.
		AppStyle->Set(
			"ShowFlagsMenu.FogOfWar",
			new FSlateVectorImageBrush(BrandKitDir / TEXT("SeinFogOfWarViewFlag.svg"), FVector2D(16.0f, 16.0f)));
		AppStyle->Set(
			"ShowFlagsMenu.SeinSteering",
			new FSlateVectorImageBrush(BrandKitDir / TEXT("SeinSteeringViewFlag.svg"), FVector2D(16.0f, 16.0f)));
		AppStyle->Set(
			"ShowFlagsMenu.SeinExtents",
			new FSlateVectorImageBrush(BrandKitDir / TEXT("SeinExtentsViewFlag.svg"), FVector2D(16.0f, 16.0f)));
	}

	// Load PNG files as UTexture2D for thumbnail renderers (FCanvas can't use Slate file brushes)
	LoadAndCacheIcon(FName(TEXT("SeinEntityIcon92")),     TEXT("SeinEntityIcon92.png"));
	LoadAndCacheIcon(FName(TEXT("SeinAbilityIcon92")),    TEXT("SeinAbilityIcon92.png"));
	LoadAndCacheIcon(FName(TEXT("SeinComponentIcon92")),  TEXT("SeinComponentIcon92.png"));
	LoadAndCacheIcon(FName(TEXT("SeinEffectIcon92")),     TEXT("SeinEffectIcon92.png"));
	LoadAndCacheIcon(FName(TEXT("SeinFormationIcon92")),  TEXT("SeinFormationIcon92.png"));
	LoadAndCacheIcon(FName(TEXT("SeinMovementIcon92")),   TEXT("SeinMovementIcon92.png"));
	LoadAndCacheIcon(FName(TEXT("SeinWidgetIcon92")),     TEXT("SeinWidgetIcon92.png"));
	LoadAndCacheIcon(FName(TEXT("SeinAssetIcon92")),      TEXT("SeinAssetIcon92.png"));
}

UTexture2D* FSeinARTSEditorStyle::LoadAndCacheIcon(const FName& TextureName, const FString& Filename)
{
	const FString FullPath = IconsDir / Filename;
	UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(FullPath);
	if (Texture)
	{
		Texture->AddToRoot(); // prevent GC
		Texture->UpdateResource(); // ensure render resource is created
		IconTextures.Add(TextureName, Texture);
	}
	return Texture;
}

UTexture2D* FSeinARTSEditorStyle::GetIconTexture(const FName& TextureName)
{
	if (const TObjectPtr<UTexture2D>* Found = IconTextures.Find(TextureName))
	{
		return *Found;
	}
	return nullptr;
}

FString FSeinARTSEditorStyle::GetIconPath(const FString& IconFilename)
{
	return IconsDir / IconFilename;
}

void FSeinARTSEditorStyle::Shutdown()
{
	// Don't touch the UTexture2D pointers during shutdown — the UObject array
	// may already be torn down, and any TObjectPtr dereference will assert.
	// The textures are rooted transients; the process is exiting so leaking is fine.
	IconTextures.Empty();

	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
		StyleSet.Reset();
	}
}

const FSlateStyleSet& FSeinARTSEditorStyle::Get()
{
	check(StyleSet.IsValid());
	return *StyleSet;
}

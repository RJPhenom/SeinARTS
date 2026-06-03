/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinBlueprintThumbnailRenderer.cpp
 * @date:		4/12/2026
 * @author:		RJ Macklem
 * @brief:		Custom thumbnail renderer implementation.
 */

#include "Thumbnails/SeinBlueprintThumbnailRenderer.h"
#include "SeinARTSEditorStyle.h"
#include "Actor/SeinActor.h"
#include "Abilities/SeinAbility.h"
#include "Effects/SeinEffectBlueprint.h"
#include "Widgets/SeinWidgetBlueprint.h"
#include "Engine/Blueprint.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"

bool USeinBlueprintThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
	const ESeinAssetType Type = ClassifyBlueprint(Object);
	if (Type != ESeinAssetType::None)
	{
		return true;
	}
	return Super::CanVisualizeAsset(Object);
}

void USeinBlueprintThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height,
	FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	const ESeinAssetType AssetType = ClassifyBlueprint(Object);

	// Non-SeinARTS Blueprints: fall through to default rendering
	if (AssetType == ESeinAssetType::None)
	{
		Super::Draw(Object, X, Y, Width, Height, RenderTarget, Canvas, bAdditionalViewFamily);
		return;
	}

	// Unit BPs render their actual mesh hierarchy via the default thumbnail scene
	// when the CDO has anything renderable. Super::CanVisualizeAsset reuses the
	// engine's component walk + FBlueprintThumbnailScene::IsValidComponentForVisualization,
	// so we cleanly fall through to the flat Unit icon for data-only Unit BPs
	// (no mesh yet → would otherwise render as empty grey).
	if (AssetType == ESeinAssetType::Unit && Super::CanVisualizeAsset(Object))
	{
		Super::Draw(Object, X, Y, Width, Height, RenderTarget, Canvas, bAdditionalViewFamily);
		return;
	}

	// ---- Dark background ----
	{
		FCanvasTileItem BgItem(
			FVector2D(X, Y),
			GWhiteTexture,
			FVector2D(Width, Height),
			FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("1A1A1A")))
		);
		BgItem.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(BgItem);
	}

	// ---- Icon (from Slate style set) ----
	const FTexture* IconResource = GetIconResource(AssetType);
	if (IconResource)
	{
		const float IconPadding = Width * 0.1f;
		const float AvailableHeight = Height - IconPadding;
		const float IconSize = FMath::Min((float)Width - IconPadding * 2.0f, AvailableHeight);
		const float IconX = X + (Width - IconSize) * 0.5f;
		const float IconY = Y + (AvailableHeight - IconSize) * 0.5f + IconPadding * 0.25f;

		FCanvasTileItem IconItem(
			FVector2D(IconX, IconY),
			IconResource,
			FVector2D(IconSize, IconSize),
			FLinearColor::White
		);
		IconItem.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(IconItem);
	}

	// Color bar is handled by the Content Browser's SAssetMenuIcon via IAssetTypeActions::GetTypeColor()
}

const FTexture* USeinBlueprintThumbnailRenderer::GetIconResource(ESeinAssetType Type)
{
	FName TextureName;
	switch (Type)
	{
	case ESeinAssetType::Unit:      TextureName = FName(TEXT("SeinEntityIcon92"));    break;
	case ESeinAssetType::Ability:   TextureName = FName(TEXT("SeinAbilityIcon92"));   break;
	case ESeinAssetType::Effect:    TextureName = FName(TEXT("SeinEffectIcon92"));    break;
	case ESeinAssetType::Widget:    TextureName = FName(TEXT("SeinWidgetIcon92"));    break;
	default: return nullptr;
	}

	UTexture2D* Texture = FSeinARTSEditorStyle::GetIconTexture(TextureName);
	if (!Texture)
	{
		return nullptr;
	}

	return Texture->GetResource();
}

USeinBlueprintThumbnailRenderer::ESeinAssetType USeinBlueprintThumbnailRenderer::ClassifyBlueprint(UObject* Object)
{
	const UBlueprint* Blueprint = Cast<UBlueprint>(Object);
	if (!Blueprint || !Blueprint->ParentClass)
	{
		return ESeinAssetType::None;
	}

	// Widget + Effect use asset-class checks (their factories produce dedicated
	// UCLASSes). Unit/Ability use parent-class checks since they're plain
	// UBlueprint assets differentiated only by their parent.
	if (Blueprint->IsA<USeinWidgetBlueprint>())
	{
		return ESeinAssetType::Widget;
	}

	if (Blueprint->IsA<USeinEffectBlueprint>())
	{
		return ESeinAssetType::Effect;
	}

	if (Blueprint->ParentClass->IsChildOf(ASeinActor::StaticClass()))
	{
		return ESeinAssetType::Unit;
	}

	if (Blueprint->ParentClass->IsChildOf(USeinAbility::StaticClass()))
	{
		return ESeinAssetType::Ability;
	}

	return ESeinAssetType::None;
}

FLinearColor USeinBlueprintThumbnailRenderer::GetBarColor(ESeinAssetType Type)
{
	switch (Type)
	{
	case ESeinAssetType::Unit:      return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("0095FF"))); // #0095FF
	case ESeinAssetType::Ability:   return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("FF0000"))); // #FF0000
	case ESeinAssetType::Effect:    return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("FFFF00"))); // #FFFF00
	case ESeinAssetType::Widget:    return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("0095FF"))); // #0095FF
	default:                        return FLinearColor::Transparent;
	}
}

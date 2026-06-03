/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinStructThumbnailRenderer.cpp
 * @brief   Custom thumbnail for Sein-marked UUserDefinedStruct assets.
 */

#include "Thumbnails/SeinStructThumbnailRenderer.h"
#include "SeinARTSEditorStyle.h"
#include "Factories/SeinSimComponentFactory.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"

bool USeinStructThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
	return USeinSimComponentFactory::IsSeinDeterministicStruct(Cast<UUserDefinedStruct>(Object));
}

void USeinStructThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height,
	FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	const UUserDefinedStruct* UDS = Cast<UUserDefinedStruct>(Object);
	if (!USeinSimComponentFactory::IsSeinDeterministicStruct(UDS))
	{
		// Not a Sein UDS — fall through to UE's default struct thumbnail.
		Super::Draw(Object, X, Y, Width, Height, RenderTarget, Canvas, bAdditionalViewFamily);
		return;
	}

	// ---- Dark background (matches SeinBlueprintThumbnailRenderer aesthetic) ----
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

	// ---- Component icon (from Slate style set) ----
	UTexture2D* IconTexture = FSeinARTSEditorStyle::GetIconTexture(FName(TEXT("SeinComponentIcon92")));
	const FTexture* IconResource = IconTexture ? IconTexture->GetResource() : nullptr;
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

	// ---- Color bar (#00FFFF — component identity) ----
	// UDS assets don't use IAssetTypeActions::GetTypeColor the way BPs do, so we
	// paint the identity bar ourselves along the bottom edge of the thumbnail.
	{
		const float BarHeight = FMath::Max(2.0f, Height * 0.04f);
		FCanvasTileItem BarItem(
			FVector2D(X, Y + Height - BarHeight),
			GWhiteTexture,
			FVector2D(Width, BarHeight),
			FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("00FFFF")))
		);
		BarItem.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(BarItem);
	}
}

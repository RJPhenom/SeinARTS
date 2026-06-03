/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinStructThumbnailRenderer.h
 * @brief   Thumbnail renderer for UUserDefinedStruct assets. Sein-marked
 *          UDSes get the component icon + identity bar; non-Sein UDSes fall
 *          through to UE's default struct thumbnail.
 */

#pragma once

#include "CoreMinimal.h"
#include "ThumbnailRendering/DefaultSizedThumbnailRenderer.h"
#include "SeinStructThumbnailRenderer.generated.h"

UCLASS()
class USeinStructThumbnailRenderer : public UDefaultSizedThumbnailRenderer
{
	GENERATED_BODY()

public:
	virtual bool CanVisualizeAsset(UObject* Object) override;
	virtual void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height,
		FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily) override;
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinBlueprintThumbnailRenderer.h
 * @date:		4/12/2026
 * @author:		RJ Macklem
 * @brief:		Custom thumbnail renderer for SeinARTS Blueprint assets.
 *				Draws type-specific icons and colored identity bars.
 *				Non-SeinARTS Blueprints fall through to default rendering.
 */

#pragma once

#include "CoreMinimal.h"
#include "ThumbnailRendering/BlueprintThumbnailRenderer.h"
#include "SeinBlueprintThumbnailRenderer.generated.h"

/**
 * Extends the default Blueprint thumbnail renderer.
 * SeinARTS Blueprints get: dark bg + type icon + colored bar.
 * Everything else passes through to UBlueprintThumbnailRenderer::Draw().
 *
 *   Unit (ASeinActor)      — Unit icon + Blue bar   (#0095FF)
 *   Ability (USeinAbility) — Ability icon + Red bar  (#FF0000)
 */
UCLASS()
class USeinBlueprintThumbnailRenderer : public UBlueprintThumbnailRenderer
{
	GENERATED_BODY()

public:
	virtual void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height,
		FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily) override;
	virtual bool CanVisualizeAsset(UObject* Object) override;

private:
	enum class ESeinAssetType : uint8 { None, Unit, Ability, Effect, Widget };
	static ESeinAssetType ClassifyBlueprint(UObject* Object);
	static FLinearColor GetBarColor(ESeinAssetType Type);

	/** Returns the FTexture render resource for the icon, pulled from the Slate style set. */
	const FTexture* GetIconResource(ESeinAssetType Type);
};

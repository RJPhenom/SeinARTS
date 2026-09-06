/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinDebugLegend.h
 * @author       RJ Macklem
 * @created      05 Sep 2026
 * @latest       05 Sep 2026
 * @brief        Shared visibility and per-view layout for debug legend panels.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */
#pragma once

#include "EngineDefines.h"
#if UE_ENABLE_DEBUG_DRAWING
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "SceneView.h"
#include "Settings/PluginSettings.h"

namespace UE::SeinARTS::DebugLegend
{
	enum class EPanel : uint8 { Navigation, Steering, Extents, FogOfWar };

	/** Stateless layout: callback order and other worlds cannot move this view's panels. */
	class FPanel
	{
	public:
		FPanel(UCanvas* InCanvas, EPanel Panel) : Canvas(InCanvas)
		{
			if (!Canvas || !Canvas->Canvas || !Canvas->SceneView || !Canvas->SceneView->Family || !GEngine
				|| !GetDefault<USeinARTSCoreSettings>()->bShowDebugLegends) return;
			static const TCHAR* Flags[] = { TEXT("SeinNavigation"), TEXT("SeinSteering"), TEXT("SeinExtents"), TEXT("FogOfWar") };
			static constexpr float Heights[] = { 112, 112, 80, 128 };
			const int32 Index = static_cast<int32>(Panel);
			const auto& ShowFlags = Canvas->SceneView->Family->EngineShowFlags;
			const int32 OwnFlag = FEngineShowFlags::FindIndexByName(Flags[Index]);
			if (OwnFlag == INDEX_NONE || !ShowFlags.GetSingleFlag(OwnFlag)) return;
			Y = 16;
			for (int32 I = 0; I < Index; ++I)
			{
				const int32 Flag = FEngineShowFlags::FindIndexByName(Flags[I]);
				if (Flag != INDEX_NONE && ShowFlags.GetSingleFlag(Flag)) Y += Heights[I] + 8;
			}
			X = FMath::Max(16.0f, Canvas->ClipX - 680.0f);
			bVisible = true;
			FCanvasTileItem Backdrop(FVector2D(X - 8, Y), FVector2D(672, Heights[Index]), FLinearColor(0, 0, 0, 0.85f));
			Backdrop.BlendMode = SE_BLEND_Translucent;
			// Canvas draws lower depth keys last. Keep every legend above the
			// geometry, including geometry submitted by a later view callback.
			Canvas->Canvas->PushDepthSortKey(-100);
			Canvas->DrawItem(Backdrop);
			Canvas->Canvas->PopDepthSortKey();
		}

		bool IsVisible() const { return bVisible; }
		void Text(float RowOffset, const FString& Value, const FLinearColor& Color) const
		{
			if (!bVisible) return;
			FCanvasTextItem Item(FVector2D(X, Y + RowOffset), FText::FromString(Value), GEngine->GetSmallFont(), Color);
			Canvas->Canvas->PushDepthSortKey(-101);
			Canvas->DrawItem(Item);
			Canvas->Canvas->PopDepthSortKey();
		}

	private:
		UCanvas* Canvas;
		float X = 0, Y = 0;
		bool bVisible = false;
	};
}
#endif

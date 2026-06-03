/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarEntityDraw.h
 * @brief   Static draw helper bound to FSeinARTSEditorModule's per-component
 *          draw registry. Walks the entity bridge's ComponentData for
 *          `FSeinVisionComponent` entries and draws each stamp's Radial /
 *          Rect / Conical shape in the BP / level editor viewport.
 *
 *          Lives in SeinARTSFogOfWarEditor so SeinARTSEditor itself stays
 *          ignorant of FoW. The editor module's StartupModule binds the
 *          function below and registers it via
 *          `FSeinARTSEditorModule::RegisterComponentDataDraw`.
 */

#pragma once

#include "CoreMinimal.h"

class FPrimitiveDrawInterface;
struct FInstancedStruct;

namespace SeinFogOfWarEntityDraw
{
	/** Bound at StartupModule. See FSeinComponentDataDrawDelegate. */
	void DrawVisionStamps(
		const TArray<FInstancedStruct>& ComponentData,
		const FQuat& ActorQuat, const FVector& ActorPos,
		FPrimitiveDrawInterface* PDI);
}

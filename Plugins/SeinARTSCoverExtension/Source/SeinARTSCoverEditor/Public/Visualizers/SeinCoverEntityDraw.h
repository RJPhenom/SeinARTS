/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverEntityDraw.h
 * @brief   Static draw helper bound to FSeinARTSEditorModule's per-component
 *          draw registry. Walks the entity bridge's ComponentData for
 *          `FSeinCoverComponent` entries and draws each provider's Area
 *          volume + Slots in the BP / level editor viewport.
 *
 *          Lives in SeinARTSCoverEditor so SeinARTSEditor itself stays
 *          ignorant of cover. The cover editor module's StartupModule binds
 *          the function below and registers it via
 *          `FSeinARTSEditorModule::RegisterComponentDataDraw`.
 */

#pragma once

#include "CoreMinimal.h"

class FPrimitiveDrawInterface;
struct FInstancedStruct;

namespace SeinCoverEntityDraw
{
	/** Bound at StartupModule. See FSeinComponentDataDrawDelegate. */
	void DrawCoverEntries(
		const TArray<FInstancedStruct>& ComponentData,
		const FQuat& ActorQuat, const FVector& ActorPos,
		FPrimitiveDrawInterface* PDI);
}

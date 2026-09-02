/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEntityComponentVisualizer.h
 * @brief   Component visualizer that walks USeinEntityBridgeComponent::ComponentData
 *          and draws every recognized FSein*Component entry's debug geometry
 *          in the BP editor + level editor viewports. Read-only — all
 *          editing happens through the Details panel.
 *
 *          Built-in layers (drawn unconditionally):
 *            - `FSeinExtentsPayload` shapes (Box → yellow wire, Capsule →
 *              cyan wire) at LocalOffset + YawOffset, extending upward by
 *              Height.
 *            - `FSeinProductionPayload` spawn points — green wire sphere
 *              + forward arrow + faint tether at the resolved
 *              `ActorTransform * SpawnPointOffset` location.
 *            - `FSeinNavigationPayload` footprint — orange wire circle
 *              at the actor position with radius `FootprintRadius`. Zero-
 *              radius (intangible) entries self-skip.
 *
 *          Optional layers register a draw delegate via
 *          `FSeinARTSEditorModule::RegisterComponentDataDraw` at module
 *          StartupModule. Decoupling rule: SeinARTSEditor stays ignorant of
 *          which optional systems are loaded — the visualizer just walks the
 *          registered delegates and asks each one to draw. Current optional
 *          registrants (each in its own editor module, independently
 *          disable-able):
 *            - SeinARTSFogOfWar  → vision stamps (radial / rect / conical)
 *            - SeinARTSCoverEditor → cover Area volume + slot markers
 *
 *          Single visualizer registered against the bridge because UE's
 *          ComponentVisualizerMap is keyed by class name with a single
 *          value per key — multiple per-feature visualizers would clobber
 *          each other. The callback fan-out is how we work around that.
 */

#pragma once

#include "CoreMinimal.h"
#include "ComponentVisualizer.h"

class FSeinEntityComponentVisualizer : public FComponentVisualizer
{
public:
	virtual void DrawVisualization(
		const UActorComponent* Component,
		const FSceneView* View,
		FPrimitiveDrawInterface* PDI) override;
};

/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSCoverModule.h
 * @brief   Module declaration for the optional cover system.
 *
 *          Opt-in module — no other framework module depends on it. Disabling
 *          this module strips:
 *            - Cover-providing entity types and area / edge cover queries
 *            - Destination preview decals (the live-cursor formation
 *              visualization that colors slots by cover quality)
 *            - Cover-aware formation snapping in the broker resolver
 *
 *          Phase 1 (current) ships only the destination preview infrastructure
 *          with neutral / cover-agnostic decals. Phase 2 adds cover entities
 *          + queries; phase 3 wires color-coding + snap-to-cover into the
 *          formation solver via the resolver hook.
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FSeinARTSCoverModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

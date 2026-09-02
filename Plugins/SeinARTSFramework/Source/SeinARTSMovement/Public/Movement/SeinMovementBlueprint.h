/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementBlueprint.h
 * @brief   Thin UBlueprint subclass that identifies a Blueprint as a movement mode. Mirrors the
 *          SeinFormationBlueprint pattern so the editor can attach asset type actions, color,
 *          icon, and thumbnail renderer specific to movement modes.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/Blueprint.h"
#include "SeinMovementBlueprint.generated.h"

/**
 * Tags a Blueprint asset as a SeinARTS movement mode so the editor treats it like one. You do not
 * pick this class directly; it is the asset type behind movement mode Blueprints in the Content
 * Browser.
 *
 * A thin UBlueprint subclass carrying no data or logic of its own. Its only job is to give
 * movement mode Blueprints a distinct asset type, so the editor can attach mode-specific asset
 * actions, color, icon, and thumbnail renderer to them. It mirrors the Sein Formation Blueprint
 * pattern. SupportedByDefaultBlueprintFactory returns false so the generic Blueprint factory
 * never offers to create one; movement mode Blueprints are created through their own dedicated
 * factory instead. (Modes authored before this class existed are plain UBlueprints — the editor
 * still recognizes them by parent class, but they keep the generic Blueprint type color until
 * recreated through the factory.)
 */
UCLASS(BlueprintType)
class SEINARTSMOVEMENT_API USeinMovementBlueprint : public UBlueprint
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual bool SupportedByDefaultBlueprintFactory() const override { return false; }
#endif
};
